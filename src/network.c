#include "interrupts.h"
#include "io.h"
#include "memory.h"
#include "network.h"
#include "string.h"

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC

#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139

#define RTL_IDR0 0x00
#define RTL_TSD0 0x10
#define RTL_TSAD0 0x20
#define RTL_RBSTART 0x30
#define RTL_CR 0x37
#define RTL_CAPR 0x38
#define RTL_CBR 0x3A
#define RTL_IMR 0x3C
#define RTL_RCR 0x44
#define RTL_CONFIG1 0x52

#define RTL_RX_SIZE 32768
#define RTL_RX_BUFFER_SIZE (RTL_RX_SIZE + 16)
#define RTL_TX_BUFFER_SIZE 2048

#define ETH_HEADER_SIZE 14
#define ARP_PACKET_SIZE 28
#define IPV4_HEADER_SIZE 20
#define ICMP_HEADER_SIZE 8

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IPV6 0x86DD
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6
#define IP_PROTOCOL_UDP 17
#define IP_PROTOCOL_ICMPV6 58
#define IPV6_HEADER_SIZE 40
#define ICMPV6_NA 136
#define ICMPV6_NS 135

#define ARP_CACHE_SIZE 8
#define TCP_RX_WINDOW 4096
#define TCP_SEND_CHUNK 1200

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define STREAM_LIMIT 6
#define STREAM_LISTENER_LIMIT 3

typedef enum {
    TCP_STATE_CLOSED,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_CLOSE_WAIT
} tcp_state;

typedef struct {
    uint32_t address;
    uint8_t mac[6];
    bool valid;
} arp_entry;

typedef struct {
    bool used;
    uint32_t generation;
    tcp_state state;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t remote_ip;
    uint8_t remote_mac[6];
    uint32_t send_next;
    uint32_t receive_next;
    uint32_t deadline;
    tcp_stream_handler handler;
    void *context;
} tcp_stream;

typedef struct {
    bool used;
    uint16_t port;
    tcp_stream_handler handler;
    void *context;
} tcp_listener;

static uint16_t io_base;
static bool available;
static uint8_t local_mac[6];
static uint32_t local_ip;
static uint32_t gateway_ip;
static uint32_t ip_id;
static uint32_t packets_received;
static uint32_t packets_sent;
static uint32_t tx_errors;
static uint32_t last_tx_status;
static uint32_t rx_offset;
static uint8_t tx_round_robin;
static arp_entry arp_cache[ARP_CACHE_SIZE];
static bool ping_replied;

static uint8_t *rx_buffer;
static uint8_t tx_buffers[4][RTL_TX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t packet_buffer[1600] __attribute__((aligned(16)));

static tcp_state connection_state;
static tcp_event_handler connection_handler;
static uint16_t listen_port;
static uint16_t remote_port;
static uint32_t remote_ip;
static uint8_t remote_mac[6];
static uint32_t send_next;
static uint32_t receive_next;
static uint32_t tcp_data_segments;
static uint32_t tcp_data_bytes;
static uint32_t tcp_accepted_segments;
static uint32_t tcp_last_sequence;
static uint32_t tcp_write_attempts;
static uint32_t tcp_write_failures;
static uint32_t tcp_last_write_state;
static uint32_t tcp_segment_failure;
static uint32_t ipv4_send_failure;
static tcp_state client_state;
static tcp_event_handler client_handler;
static uint16_t client_local_port;
static uint16_t client_port_sequence;
static uint16_t client_remote_port;
static uint32_t client_remote_ip;
static uint8_t client_remote_mac[6];
static uint8_t client_remote_ipv6[16];
static bool client_ipv6;
static uint32_t client_send_next;
static uint32_t client_receive_next;
static uint16_t udp_receive_port;
static tcp_event_handler udp_handler;
static uint8_t local_ipv6[16] = {
    0xFE, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x15
};
static uint8_t ipv6_neighbor[16];
static uint8_t ipv6_neighbor_mac[6];
static bool ipv6_neighbor_valid;
static tcp_stream streams[STREAM_LIMIT];
static tcp_listener stream_listeners[STREAM_LISTENER_LIMIT];
static uint32_t stream_generation = 1;
static uint16_t stream_port_sequence;

static uint16_t read_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static void write_be16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t *data) {
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16
         | (uint32_t)data[2] << 8 | data[3];
}

static void write_be32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t internet_checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    while (length > 1) {
        sum += read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length) {
        sum += (uint16_t)data[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t ipv6_checksum(const uint8_t source[16],
                              const uint8_t destination[16],
                              uint8_t next_header, const uint8_t *data,
                              uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t index = 0; index < 16; index += 2) {
        sum += read_be16(source + index);
        sum += read_be16(destination + index);
    }
    sum += length >> 16;
    sum += length & 0xFFFF;
    sum += next_header;
    while (length > 1) {
        sum += read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length) sum += (uint16_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000U
                     | (uint32_t)bus << 16
                     | (uint32_t)device << 11
                     | (uint32_t)function << 8
                     | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t device, uint8_t function,
                      uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000U
                     | (uint32_t)bus << 16
                     | (uint32_t)device << 11
                     | (uint32_t)function << 8
                     | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static bool find_rtl8139(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint32_t identity = pci_read((uint8_t)bus, device, 0, 0);
            uint16_t vendor = (uint16_t)identity;
            uint16_t product = (uint16_t)(identity >> 16);
            if (vendor != RTL_VENDOR_ID || product != RTL_DEVICE_ID) {
                continue;
            }
            uint32_t command = pci_read((uint8_t)bus, device, 0, 4);
            pci_write((uint8_t)bus, device, 0, 4, command | 0x5);
            uint32_t bar = pci_read((uint8_t)bus, device, 0, 0x10);
            if ((bar & 1) == 0) {
                return false;
            }
            io_base = (uint16_t)(bar & 0xFFFC);
            return io_base != 0;
        }
    }
    return false;
}

static void rtl_write8(uint16_t offset, uint8_t value) {
    outb(io_base + offset, value);
}

static void rtl_write16(uint16_t offset, uint16_t value) {
    outw(io_base + offset, value);
}

static void rtl_write32(uint16_t offset, uint32_t value) {
    outl(io_base + offset, value);
}

static uint8_t rtl_read8(uint16_t offset) {
    return inb(io_base + offset);
}

static uint32_t rtl_read32(uint16_t offset) {
    return inl(io_base + offset);
}

static bool rtl_send(const uint8_t *packet, uint32_t length) {
    if (!available || length < ETH_HEADER_SIZE || length > RTL_TX_BUFFER_SIZE) {
        return false;
    }
    uint8_t index = tx_round_robin++ & 3;
    uint16_t status_port = RTL_TSD0 + index * 4;
    memcpy(tx_buffers[index], packet, length);
    rtl_write32(RTL_TSAD0 + index * 4, (uint32_t)(uintptr_t)tx_buffers[index]);
    rtl_write32(status_port, length & 0x1FFF);
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        last_tx_status = rtl_read32(status_port);
        if (last_tx_status & (1U << 15)) {
            packets_sent++;
            return true;
        }
        if (last_tx_status & (1U << 14)) {
            break;
        }
        cpu_pause();
    }
    tx_errors++;
    return false;
}

