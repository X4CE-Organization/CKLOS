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

#define RTL_RX_SIZE 8192
#define RTL_RX_BUFFER_SIZE (RTL_RX_SIZE + 16)
#define RTL_TX_BUFFER_SIZE 2048

#define ETH_HEADER_SIZE 14
#define ARP_PACKET_SIZE 28
#define IPV4_HEADER_SIZE 20
#define ICMP_HEADER_SIZE 8

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6

#define ARP_CACHE_SIZE 8
#define TCP_RX_WINDOW 4096
#define TCP_SEND_CHUNK 1200

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef enum {
    TCP_STATE_CLOSED,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_CLOSE_WAIT
} tcp_state;

typedef struct {
    uint32_t address;
    uint8_t mac[6];
    bool valid;
} arp_entry;

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

static uint8_t rx_buffer[RTL_RX_BUFFER_SIZE] __attribute__((aligned(8192)));
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
    if (!for_us && !broadcast) {
        return;
    }
    uint16_t type = read_be16(packet + 12);
    if (type == ETHERTYPE_ARP) {
        handle_arp(packet, length);
    } else if (type == ETHERTYPE_IPV4) {
        handle_ipv4(packet, length);
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
    tcp_reset_connection(TCP_STATE_CLOSED);

    if (!find_rtl8139()) {
        return;
    }
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
    rtl_write32(RTL_RCR, 0x0000008F);
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
