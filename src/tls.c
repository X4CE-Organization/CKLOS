#include "console.h"
#include "fs2.h"
#include "interrupts.h"
#include "io.h"
#include "memory.h"
#include "network.h"
#include "string.h"
#include "tls.h"

#include "bearssl.h"

#define TLS_RX_CAPACITY 32768U
#define TLS_DN_CAPACITY 512U

typedef struct {
    uint8_t data[TLS_RX_CAPACITY];
    uint32_t size;
    bool disconnected;
} tls_socket;

static tls_socket socket_buffer;
static br_ssl_client_context client;
static br_x509_minimal_context x509;
static br_sslio_context io;
static uint8_t io_buffer[BR_SSL_BUFSIZE_BIDI];
static br_x509_decoder_context anchor_decoder;
static uint8_t anchor_dn_data[TLS_DN_CAPACITY];
static uint32_t anchor_dn_length;
static br_x509_trust_anchor trust_anchor;
static bool trust_anchor_ready;

static void socket_handler(uint8_t event, const uint8_t *data, uint32_t length) {
    if (event == TCP_EVENT_DATA) {
        uint32_t available = TLS_RX_CAPACITY - socket_buffer.size;
        if (length > available) length = available;
        memcpy(socket_buffer.data + socket_buffer.size, data, length);
        socket_buffer.size += length;
    } else if (event == TCP_EVENT_DISCONNECTED) {
        socket_buffer.disconnected = true;
    }
}

static int tls_read(void *context, unsigned char *data, size_t length) {
    (void)context;
    uint32_t deadline = timer_ticks() + 10000;
    while (!socket_buffer.size && !socket_buffer.disconnected
           && (int32_t)(timer_ticks() - deadline) < 0) {
        network_poll();
        cpu_pause();
    }
    if (!socket_buffer.size) return socket_buffer.disconnected ? 0 : -1;
    if (length > socket_buffer.size) length = socket_buffer.size;
    memcpy(data, socket_buffer.data, length);
    socket_buffer.size -= (uint32_t)length;
    memmove(socket_buffer.data, socket_buffer.data + length, socket_buffer.size);
    return (int)length;
}

static int tls_write(void *context, const unsigned char *data, size_t length) {
    (void)context;
    return network_tcp_client_write(data, (uint32_t)length) ? (int)length : -1;
}

static void append_dn(void *context, const void *data, size_t length) {
    (void)context;
    if (anchor_dn_length + length > sizeof(anchor_dn_data)) return;
    memcpy(anchor_dn_data + anchor_dn_length, data, length);
    anchor_dn_length += (uint32_t)length;
}

static bool load_trust_anchor(const char *path) {
    if (trust_anchor_ready) return true;
    uint32_t size;
    if (fs2_stat(path, &size, 0) != 0 || size < 64 || size > 65536) return false;
    uint8_t *certificate = (uint8_t *)kmalloc(size);
    if (!certificate) return false;
    uint32_t actual;
    if (fs2_read(path, certificate, size, &actual) != 0 || actual != size) {
        kfree(certificate);
        return false;
    }
    anchor_dn_length = 0;
    br_x509_decoder_init(&anchor_decoder, append_dn, 0);
    br_x509_decoder_push(&anchor_decoder, certificate, size);
    kfree(certificate);
    br_x509_pkey *key = br_x509_decoder_get_pkey(&anchor_decoder);
    if (!key || br_x509_decoder_last_error(&anchor_decoder) != 0) return false;
    trust_anchor.dn.data = anchor_dn_data;
    trust_anchor.dn.len = anchor_dn_length;
    trust_anchor.flags = anchor_decoder.isCA ? BR_X509_TA_CA : 0;
    trust_anchor.pkey = *key;
    trust_anchor_ready = true;
    return true;
}

static bool response_complete(const uint8_t *data, uint32_t size) {
    uint32_t body = size;
    for (uint32_t index = 0; index + 3 < size; index++) {
        if (data[index] == '\r' && data[index + 1] == '\n'
            && data[index + 2] == '\r' && data[index + 3] == '\n') {
            body = index + 4;
            break;
        }
    }
    if (body == size) return false;
    for (uint32_t index = 0; index + 15 < body; index++) {
        if ((data[index] == 'C' || data[index] == 'c')) {
            const char *header = (const char *)data + index;
            if ((header[0] == 'C' || header[0] == 'c')
                && (header[1] == 'o' || header[1] == 'O')
                && (header[2] == 'n' || header[2] == 'N')
                && (header[3] == 't' || header[3] == 'T')
                && (header[4] == 'e' || header[4] == 'E')
                && (header[5] == 'n' || header[5] == 'N')
                && (header[6] == 't' || header[6] == 'T')
                && (header[7] == '-' || header[7] == '_')
                && (header[8] == 'L' || header[8] == 'l')
                && (header[9] == 'e' || header[9] == 'E')
                && (header[10] == 'n' || header[10] == 'N')
                && (header[11] == 'g' || header[11] == 'G')
                && (header[12] == 't' || header[12] == 'T')
                && (header[13] == 'h' || header[13] == 'H')) {
                uint32_t at = index + 14;
                while (at < body && data[at] != ':') at++;
                at++;
                while (at < body && (data[at] == ' ' || data[at] == '\t')) at++;
                uint64_t length = 0;
                while (at < body && data[at] >= '0' && data[at] <= '9') {
                    length = length * 10 + (uint64_t)(data[at++] - '0');
                }
                return body + length <= size;
            }
        }
    }
    return false;
}

bool tls_https_request(const char *host, uint32_t address, uint16_t port,
                       const uint8_t *request, uint32_t request_size,
                       uint8_t *response, uint32_t capacity, uint32_t *size,
                       bool verbose) {
    if (!load_trust_anchor("/etc/ssl/certs/cklos-test-ca.der")) {
        if (verbose) console_write("TLS: trust anchor unavailable.\n");
        return false;
    }
    socket_buffer.size = 0;
    socket_buffer.disconnected = false;
    if (!network_tcp_connect(address, port, socket_handler, 5000)) {
        if (verbose) console_write("TLS: TCP connect failed.\n");
        return false;
    }
    br_ssl_client_init_full(&client, &x509, &trust_anchor, 1);
    br_ssl_engine_set_buffer(&client.eng, io_buffer, sizeof(io_buffer), 1);
    br_x509_minimal_set_time(&x509, 740288, 0);
    if (!br_ssl_client_reset(&client, host, 0)) {
        if (verbose) console_write("TLS: client reset failed.\n");
        network_tcp_client_close();
        return false;
    }
    br_sslio_init(&io, &client.eng, tls_read, 0, tls_write, 0);
    if (br_sslio_write_all(&io, request, request_size) < 0
        || br_sslio_flush(&io) < 0) {
        if (verbose) {
            console_printf("TLS: handshake write failed (%d).\n",
                           br_ssl_engine_last_error(&client.eng));
        }
        network_tcp_client_close();
        return false;
    }
    uint32_t total = 0;
    while (total < capacity) {
        int count = br_sslio_read(&io, response + total, capacity - total);
        if (count < 0) {
            if (verbose) {
                console_printf("TLS: read error %d.\n",
                               br_ssl_engine_last_error(&client.eng));
            }
            break;
        }
        if (count == 0) break;
        total += (uint32_t)count;
        if (response_complete(response, total)) break;
    }
    if (size) *size = total;
    network_tcp_client_close();
    if (verbose) {
        console_printf("TLS handshake/read complete, %u bytes.\n", total);
    }
    return total > 0 && br_ssl_engine_last_error(&client.eng) == 0;
}