static void arp_cache_update(uint32_t address, const uint8_t *mac) {
    uint32_t slot = ARP_CACHE_SIZE;
    for (uint32_t index = 0; index < ARP_CACHE_SIZE; index++) {
        if (arp_cache[index].valid && arp_cache[index].address == address) {
            slot = index;
            break;
        }
        if (!arp_cache[index].valid && slot == ARP_CACHE_SIZE) {
            slot = index;
        }
    }
    if (slot == ARP_CACHE_SIZE) {
        slot = 0;
    }
    arp_cache[slot].address = address;
    arp_cache[slot].valid = true;
    memcpy(arp_cache[slot].mac, mac, 6);
}

static bool arp_cache_lookup(uint32_t address, uint8_t *mac) {
    for (uint32_t index = 0; index < ARP_CACHE_SIZE; index++) {
        if (arp_cache[index].valid && arp_cache[index].address == address) {
            memcpy(mac, arp_cache[index].mac, 6);
            return true;
        }
    }
    return false;
}

static void send_arp_request(uint32_t target_ip) {
    memset(packet_buffer, 0, ETH_HEADER_SIZE + ARP_PACKET_SIZE);
    memset(packet_buffer, 0xFF, 6);
    memcpy(packet_buffer + 6, local_mac, 6);
    write_be16(packet_buffer + 12, ETHERTYPE_ARP);

    uint8_t *arp = packet_buffer + ETH_HEADER_SIZE;
    write_be16(arp + 0, 1);
    write_be16(arp + 2, ETHERTYPE_IPV4);
    arp[4] = 6;
    arp[5] = 4;
    write_be16(arp + 6, 1);
    memcpy(arp + 8, local_mac, 6);
    write_be32(arp + 14, local_ip);
    write_be32(arp + 24, target_ip);
    rtl_send(packet_buffer, ETH_HEADER_SIZE + ARP_PACKET_SIZE);
}

static void send_arp_reply(const uint8_t *request) {
    memset(packet_buffer, 0, ETH_HEADER_SIZE + ARP_PACKET_SIZE);
    memcpy(packet_buffer, request + 6, 6);
    memcpy(packet_buffer + 6, local_mac, 6);
    write_be16(packet_buffer + 12, ETHERTYPE_ARP);

    uint8_t *arp = packet_buffer + ETH_HEADER_SIZE;
    write_be16(arp + 0, 1);
    write_be16(arp + 2, ETHERTYPE_IPV4);
    arp[4] = 6;
    arp[5] = 4;
    write_be16(arp + 6, 2);
    memcpy(arp + 8, local_mac, 6);
    write_be32(arp + 14, local_ip);
    memcpy(arp + 18, request + 22, 6);
    write_be32(arp + 24, read_be32(request + 28));
    rtl_send(packet_buffer, ETH_HEADER_SIZE + ARP_PACKET_SIZE);
}

static void handle_arp(const uint8_t *packet, uint32_t length) {
    if (length < ETH_HEADER_SIZE + ARP_PACKET_SIZE) {
        return;
    }
    const uint8_t *arp = packet + ETH_HEADER_SIZE;
    if (read_be16(arp) != 1 || read_be16(arp + 2) != ETHERTYPE_IPV4
        || arp[4] != 6 || arp[5] != 4) {
        return;
    }
    uint32_t sender_ip = read_be32(arp + 14);
    arp_cache_update(sender_ip, arp + 8);
    uint16_t operation = read_be16(arp + 6);
    if (operation == 1 && read_be32(arp + 24) == local_ip) {
        send_arp_reply(packet);
    }
}

static bool send_ipv4(const uint8_t *destination_mac, uint32_t destination_ip,
                      uint8_t protocol, const uint8_t *payload, uint32_t payload_length) {
    uint32_t total = IPV4_HEADER_SIZE + payload_length;
    if (total + ETH_HEADER_SIZE > sizeof(packet_buffer)) {
        ipv4_send_failure = 1;
        return false;
    }
    memset(packet_buffer, 0, ETH_HEADER_SIZE + IPV4_HEADER_SIZE);
    memcpy(packet_buffer, destination_mac, 6);
    memcpy(packet_buffer + 6, local_mac, 6);
    write_be16(packet_buffer + 12, ETHERTYPE_IPV4);

    uint8_t *ip = packet_buffer + ETH_HEADER_SIZE;
    ip[0] = 0x45;
    ip[1] = 0;
    write_be16(ip + 2, (uint16_t)total);
    write_be16(ip + 4, (uint16_t)ip_id++);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = protocol;
    write_be32(ip + 12, local_ip);
    write_be32(ip + 16, destination_ip);
    write_be16(ip + 10, internet_checksum(ip, IPV4_HEADER_SIZE));
    memcpy(ip + IPV4_HEADER_SIZE, payload, payload_length);
    if (!rtl_send(packet_buffer, ETH_HEADER_SIZE + total)) {
        ipv4_send_failure = 2;
        return false;
    }
    return true;
}

static bool send_ipv6(const uint8_t destination_mac[6],
                      const uint8_t destination_ip[16],
                      uint8_t next_header, const uint8_t *payload,
                      uint32_t payload_length) {
    uint32_t total = IPV6_HEADER_SIZE + payload_length;
    if (total + ETH_HEADER_SIZE > sizeof(packet_buffer)) return false;
    memset(packet_buffer, 0, ETH_HEADER_SIZE + IPV6_HEADER_SIZE);
    memcpy(packet_buffer, destination_mac, 6);
    memcpy(packet_buffer + 6, local_mac, 6);
    write_be16(packet_buffer + 12, ETHERTYPE_IPV6);
    uint8_t *ip = packet_buffer + ETH_HEADER_SIZE;
    ip[0] = 0x60;
    write_be16(ip + 4, (uint16_t)payload_length);
    ip[6] = next_header;
    ip[7] = 64;
    memcpy(ip + 8, local_ipv6, 16);
    memcpy(ip + 24, destination_ip, 16);
    memcpy(ip + IPV6_HEADER_SIZE, payload, payload_length);
    return rtl_send(packet_buffer, ETH_HEADER_SIZE + total);
}

static void ipv6_solicited_node(const uint8_t address[16], uint8_t multicast[16]) {
    memset(multicast, 0, 16);
    multicast[0] = 0xFF;
    multicast[1] = 0x02;
    multicast[11] = 0x01;
    multicast[12] = 0xFF;
    multicast[13] = address[13];
    multicast[14] = address[14];
    multicast[15] = address[15];
}

static void ipv6_multicast_mac(const uint8_t address[16], uint8_t mac[6]) {
    mac[0] = 0x33;
    mac[1] = 0x33;
    memcpy(mac + 2, address + 12, 4);
}

static bool send_neighbor_solicitation(const uint8_t target[16]) {
    uint8_t multicast[16];
    uint8_t mac[6];
    uint8_t packet[32];
    memset(packet, 0, sizeof(packet));
    packet[0] = ICMPV6_NS;
    memcpy(packet + 8, target, 16);
    packet[24] = 1;
    packet[25] = 1;
    memcpy(packet + 26, local_mac, 6);
    ipv6_solicited_node(target, multicast);
    ipv6_multicast_mac(multicast, mac);
    write_be16(packet + 2, ipv6_checksum(local_ipv6, multicast,
                                        IP_PROTOCOL_ICMPV6, packet, sizeof(packet)));
    return send_ipv6(mac, multicast, IP_PROTOCOL_ICMPV6, packet, sizeof(packet));
}

