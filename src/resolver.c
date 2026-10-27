#include "fs2.h"
#include "interrupts.h"
#include "io.h"
#include "memory.h"
#include "network.h"
#include "resolver.h"
#include "string.h"

#define DNS_SERVER 0x0A000203U
#define DNS_PORT 53
#define DNS_LOCAL_PORT 53000
#define DNS_PACKET_MAX 1024

static uint8_t dns_response[DNS_PACKET_MAX];
static uint32_t dns_response_size;
static uint16_t dns_response_id;

static uint16_t read_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static void write_be16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

bool resolver_parse_ipv4(const char *text, uint32_t *address) {
    uint32_t parts[4] = {0, 0, 0, 0};
    uint32_t index = 0;
    bool digit = false;
    for (const char *cursor = text; ; cursor++) {
        if (*cursor >= '0' && *cursor <= '9') {
            parts[index] = parts[index] * 10 + (uint32_t)(*cursor - '0');
            if (parts[index] > 255) return false;
            digit = true;
        } else if (*cursor == '.' || *cursor == '\0') {
            if (!digit || index >= 4) return false;
            if (*cursor == '\0') break;
            index++;
            digit = false;
        } else {
            return false;
        }
    }
    if (index != 3) return false;
    *address = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
    return true;
}

static bool hosts_lookup(const char *host, uint32_t *address) {
    uint32_t size;
    if (fs2_stat("/etc/hosts", &size, 0) != 0 || !size || size > 65536) {
        return false;
    }
    char *data = (char *)kmalloc(size + 1);
    if (!data) return false;
    uint32_t actual;
    if (fs2_read("/etc/hosts", data, size, &actual) != 0) {
        kfree(data);
        return false;
    }
    data[actual] = '\0';
    char *line = data;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char saved = *end;
        *end = '\0';
        while (*line == ' ' || *line == '\t') line++;
        char *ip = line;
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) *line++ = '\0';
        while (*line == ' ' || *line == '\t') line++;
        char *name = line;
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) *line = '\0';
        if (strcmp(name, host) == 0 && resolver_parse_ipv4(ip, address)) {
            kfree(data);
            return true;
        }
        if (!saved) break;
        line = end + 1;
    }
    kfree(data);
    return false;
}

static void dns_callback(uint8_t event, const uint8_t *data, uint32_t length) {
    if (event != TCP_EVENT_DATA || length > sizeof(dns_response)) return;
    memcpy(dns_response, data, length);
    dns_response_size = length;
}

static bool send_query(const char *host, uint16_t type, uint16_t id) {
    uint8_t query[512];
    memset(query, 0, sizeof(query));
    write_be16(query + 0, id);
    write_be16(query + 2, 0x0100);
    write_be16(query + 4, 1);
    uint32_t offset = 12;
    const char *label = host;
    while (*label) {
        const char *end = label;
        while (*end && *end != '.') end++;
        uint32_t length = (uint32_t)(end - label);
        if (!length || length > 63 || offset + length + 6 >= sizeof(query)) {
            return false;
        }
        query[offset++] = (uint8_t)length;
        memcpy(query + offset, label, length);
        offset += length;
        if (!*end) break;
        label = end + 1;
    }
    query[offset++] = 0;
    write_be16(query + offset, type);
    write_be16(query + offset + 2, 1);
    offset += 4;
    dns_response_size = 0;
    dns_response_id = id;
    network_udp_set_receiver(DNS_LOCAL_PORT, dns_callback);
    return network_udp_send(DNS_SERVER, DNS_LOCAL_PORT, DNS_PORT, query, offset);
}

static bool parse_response(uint16_t type, uint8_t *answer, uint32_t answer_size) {
    if (dns_response_size < 12 || read_be16(dns_response) != dns_response_id) {
        return false;
    }
    uint16_t questions = read_be16(dns_response + 4);
    uint16_t answers = read_be16(dns_response + 6);
    uint32_t offset = 12;
    for (uint16_t index = 0; index < questions; index++) {
        while (offset < dns_response_size) {
            uint8_t length = dns_response[offset++];
            if (!length) break;
            if ((length & 0xC0) == 0xC0) {
                offset++;
                break;
            }
            offset += length;
        }
        offset += 4;
    }
    for (uint16_t index = 0; index < answers && offset + 12 <= dns_response_size;
         index++) {
        if ((dns_response[offset] & 0xC0) == 0xC0) offset += 2;
        else {
            while (offset < dns_response_size && dns_response[offset]) {
                offset += dns_response[offset] + 1;
            }
            offset++;
        }
        if (offset + 10 > dns_response_size) return false;
        uint16_t answer_type = read_be16(dns_response + offset);
        uint16_t data_length = read_be16(dns_response + offset + 8);
        offset += 10;
        if (offset + data_length > dns_response_size) return false;
        if (answer_type == type
            && data_length == (type == 1 ? 4U : 16U)
            && answer_size >= data_length) {
            memcpy(answer, dns_response + offset, data_length);
            return true;
        }
        offset += data_length;
    }
    return false;
}

static bool lookup(const char *host, uint16_t type, uint8_t *answer,
                   uint32_t answer_size, uint32_t timeout_ms) {
    if (!network_available()) return false;
    uint16_t id = (uint16_t)(timer_ticks() ^ (uint32_t)(uintptr_t)host ^ type);
    if (!send_query(host, type, id)) return false;
    uint32_t target = timer_ticks() + timeout_ms / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        if (dns_response_size && parse_response(type, answer, answer_size)) {
            network_udp_set_receiver(0, 0);
            return true;
        }
        cpu_pause();
    }
    network_udp_set_receiver(0, 0);
    return false;
}

bool resolver_lookup_ipv4(const char *host, uint32_t *address, uint32_t timeout_ms) {
    if (resolver_parse_ipv4(host, address)) return true;
    if (hosts_lookup(host, address)) return true;
    uint8_t answer[4];
    if (!lookup(host, 1, answer, sizeof(answer), timeout_ms)) return false;
    *address = (uint32_t)answer[0] << 24 | (uint32_t)answer[1] << 16
             | (uint32_t)answer[2] << 8 | answer[3];
    return true;
}

bool resolver_lookup_ipv6(const char *host, uint8_t address[16], uint32_t timeout_ms) {
    return lookup(host, 28, address, 16, timeout_ms);
}