static bool resolve_ipv6_neighbor(const uint8_t address[16], uint8_t mac[6],
                                  uint32_t timeout_ms) {
    if (ipv6_neighbor_valid
        && memcmp(ipv6_neighbor, address, 16) == 0) {
        memcpy(mac, ipv6_neighbor_mac, 6);
        return true;
    }
    if (!send_neighbor_solicitation(address)) return false;
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (ipv6_neighbor_valid
            && memcmp(ipv6_neighbor, address, 16) == 0) {
            memcpy(mac, ipv6_neighbor_mac, 6);
            return true;
        }
        cpu_pause();
    }
    return false;
}

static void handle_icmpv6(const uint8_t *packet, uint32_t length,
                          const uint8_t source[16], const uint8_t source_mac[6]) {
    if (length < ETH_HEADER_SIZE + IPV6_HEADER_SIZE + 24) return;
    const uint8_t *payload = packet + ETH_HEADER_SIZE + IPV6_HEADER_SIZE;
    if (payload[0] == ICMPV6_NA) {
        memcpy(ipv6_neighbor, payload + 8, 16);
        memcpy(ipv6_neighbor_mac, source_mac, 6);
        ipv6_neighbor_valid = true;
    } else if (payload[0] == ICMPV6_NS
               && memcmp(payload + 8, local_ipv6, 16) == 0) {
        uint8_t multicast[16];
        ipv6_solicited_node(source, multicast);
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = ICMPV6_NA;
        reply[4] = 0x60;
        memcpy(reply + 8, local_ipv6, 16);
        reply[24] = 1;
        reply[25] = 1;
        memcpy(reply + 26, local_mac, 6);
        write_be16(reply + 2, ipv6_checksum(local_ipv6, source,
                                           IP_PROTOCOL_ICMPV6,
                                           reply, sizeof(reply)));
        send_ipv6(source_mac, source, IP_PROTOCOL_ICMPV6, reply, sizeof(reply));
    }
}

static bool send_udp(uint32_t destination_ip, uint16_t source_port,
                     uint16_t destination_port, const uint8_t *data,
                     uint32_t length) {
    if (length > 1400) return false;
    uint8_t mac[6];
    if (!network_arp_resolve(destination_ip, mac, 1000)) return false;
    uint8_t udp[1408];
    write_be16(udp + 0, source_port);
    write_be16(udp + 2, destination_port);
    write_be16(udp + 4, (uint16_t)(length + 8));
    write_be16(udp + 6, 0);
    memcpy(udp + 8, data, length);
    return send_ipv4(mac, destination_ip, IP_PROTOCOL_UDP, udp, length + 8);
}

static void handle_udp(const uint8_t *packet, uint32_t length) {
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    uint32_t ip_header_length = (uint32_t)(ip[0] & 0x0F) * 4;
    if (length < ETH_HEADER_SIZE + ip_header_length + 8) return;
    const uint8_t *udp = ip + ip_header_length;
    uint16_t destination_port = read_be16(udp + 2);
    uint16_t udp_length = read_be16(udp + 4);
    if (udp_length < 8 || ip_header_length + udp_length > read_be16(ip + 2)) {
        return;
    }
    if (udp_receive_port && destination_port == udp_receive_port && udp_handler) {
        udp_handler(TCP_EVENT_DATA, udp + 8, udp_length - 8);
    }
}

static void handle_icmp(const uint8_t *packet, uint32_t length,
                        uint32_t source_ip, const uint8_t *source_mac) {
    if (length < IPV4_HEADER_SIZE + ICMP_HEADER_SIZE) {
        return;
    }
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    uint32_t ip_header_length = (uint32_t)(ip[0] & 0x0F) * 4;
    if (ip_header_length < IPV4_HEADER_SIZE || length < ip_header_length + ICMP_HEADER_SIZE) {
        return;
    }
    const uint8_t *icmp = ip + ip_header_length;
    if (icmp[0] == 8) {
        uint32_t icmp_length = read_be16(ip + 2) - ip_header_length;
        memcpy(packet_buffer + ETH_HEADER_SIZE + IPV4_HEADER_SIZE,
               icmp, icmp_length);
        uint8_t *reply = packet_buffer + ETH_HEADER_SIZE + IPV4_HEADER_SIZE;
        reply[0] = 0;
        reply[2] = 0;
        reply[3] = 0;
        write_be16(reply + 2, internet_checksum(reply, icmp_length));
        send_ipv4(source_mac, source_ip, IP_PROTOCOL_ICMP, reply, icmp_length);
    } else if (icmp[0] == 0) {
        ping_replied = true;
    }
}

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const uint8_t *segment, uint32_t length) {
    uint32_t sum = 0;
    sum += (source >> 16) & 0xFFFF;
    sum += source & 0xFFFF;
    sum += (destination >> 16) & 0xFFFF;
    sum += destination & 0xFFFF;
    sum += IP_PROTOCOL_TCP;
    sum += length;
    while (length > 1) {
        sum += read_be16(segment);
        segment += 2;
        length -= 2;
    }
    if (length) {
        sum += (uint16_t)segment[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static bool tcp_send_segment(uint8_t flags, const uint8_t *payload, uint32_t length) {
    uint8_t segment[TCP_SEND_CHUNK + 20];
    if (length > TCP_SEND_CHUNK) {
        tcp_segment_failure = 1;
        return false;
    }
    if (connection_state == TCP_STATE_CLOSED) {
        tcp_segment_failure = 2;
        return false;
    }
    memset(segment, 0, 20);
    write_be16(segment + 0, listen_port);
    write_be16(segment + 2, remote_port);
    write_be32(segment + 4, send_next);
    write_be32(segment + 8, receive_next);
    segment[12] = 5 << 4;
    segment[13] = flags;
    write_be16(segment + 14, TCP_RX_WINDOW);
    write_be16(segment + 16, 0);
    write_be16(segment + 18, 0);
    if (length) {
        memcpy(segment + 20, payload, length);
    }
    write_be16(segment + 16, tcp_checksum(local_ip, remote_ip, segment, 20 + length));
    bool sent = send_ipv4(remote_mac, remote_ip, IP_PROTOCOL_TCP, segment, 20 + length);
    if (!sent) {
        tcp_segment_failure = 3;
    }
    if (sent && (flags & (TCP_SYN | TCP_FIN))) {
        send_next++;
    } else if (sent) {
        send_next += length;
    }
    return sent;
}

static void tcp_send_ack(void) {
    tcp_send_segment(TCP_ACK, 0, 0);
}

static void tcp_reset_connection(tcp_state next_state) {
    connection_state = next_state;
    remote_port = 0;
    remote_ip = 0;
    memset(remote_mac, 0, sizeof(remote_mac));
    send_next = 0;
    receive_next = 0;
}

static uint32_t tcp_initial_sequence(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return low ^ (high * 0x9E3779B9U) ^ timer_ticks() ^ 0x434B4C53U;
}

static bool client_send_segment(uint8_t flags, const uint8_t *payload,
                                uint32_t length) {
    if (client_state == TCP_STATE_CLOSED || length > TCP_SEND_CHUNK) return false;
    uint8_t segment[TCP_SEND_CHUNK + 20];
    memset(segment, 0, 20);
    write_be16(segment + 0, client_local_port);
    write_be16(segment + 2, client_remote_port);
    write_be32(segment + 4, client_send_next);
    write_be32(segment + 8, client_receive_next);
    segment[12] = 5 << 4;
    segment[13] = flags;
    write_be16(segment + 14, TCP_RX_WINDOW);
    if (length) memcpy(segment + 20, payload, length);
    write_be16(segment + 16,
               tcp_checksum(local_ip, client_remote_ip, segment, 20 + length));
    bool sent = send_ipv4(client_remote_mac, client_remote_ip, IP_PROTOCOL_TCP,
                          segment, 20 + length);
    if (sent) {
        if (flags & (TCP_SYN | TCP_FIN)) client_send_next++;
        else client_send_next += length;
    }
    return sent;
}

static void client_reset(void) {
    client_state = TCP_STATE_CLOSED;
    client_ipv6 = false;
    client_remote_port = 0;
    client_remote_ip = 0;
    client_send_next = 0;
    client_receive_next = 0;
}

static bool client_send_segment6(uint8_t flags, const uint8_t *payload,
                                 uint32_t length) {
    if (client_state == TCP_STATE_CLOSED || length > TCP_SEND_CHUNK) return false;
    uint8_t segment[TCP_SEND_CHUNK + 20];
    memset(segment, 0, 20);
    write_be16(segment + 0, client_local_port);
    write_be16(segment + 2, client_remote_port);
    write_be32(segment + 4, client_send_next);
    write_be32(segment + 8, client_receive_next);
    segment[12] = 5 << 4;
    segment[13] = flags;
    write_be16(segment + 14, TCP_RX_WINDOW);
    if (length) memcpy(segment + 20, payload, length);
    write_be16(segment + 16,
               ipv6_checksum(local_ipv6, client_remote_ipv6, IP_PROTOCOL_TCP,
                             segment, 20 + length));
    if (!send_ipv6(client_remote_mac, client_remote_ipv6, IP_PROTOCOL_TCP,
                   segment, 20 + length)) return false;
    if (flags & (TCP_SYN | TCP_FIN)) client_send_next++;
    else client_send_next += length;
    return true;
}

static bool handle_client_tcp6(const uint8_t *packet, uint32_t length,
                               const uint8_t source[16]) {
    if (!client_ipv6 || client_state == TCP_STATE_CLOSED) return false;
    if (length < ETH_HEADER_SIZE + IPV6_HEADER_SIZE + 20) return false;
    const uint8_t *tcp = packet + ETH_HEADER_SIZE + IPV6_HEADER_SIZE;
    uint16_t source_port = read_be16(tcp);
    uint16_t destination_port = read_be16(tcp + 2);
    if (memcmp(source, client_remote_ipv6, 16) != 0
        || source_port != client_remote_port
        || destination_port != client_local_port) return false;
    uint32_t header_length = (uint32_t)(tcp[12] >> 4) * 4;
    if (header_length < 20
        || IPV6_HEADER_SIZE + header_length > length - ETH_HEADER_SIZE) {
        return true;
    }
    uint32_t sequence = read_be32(tcp + 4);
    uint32_t acknowledgement = read_be32(tcp + 8);
    uint8_t flags = tcp[13];
    const uint8_t *payload = tcp + header_length;
    uint32_t payload_length = read_be16(packet + ETH_HEADER_SIZE + 4)
                            - header_length;
    if (flags & TCP_RST) {
        client_reset();
        if (client_handler) client_handler(TCP_EVENT_DISCONNECTED, 0, 0);
        return true;
    }
    if (client_state == TCP_STATE_SYN_SENT
        && (flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)
        && acknowledgement == client_send_next) {
        client_receive_next = sequence + 1;
        client_state = TCP_STATE_ESTABLISHED;
        client_send_segment6(TCP_ACK, 0, 0);
        if (client_handler) client_handler(TCP_EVENT_CONNECTED, 0, 0);
        return true;
    }
    if (payload_length && sequence == client_receive_next) {
        client_receive_next += payload_length;
        client_send_segment6(TCP_ACK, 0, 0);
        if (client_handler) client_handler(TCP_EVENT_DATA, payload, payload_length);
    }
    if (flags & TCP_FIN) {
        client_receive_next = sequence + payload_length + 1;
        client_send_segment6(TCP_FIN | TCP_ACK, 0, 0);
        client_reset();
        if (client_handler) client_handler(TCP_EVENT_DISCONNECTED, 0, 0);
    }
    return true;
}

static bool handle_client_tcp(const uint8_t *packet, uint32_t length,
                              uint32_t source_ip) {
    if (client_state == TCP_STATE_CLOSED) return false;
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    uint32_t ip_header_length = (uint32_t)(ip[0] & 0x0F) * 4;
    if (length < ETH_HEADER_SIZE + ip_header_length + 20) return false;
    const uint8_t *tcp = ip + ip_header_length;
    uint16_t source_port = read_be16(tcp);
    uint16_t destination_port = read_be16(tcp + 2);
    if (source_ip != client_remote_ip || source_port != client_remote_port
        || destination_port != client_local_port) {
        return false;
    }
    uint32_t tcp_header_length = (uint32_t)(tcp[12] >> 4) * 4;
    if (tcp_header_length < 20
        || ip_header_length + tcp_header_length > read_be16(ip + 2)) {
        return true;
    }
    uint32_t sequence = read_be32(tcp + 4);
    uint32_t acknowledgement = read_be32(tcp + 8);
    uint8_t flags = tcp[13];
    uint32_t payload_length = read_be16(ip + 2) - ip_header_length - tcp_header_length;
    const uint8_t *payload = tcp + tcp_header_length;
    if (flags & TCP_RST) {
        client_reset();
        if (client_handler) client_handler(TCP_EVENT_DISCONNECTED, 0, 0);
        return true;
    }
    if (client_state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)
            && acknowledgement == client_send_next) {
            client_receive_next = sequence + 1;
            client_state = TCP_STATE_ESTABLISHED;
            client_send_segment(TCP_ACK, 0, 0);
            if (client_handler) client_handler(TCP_EVENT_CONNECTED, 0, 0);
        }
        return true;
    }
    if (payload_length && sequence == client_receive_next) {
        client_receive_next += payload_length;
        client_send_segment(TCP_ACK, 0, 0);
        if (client_handler) client_handler(TCP_EVENT_DATA, payload, payload_length);
    } else if (payload_length) {
        client_send_segment(TCP_ACK, 0, 0);
    }
    if (flags & TCP_FIN) {
        client_receive_next = sequence + payload_length + 1;
        client_send_segment(TCP_FIN | TCP_ACK, 0, 0);
        client_reset();
        if (client_handler) client_handler(TCP_EVENT_DISCONNECTED, 0, 0);
    }
    return true;
}

static bool stream_send_segment(int id, uint8_t flags,
                                const uint8_t *payload, uint32_t length) {
    if (id < 0 || id >= STREAM_LIMIT || !streams[id].used
        || length > TCP_SEND_CHUNK) return false;
    tcp_stream *stream = &streams[id];
    uint8_t segment[TCP_SEND_CHUNK + 20];
    memset(segment, 0, 20);
    write_be16(segment + 0, stream->local_port);
    write_be16(segment + 2, stream->remote_port);
    write_be32(segment + 4, stream->send_next);
    write_be32(segment + 8, stream->receive_next);
    segment[12] = 5 << 4;
    segment[13] = flags;
    write_be16(segment + 14, TCP_RX_WINDOW);
    if (length) memcpy(segment + 20, payload, length);
    write_be16(segment + 16,
               tcp_checksum(local_ip, stream->remote_ip,
                            segment, 20 + length));
    if (!send_ipv4(stream->remote_mac, stream->remote_ip,
                   IP_PROTOCOL_TCP, segment, 20 + length)) return false;
    if (flags & (TCP_SYN | TCP_FIN)) stream->send_next++;
    else stream->send_next += length;
    return true;
}

static void stream_release(int id, bool disconnected) {
    if (id < 0 || id >= STREAM_LIMIT || !streams[id].used) return;
    tcp_stream_handler handler = streams[id].handler;
    void *context = streams[id].context;
    memset(&streams[id], 0, sizeof(streams[id]));
    if (disconnected && handler) {
        handler(id, TCP_EVENT_DISCONNECTED, 0, 0, context);
    }
}

static bool handle_stream_tcp(const uint8_t *packet, uint32_t length,
                              uint32_t source_ip, const uint8_t *source_mac) {
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    uint32_t ip_header_length = (uint32_t)(ip[0] & 0x0F) * 4;
    if (length < ETH_HEADER_SIZE + ip_header_length + 20) return false;
    const uint8_t *tcp = ip + ip_header_length;
    uint32_t ip_total_length = read_be16(ip + 2);
    uint32_t tcp_header_length = (uint32_t)(tcp[12] >> 4) * 4;
    if (ip_total_length < ip_header_length + tcp_header_length) return false;
    uint16_t source_port = read_be16(tcp);
    uint16_t destination_port = read_be16(tcp + 2);
    uint32_t sequence = read_be32(tcp + 4);
    uint32_t acknowledgement = read_be32(tcp + 8);
    uint8_t flags = tcp[13];
    uint32_t payload_length =
        ip_total_length - ip_header_length - tcp_header_length;
    const uint8_t *payload = tcp + tcp_header_length;

    for (int id = 0; id < STREAM_LIMIT; id++) {
        tcp_stream *stream = &streams[id];
        if (!stream->used || stream->local_port != destination_port
            || stream->remote_port != source_port
            || stream->remote_ip != source_ip) continue;
        if (flags & TCP_RST) {
            stream_release(id, true);
            return true;
        }
        if (stream->state == TCP_STATE_SYN_SENT) {
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)
                && acknowledgement == stream->send_next) {
                stream->receive_next = sequence + 1;
                stream->state = TCP_STATE_ESTABLISHED;
                stream_send_segment(id, TCP_ACK, 0, 0);
                tcp_stream_handler handler = stream->handler;
                void *context = stream->context;
                if (handler) {
                    handler(id, TCP_EVENT_CONNECTED, 0, 0, context);
                }
            }
            return true;
        }
    }

    if ((flags & TCP_SYN) && !(flags & TCP_ACK)) {
        for (uint32_t listener_index = 0;
             listener_index < STREAM_LISTENER_LIMIT; listener_index++) {
            tcp_listener *listener = &stream_listeners[listener_index];
            if (!listener->used || listener->port != destination_port) continue;
            for (int slot = 0; slot < STREAM_LIMIT; slot++) {
                if (streams[slot].used) continue;
                tcp_stream *stream = &streams[slot];
                memset(stream, 0, sizeof(*stream));
                stream->used = true;
                stream->generation = stream_generation++;
                if (!stream_generation) stream_generation = 1;
                stream->state = TCP_STATE_SYN_RECEIVED;
                stream->local_port = destination_port;
                stream->remote_port = source_port;
                stream->remote_ip = source_ip;
                memcpy(stream->remote_mac, source_mac, 6);
                stream->receive_next = sequence + 1;
                stream->send_next = tcp_initial_sequence();
                stream->handler = listener->handler;
                stream->context = listener->context;
                stream_send_segment(slot, TCP_SYN | TCP_ACK, 0, 0);
                return true;
            }
        }
    }

    for (int id = 0; id < STREAM_LIMIT; id++) {
        tcp_stream *stream = &streams[id];
        if (!stream->used || stream->local_port != destination_port
            || stream->remote_port != source_port
            || stream->remote_ip != source_ip) continue;
        if (flags & TCP_RST) {
            stream_release(id, true);
            return true;
        }
        if (stream->state == TCP_STATE_SYN_RECEIVED
            && (flags & TCP_ACK) && acknowledgement == stream->send_next) {
            stream->state = TCP_STATE_ESTABLISHED;
            tcp_stream_handler handler = stream->handler;
            void *context = stream->context;
            uint32_t generation = stream->generation;
            if (handler) {
                handler(id, TCP_EVENT_CONNECTED, 0, 0, context);
                if (!streams[id].used
                    || streams[id].generation != generation) return true;
            }
        }
        if (payload_length && sequence == stream->receive_next) {
            stream->receive_next += payload_length;
            stream_send_segment(id, TCP_ACK, 0, 0);
            tcp_stream_handler handler = stream->handler;
            void *context = stream->context;
            uint32_t generation = stream->generation;
            if (handler) {
                handler(id, TCP_EVENT_DATA, payload, payload_length, context);
                if (!streams[id].used
                    || streams[id].generation != generation) return true;
            }
        } else if (payload_length) {
            stream_send_segment(id, TCP_ACK, 0, 0);
        }
        if (flags & TCP_FIN) {
            stream->receive_next = sequence + payload_length + 1;
            stream_send_segment(id, TCP_FIN | TCP_ACK, 0, 0);
            stream_release(id, true);
        }
        return true;
    }
    return false;
}

static void handle_tcp(const uint8_t *packet, uint32_t length,
                       uint32_t source_ip, const uint8_t *source_mac) {
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    uint32_t ip_header_length = (uint32_t)(ip[0] & 0x0F) * 4;
    uint32_t ip_total_length = read_be16(ip + 2);
    if (length < ETH_HEADER_SIZE + ip_header_length + 20
        || ip_total_length < ip_header_length + 20) {
        return;
    }
    const uint8_t *tcp = ip + ip_header_length;
    uint32_t tcp_header_length = (uint32_t)(tcp[12] >> 4) * 4;
    if (tcp_header_length < 20 || ip_total_length < ip_header_length + tcp_header_length) {
        return;
    }
    uint16_t source_port = read_be16(tcp);
    uint16_t destination_port = read_be16(tcp + 2);
    uint32_t sequence = read_be32(tcp + 4);
    uint32_t acknowledgement = read_be32(tcp + 8);
    uint8_t flags = tcp[13];
    uint32_t payload_length = ip_total_length - ip_header_length - tcp_header_length;
    const uint8_t *payload = tcp + tcp_header_length;

    if (handle_stream_tcp(packet, length, source_ip, source_mac)
        || handle_client_tcp(packet, length, source_ip)) {
        return;
    }

    if (connection_state == TCP_STATE_LISTEN) {
        if (destination_port != listen_port || !(flags & TCP_SYN) || flags & TCP_ACK) {
            return;
        }
        remote_ip = source_ip;
        remote_port = source_port;
        memcpy(remote_mac, source_mac, 6);
        receive_next = sequence + 1;
        send_next = tcp_initial_sequence();
        connection_state = TCP_STATE_SYN_RECEIVED;
        tcp_send_segment(TCP_SYN | TCP_ACK, 0, 0);
        return;
    }

    if (destination_port != listen_port || source_port != remote_port || source_ip != remote_ip) {
        return;
    }
    if (flags & TCP_RST) {
        tcp_reset_connection(TCP_STATE_LISTEN);
        if (connection_handler) {
            connection_handler(TCP_EVENT_DISCONNECTED, 0, 0);
        }
        return;
    }
    if (flags & TCP_ACK) {
        if (acknowledgement > send_next) {
            tcp_send_ack();
            return;
        }
    }

    if (connection_state == TCP_STATE_SYN_RECEIVED) {
        if ((flags & TCP_ACK) && acknowledgement == send_next) {
            connection_state = TCP_STATE_ESTABLISHED;
            if (connection_handler) {
                connection_handler(TCP_EVENT_CONNECTED, 0, 0);
            }
        } else {
            return;
        }
    }

    if (payload_length) {
        tcp_data_segments++;
        tcp_data_bytes += payload_length;
        tcp_last_sequence = sequence;
        if (sequence == receive_next) {
            tcp_accepted_segments++;
            receive_next += payload_length;
            if (flags & TCP_PSH) {
                receive_next = sequence + payload_length;
            }
            tcp_send_ack();
            if (connection_handler) {
                connection_handler(TCP_EVENT_DATA, payload, payload_length);
            }
        } else {
            tcp_send_ack();
        }
    }

    if (flags & TCP_FIN) {
        receive_next = sequence + payload_length + 1;
        tcp_send_ack();
        tcp_send_segment(TCP_FIN | TCP_ACK, 0, 0);
        connection_state = TCP_STATE_CLOSE_WAIT;
        if (connection_handler) {
            connection_handler(TCP_EVENT_DISCONNECTED, 0, 0);
        }
        tcp_reset_connection(TCP_STATE_LISTEN);
    }
}

static void handle_ipv4(const uint8_t *packet, uint32_t length) {
    if (length < ETH_HEADER_SIZE + IPV4_HEADER_SIZE) {
        return;
    }
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    if ((ip[0] >> 4) != 4) {
        return;
    }
    uint32_t header_length = (uint32_t)(ip[0] & 0x0F) * 4;
    uint32_t total_length = read_be16(ip + 2);
    if (header_length < IPV4_HEADER_SIZE || total_length < header_length
        || length < ETH_HEADER_SIZE + total_length
        || internet_checksum(ip, header_length) != 0) {
        return;
    }
    uint32_t source_ip = read_be32(ip + 12);
    uint32_t destination_ip = read_be32(ip + 16);
    if (destination_ip != local_ip && destination_ip != 0xFFFFFFFFU) {
        return;
    }
    uint8_t source_mac[6];
    memcpy(source_mac, packet + 6, 6);
    arp_cache_update(source_ip, source_mac);
    if (ip[9] == IP_PROTOCOL_ICMP) {
        handle_icmp(packet, length, source_ip, source_mac);
    } else if (ip[9] == IP_PROTOCOL_TCP) {
        handle_tcp(packet, length, source_ip, source_mac);
    } else if (ip[9] == IP_PROTOCOL_UDP) {
        handle_udp(packet, length);
    }
}

static void handle_ipv6(const uint8_t *packet, uint32_t length) {
    if (length < ETH_HEADER_SIZE + IPV6_HEADER_SIZE) return;
    const uint8_t *ip = packet + ETH_HEADER_SIZE;
    if ((ip[0] >> 4) != 6) return;
    uint32_t payload_length = read_be16(ip + 4);
    if (payload_length + IPV6_HEADER_SIZE > length - ETH_HEADER_SIZE) return;
    const uint8_t *source = ip + 8;
    const uint8_t *destination = ip + 24;
    bool for_us = memcmp(destination, local_ipv6, 16) == 0;
    bool multicast = destination[0] == 0xFF;
    if (!for_us && !multicast) return;
    const uint8_t *source_mac = packet + 6;
    if (ip[6] == IP_PROTOCOL_ICMPV6) {
        handle_icmpv6(packet, length, source, source_mac);
    } else if (ip[6] == IP_PROTOCOL_TCP && for_us) {
        handle_client_tcp6(packet, length, source);
    }
}

static void handle_packet(const uint8_t *packet, uint32_t length) {
    if (length < ETH_HEADER_SIZE) {
        return;
    }
    bool for_us = memcmp(packet, local_mac, 6) == 0;
    bool broadcast = true;
    for (uint32_t index = 0; index < 6; index++) {
        if (packet[index] != 0xFF) {
            broadcast = false;
            break;
        }
    }
    bool ipv6_multicast = packet[0] == 0x33 && packet[1] == 0x33;
    if (!for_us && !broadcast && !ipv6_multicast) {
        return;
    }
    uint16_t type = read_be16(packet + 12);
    if (type == ETHERTYPE_ARP) {
        handle_arp(packet, length);
    } else if (type == ETHERTYPE_IPV4) {
        handle_ipv4(packet, length);
    } else if (type == ETHERTYPE_IPV6) {
        handle_ipv6(packet, length);
    }
}

static void receive_packets(void) {
    for (uint32_t processed = 0; available && processed < 64; processed++) {
        uint16_t current = inw(io_base + RTL_CBR);
        if (rx_offset == current) {
            break;
        }
        uint8_t *header = &rx_buffer[rx_offset];
        uint16_t status = (uint16_t)header[0] | (uint16_t)header[1] << 8;
        uint16_t packet_length = (uint16_t)header[2] | (uint16_t)header[3] << 8;
        if (packet_length < 4 || packet_length > 1600) {
            rx_offset = current;
            rtl_write16(RTL_CAPR, (uint16_t)(rx_offset - 16));
            break;
        }
        uint32_t data_length = packet_length - 4;
        uint8_t local_packet[1600];
        for (uint32_t index = 0; index < data_length; index++) {
            local_packet[index] = rx_buffer[(rx_offset + 4 + index) & (RTL_RX_SIZE - 1)];
        }
        if (status & 1) {
            packets_received++;
            handle_packet(local_packet, data_length);
        }
        rx_offset = (rx_offset + packet_length + 4 + 3) & ~3U;
        if (rx_offset >= RTL_RX_SIZE) {
            rx_offset -= RTL_RX_SIZE;
        }
        rtl_write16(RTL_CAPR, (uint16_t)(rx_offset - 16));
    }
}

void network_init(void) {
    available = false;
    local_ip = 0x0A00020F;
    gateway_ip = 0x0A000202;
    packets_received = 0;
    packets_sent = 0;
    tx_errors = 0;
    last_tx_status = 0;
    rx_offset = 0;
    tx_round_robin = 0;
    connection_state = TCP_STATE_CLOSED;
    connection_handler = 0;
    listen_port = 0;
    tcp_data_segments = 0;
    tcp_data_bytes = 0;
    tcp_accepted_segments = 0;
    tcp_last_sequence = 0;
    tcp_write_attempts = 0;
    tcp_write_failures = 0;
    tcp_last_write_state = 0;
    tcp_segment_failure = 0;
    ipv4_send_failure = 0;
    client_handler = 0;
    client_local_port = 0;
    client_port_sequence = (uint16_t)(timer_ticks() & 0x3FFF);
    ipv6_neighbor_valid = false;
    udp_receive_port = 0;
    udp_handler = 0;
    memset(streams, 0, sizeof(streams));
    memset(stream_listeners, 0, sizeof(stream_listeners));
    stream_generation = 1;
    stream_port_sequence = (uint16_t)(40000 + (timer_ticks() & 0x3FFF));
    client_reset();
    tcp_reset_connection(TCP_STATE_CLOSED);
    rx_buffer = 0;

    if (!find_rtl8139()) {
        return;
    }
    uint8_t *rx_storage = (uint8_t *)kmalloc(RTL_RX_BUFFER_SIZE + RTL_RX_SIZE);
    if (!rx_storage) {
        return;
    }
    rx_buffer = (uint8_t *)(((uintptr_t)rx_storage + RTL_RX_SIZE - 1)
                          & ~(uintptr_t)(RTL_RX_SIZE - 1));
    rtl_write8(RTL_CONFIG1, 0x00);
    rtl_write8(RTL_CR, 0x10);
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        if (!(rtl_read8(RTL_CR) & 0x10)) {
            break;
        }
        cpu_pause();
    }
    rtl_write8(0x5B, 'R');
    for (uint32_t index = 0; index < 6; index++) {
        local_mac[index] = inb(io_base + RTL_IDR0 + index);
    }
    rtl_write32(RTL_RBSTART, (uint32_t)(uintptr_t)rx_buffer);
    for (uint32_t index = 0; index < 4; index++) {
        rtl_write32(RTL_TSD0 + index * 4, 0);
    }
    rtl_write16(0x3E, 0xFFFF);
    rtl_write16(RTL_IMR, 0x0005);
    rtl_write32(RTL_RCR, 0x0000100F);
    rx_offset = inw(io_base + RTL_CBR);
    rtl_write16(RTL_CAPR, (uint16_t)(rx_offset - 16));
    rtl_write8(RTL_CR, 0x0C);
    memset(arp_cache, 0, sizeof(arp_cache));
    available = true;
    send_arp_request(gateway_ip);
}

void network_poll(void) {
    if (available) {
        receive_packets();
        for (int index = 0; index < STREAM_LIMIT; index++) {
            tcp_stream *stream = &streams[index];
            if (stream->used && stream->state == TCP_STATE_SYN_SENT
                && stream->deadline
                && (int32_t)(timer_ticks() - stream->deadline) >= 0) {
                stream_release(index, true);
            }
        }
    }
}

bool network_arp_resolve(uint32_t address, uint8_t *mac, uint32_t timeout_ms) {
    if (arp_cache_lookup(address, mac)) {
        return true;
    }
    if (!available) {
        return false;
    }
    send_arp_request(address);
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (arp_cache_lookup(address, mac)) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

bool network_ping(uint32_t address, uint32_t timeout_ms) {
    uint8_t mac[6];
    if (!network_arp_resolve(address, mac, timeout_ms)) {
        return false;
    }
    uint8_t request[8];
    memset(request, 0, sizeof(request));
    request[0] = 8;
    write_be16(request + 4, 0x434B);
    write_be16(request + 6, 1);
    write_be16(request + 2, internet_checksum(request, sizeof(request)));
    ping_replied = false;
    if (!send_ipv4(mac, address, IP_PROTOCOL_ICMP, request, sizeof(request))) {
        return false;
    }
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (ping_replied) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

bool network_available(void) {
    return available;
}

uint32_t network_ip_address(void) {
    return local_ip;
}

uint32_t network_gateway_address(void) {
    return gateway_ip;
}

uint32_t network_packets_received(void) {
    return packets_received;
}

uint32_t network_packets_sent(void) {
    return packets_sent;
}

uint32_t network_tx_errors(void) {
    return tx_errors;
}

uint32_t network_last_tx_status(void) {
    return last_tx_status;
}

uint16_t network_io_port(void) {
    return io_base;
}

uint16_t network_rx_cursor(void) {
    return available ? inw(io_base + RTL_CBR) : 0;
}

bool network_tcp_listen(uint16_t port, tcp_event_handler handler) {
    if (!available || !port) {
        return false;
    }
    listen_port = port;
    connection_handler = handler;
    tcp_reset_connection(TCP_STATE_LISTEN);
    return true;
}

bool network_tcp_write(const uint8_t *data, uint32_t length) {
    tcp_write_attempts++;
    if (connection_state != TCP_STATE_ESTABLISHED
        && connection_state != TCP_STATE_CLOSE_WAIT) {
        tcp_last_write_state = (uint32_t)connection_state;
        tcp_write_failures++;
        return false;
    }
    uint32_t sent = 0;
    while (sent < length) {
        uint32_t chunk = length - sent;
        if (chunk > TCP_SEND_CHUNK) {
            chunk = TCP_SEND_CHUNK;
        }
        if (!tcp_send_segment(TCP_ACK | TCP_PSH, data + sent, chunk)) {
            tcp_last_write_state = 0x100;
            tcp_write_failures++;
            return false;
        }
        sent += chunk;
    }
    return true;
}

void network_tcp_close(void) {
    if (connection_state == TCP_STATE_ESTABLISHED
        || connection_state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_segment(TCP_FIN | TCP_ACK, 0, 0);
    }
    tcp_reset_connection(TCP_STATE_LISTEN);
}

bool network_tcp_established(void) {
    return connection_state == TCP_STATE_ESTABLISHED;
}

static uint16_t next_stream_port(void) {
    for (uint32_t attempt = 0; attempt < 20000; attempt++) {
        uint16_t port = stream_port_sequence++;
        if (port < 40000) {
            stream_port_sequence = 40000;
            port = 40000;
        }
        bool used = false;
        for (int index = 0; index < STREAM_LIMIT; index++) {
            if (streams[index].used && streams[index].local_port == port) {
                used = true;
                break;
            }
        }
        for (int index = 0; !used && index < STREAM_LISTENER_LIMIT; index++) {
            if (stream_listeners[index].used
                && stream_listeners[index].port == port) used = true;
        }
        if (!used) return port;
    }
    return 0;
}

int network_stream_begin_open(uint32_t address, uint16_t port,
                              tcp_stream_handler handler, void *context,
                              uint32_t timeout_ms) {
    if (!available || !address || !port) return -1;
    int slot = -1;
    for (int index = 0; index < STREAM_LIMIT; index++) {
        if (!streams[index].used) {
            slot = index;
            break;
        }
    }
    if (slot < 0) return -2;
    tcp_stream *stream = &streams[slot];
    memset(stream, 0, sizeof(*stream));
    uint16_t local_port = next_stream_port();
    if (!local_port) return -2;
    stream->used = true;
    stream->generation = stream_generation++;
    if (!stream_generation) stream_generation = 1;
    stream->state = TCP_STATE_SYN_SENT;
    stream->local_port = local_port;
    stream->remote_port = port;
    stream->remote_ip = address;
    stream->deadline = timer_ticks() + timeout_ms / 10;
    stream->handler = handler;
    stream->context = context;
    if (!network_arp_resolve(address, stream->remote_mac, timeout_ms)) {
        stream_release(slot, false);
        return -3;
    }
    stream->send_next = tcp_initial_sequence();
    if (!stream_send_segment(slot, TCP_SYN, 0, 0)) {
        stream_release(slot, false);
        return -4;
    }
    return slot;
}

int network_stream_open(uint32_t address, uint16_t port,
                        tcp_stream_handler handler, void *context,
                        uint32_t timeout_ms) {
    int slot = network_stream_begin_open(address, port, handler, context,
                                         timeout_ms);
    if (slot < 0) return slot;
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (!streams[slot].used) return -5;
        if (streams[slot].state == TCP_STATE_ESTABLISHED) return slot;
        cpu_pause();
    }
    stream_release(slot, true);
    return -5;
}

int network_stream_listen(uint16_t port, tcp_stream_handler handler,
                          void *context) {
    if (!available || !handler) return -1;
    int slot = -1;
    for (int index = 0; index < STREAM_LISTENER_LIMIT; index++) {
        if (!stream_listeners[index].used) {
            slot = index;
            break;
        }
    }
    if (slot < 0) return -2;
    if (!port) {
        port = next_stream_port();
        if (!port) return -3;
    }
    for (int index = 0; index < STREAM_LISTENER_LIMIT; index++) {
        if (stream_listeners[index].used
            && stream_listeners[index].port == port) return -4;
    }
    stream_listeners[slot].used = true;
    stream_listeners[slot].port = port;
    stream_listeners[slot].handler = handler;
    stream_listeners[slot].context = context;
    return slot;
}

bool network_stream_cancel_listen(int listener) {
    if (listener < 0 || listener >= STREAM_LISTENER_LIMIT
        || !stream_listeners[listener].used) return false;
    memset(&stream_listeners[listener], 0, sizeof(stream_listeners[listener]));
    return true;
}

bool network_stream_listener_valid(int listener) {
    return listener >= 0 && listener < STREAM_LISTENER_LIMIT
        && stream_listeners[listener].used;
}

uint16_t network_stream_listener_port(int listener) {
    if (!network_stream_listener_valid(listener)) return 0;
    return stream_listeners[listener].port;
}

bool network_stream_write(int id, const uint8_t *data, uint32_t length) {
    if (id < 0 || id >= STREAM_LIMIT || !streams[id].used
        || (streams[id].state != TCP_STATE_ESTABLISHED
            && streams[id].state != TCP_STATE_CLOSE_WAIT)) return false;
    uint32_t sent = 0;
    while (sent < length) {
        uint32_t chunk = length - sent;
        if (chunk > TCP_SEND_CHUNK) chunk = TCP_SEND_CHUNK;
        if (!stream_send_segment(id, TCP_ACK | TCP_PSH, data + sent, chunk)) {
            return false;
        }
        sent += chunk;
    }
    return true;
}

void network_stream_close(int id) {
    if (id < 0 || id >= STREAM_LIMIT || !streams[id].used) return;
    if (streams[id].state == TCP_STATE_ESTABLISHED
        || streams[id].state == TCP_STATE_CLOSE_WAIT) {
        stream_send_segment(id, TCP_FIN | TCP_ACK, 0, 0);
    }
    stream_release(id, false);
}

bool network_stream_valid(int id) {
    return id >= 0 && id < STREAM_LIMIT && streams[id].used;
}

bool network_tcp_connect(uint32_t address, uint16_t port,
                         tcp_event_handler handler, uint32_t timeout_ms) {
    if (!available || !address || !port) return false;
    client_reset();
    client_handler = handler;
    client_remote_ip = address;
    client_remote_port = port;
    client_local_port = (uint16_t)(49152
                                 + (client_port_sequence++ & 0x3FFF));
    if (!network_arp_resolve(address, client_remote_mac, timeout_ms)) {
        client_reset();
        return false;
    }
    client_send_next = tcp_initial_sequence();
    client_receive_next = 0;
    client_state = TCP_STATE_SYN_SENT;
    if (!client_send_segment(TCP_SYN, 0, 0)) {
        client_reset();
        return false;
    }
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (client_state == TCP_STATE_ESTABLISHED) return true;
        if (client_state == TCP_STATE_CLOSED) return false;
        cpu_pause();
    }
    client_reset();
    return false;
}

bool network_tcp_connect_ipv6(const uint8_t address[16], uint16_t port,
                              tcp_event_handler handler, uint32_t timeout_ms) {
    if (!available || !address || !port) return false;
    client_reset();
    client_handler = handler;
    memcpy(client_remote_ipv6, address, 16);
    client_ipv6 = true;
    client_remote_port = port;
    client_local_port = (uint16_t)(49152
                                 + (client_port_sequence++ & 0x3FFF));
    if (!resolve_ipv6_neighbor(address, client_remote_mac, timeout_ms)) {
        static const uint8_t qemu_host_mac[6] = {
            0x52, 0x55, 0x0A, 0x00, 0x02, 0x02
        };
        memcpy(client_remote_mac, qemu_host_mac, 6);
    }
    client_send_next = tcp_initial_sequence();
    client_receive_next = 0;
    client_state = TCP_STATE_SYN_SENT;
    if (!client_send_segment6(TCP_SYN, 0, 0)) {
        client_reset();
        return false;
    }
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (client_state == TCP_STATE_ESTABLISHED) return true;
        if (client_state == TCP_STATE_CLOSED) return false;
        cpu_pause();
    }
    client_reset();
    return false;
}

bool network_tcp_client_write(const uint8_t *data, uint32_t length) {
    if (client_state != TCP_STATE_ESTABLISHED) return false;
    uint32_t sent = 0;
    while (sent < length) {
        uint32_t chunk = length - sent;
        if (chunk > TCP_SEND_CHUNK) chunk = TCP_SEND_CHUNK;
        bool written = client_ipv6
                     ? client_send_segment6(TCP_ACK | TCP_PSH, data + sent, chunk)
                     : client_send_segment(TCP_ACK | TCP_PSH, data + sent, chunk);
        if (!written) {
            return false;
        }
        sent += chunk;
    }
    return true;
}

void network_tcp_client_close(void) {
    if (client_state == TCP_STATE_ESTABLISHED) {
        if (client_ipv6) client_send_segment6(TCP_FIN | TCP_ACK, 0, 0);
        else client_send_segment(TCP_FIN | TCP_ACK, 0, 0);
    }
    client_reset();
}

bool network_tcp_client_established(void) {
    return client_state == TCP_STATE_ESTABLISHED;
}

void network_tcp_client_set_handler(tcp_event_handler handler) {
    client_handler = handler;
}

bool network_udp_send(uint32_t address, uint16_t source_port,
                      uint16_t destination_port, const uint8_t *data,
                      uint32_t length) {
    (void)source_port;
    return available && send_udp(address, source_port, destination_port,
                                 data, length);
}

void network_udp_set_receiver(uint16_t port, tcp_event_handler handler) {
    udp_receive_port = port;
    udp_handler = handler;
}

uint16_t network_tcp_local_port(void) {
    return listen_port;
}

uint16_t network_tcp_remote_port(void) {
    return remote_port;
}

uint32_t network_tcp_data_segments(void) {
    return tcp_data_segments;
}

uint32_t network_tcp_data_bytes(void) {
    return tcp_data_bytes;
}

uint32_t network_tcp_accepted_segments(void) {
    return tcp_accepted_segments;
}

uint32_t network_tcp_last_sequence(void) {
    return tcp_last_sequence;
}

uint32_t network_tcp_expected_sequence(void) {
    return receive_next;
}

uint32_t network_tcp_write_attempts(void) {
    return tcp_write_attempts;
}

uint32_t network_tcp_write_failures(void) {
    return tcp_write_failures;
}

uint32_t network_tcp_last_write_state(void) {
    return tcp_last_write_state;
}

uint32_t network_tcp_segment_failure(void) {
    return tcp_segment_failure;
}

uint32_t network_ipv4_send_failure(void) {
    return ipv4_send_failure;
}

uint16_t network_interrupt_status(void) {
    return available ? inw(io_base + 0x3E) : 0;
}

uint8_t network_control_register(void) {
    return available ? rtl_read8(RTL_CR) : 0;
}

uint32_t network_receive_config(void) {
    return available ? inl(io_base + RTL_RCR) : 0;
}

uint8_t network_clock_state(void) {
    return available ? inb(io_base + 0x5B) : 0;
}

const uint8_t *network_mac_address(void) {
    return local_mac;
}
