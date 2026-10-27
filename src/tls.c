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
#define TLS_DN_CAPACITY 65536U
#define TLS_ANCHOR_MAX 192U

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
static uint8_t *anchor_dn_data;
static uint8_t *anchor_key_data;
static uint32_t anchor_dn_length;
static br_x509_trust_anchor *trust_anchors;
static uint32_t trust_anchor_count;
static bool trust_store_ready;

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
    if (anchor_dn_length + length > TLS_DN_CAPACITY) return;
    memcpy(anchor_dn_data + anchor_dn_length, data, length);
    anchor_dn_length += (uint32_t)length;
}

static bool add_trust_anchor(uint8_t *certificate, uint32_t size) {
    if (trust_anchor_count >= TLS_ANCHOR_MAX || size < 64
        || !anchor_dn_data || !anchor_key_data || !trust_anchors) return false;
    uint32_t dn_start = anchor_dn_length;
    br_x509_decoder_init(&anchor_decoder, append_dn, 0);
    br_x509_decoder_push(&anchor_decoder, certificate, size);
    br_x509_pkey *key = br_x509_decoder_get_pkey(&anchor_decoder);
    if (!key || br_x509_decoder_last_error(&anchor_decoder) != 0) return false;
    uint32_t anchor_index = trust_anchor_count++;
    br_x509_trust_anchor *anchor = &trust_anchors[anchor_index];
    anchor->dn.data = anchor_dn_data + dn_start;
    anchor->dn.len = anchor_dn_length - dn_start;
    anchor->flags = anchor_decoder.isCA ? BR_X509_TA_CA : 0;
    anchor->pkey = *key;
    uint8_t *key_data = anchor_key_data
                      + anchor_index * BR_X509_BUFSIZE_KEY;
    memcpy(key_data, anchor_decoder.pkey_data, BR_X509_BUFSIZE_KEY);
    if ((key->key_type & 0x0F) == BR_KEYTYPE_RSA) {
        anchor->pkey.key.rsa.n = key_data;
        anchor->pkey.key.rsa.e = key_data + key->key.rsa.nlen;
    } else if ((key->key_type & 0x0F) == BR_KEYTYPE_EC) {
        anchor->pkey.key.ec.q = key_data;
    }
    return true;
}

static bool load_der_file(const char *path) {
    uint32_t size;
    if (fs2_stat(path, &size, 0) != 0 || size < 64 || size > 65536) {
        return false;
    }
    uint8_t *certificate = (uint8_t *)kmalloc(size);
    if (!certificate) return false;
    uint32_t actual;
    if (fs2_read(path, certificate, size, &actual) != 0 || actual != size
        || !add_trust_anchor(certificate, size)) {
        kfree(certificate);
        return false;
    }
    return true;
}

static int base64_value(char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

static uint32_t decode_base64(const char *input, uint32_t length,
                              uint8_t *output, uint32_t capacity) {
    uint32_t values[4];
    uint32_t used = 0;
    uint32_t written = 0;
    for (uint32_t index = 0; index <= length; index++) {
        char character = index < length ? input[index] : '=';
        if (character == '\r' || character == '\n'
            || character == ' ' || character == '\t') continue;
        int value;
        if (character == '=') value = 64;
        else value = base64_value(character);
        if (value < 0) return 0;
        values[used++] = (uint32_t)value;
        if (used != 4) continue;
        if (written >= capacity) return 0;
        output[written++] = (uint8_t)(values[0] << 2 | values[1] >> 4);
        if (values[2] != 64) {
            if (written >= capacity) return 0;
            output[written++] = (uint8_t)(values[1] << 4 | values[2] >> 2);
        }
        if (values[3] != 64) {
            if (written >= capacity) return 0;
            output[written++] = (uint8_t)(values[2] << 6 | values[3]);
        }
        used = 0;
        if (character == '=') break;
    }
    return written;
}

static bool load_pem_bundle(const char *path) {
    uint32_t size;
    if (fs2_stat(path, &size, 0) != 0 || size < 64 || size > 512 * 1024U) {
        return false;
    }
    char *pem = (char *)kmalloc(size + 1);
    uint8_t *der = (uint8_t *)kmalloc(size + 1);
    if (!pem || !der) {
        kfree(pem);
        kfree(der);
        return false;
    }
    uint32_t actual;
    if (fs2_read(path, pem, size, &actual) != 0 || actual != size) {
        kfree(pem);
        kfree(der);
        return false;
    }
    pem[size] = '\0';
    const char *begin_marker = "-----BEGIN CERTIFICATE-----";
    const char *end_marker = "-----END CERTIFICATE-----";
    char *cursor = pem;
    uint32_t found = 0;
    uint32_t der_offset = 0;
    while (cursor && *cursor) {
        char *begin = strstr(cursor, begin_marker);
        if (!begin) break;
        begin += strlen(begin_marker);
        char *end = strstr(begin, end_marker);
        if (!end) break;
        uint32_t encoded_length = (uint32_t)(end - begin);
        uint32_t der_length = decode_base64(begin, encoded_length,
                                            der + der_offset,
                                            size - der_offset);
        if (der_length && add_trust_anchor(der + der_offset, der_length)) {
            found++;
            der_offset += der_length;
        }
        cursor = end + strlen(end_marker);
    }
    kfree(pem);
    return found != 0;
}

static bool load_trust_store(void) {
    if (trust_store_ready) return trust_anchor_count != 0;
    anchor_dn_data = (uint8_t *)kmalloc(TLS_DN_CAPACITY);
    anchor_key_data = (uint8_t *)kmalloc(
        TLS_ANCHOR_MAX * BR_X509_BUFSIZE_KEY);
    trust_anchors = (br_x509_trust_anchor *)kmalloc(
        sizeof(br_x509_trust_anchor) * TLS_ANCHOR_MAX);
    if (!anchor_dn_data || !anchor_key_data || !trust_anchors) {
        kfree(anchor_dn_data);
        kfree(anchor_key_data);
        kfree(trust_anchors);
        anchor_dn_data = 0;
        anchor_key_data = 0;
        trust_anchors = 0;
        return false;
    }
    load_der_file("/etc/ssl/certs/cklos-test-ca.der");
    load_pem_bundle("/etc/ssl/certs/ca-certificates.crt");
    trust_store_ready = true;
    return trust_anchor_count != 0;
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
    if (!load_trust_store()) {
        if (verbose) console_write("TLS: trust anchor unavailable.\n");
        return false;
    }
    if (verbose) {
        console_printf("TLS: %u trust anchors loaded.\n", trust_anchor_count);
    }
    socket_buffer.size = 0;
    socket_buffer.disconnected = false;
    if (!network_tcp_connect(address, port, socket_handler, 5000)) {
        if (verbose) console_write("TLS: TCP connect failed.\n");
        return false;
    }
    br_ssl_client_init_full(&client, &x509, trust_anchors, trust_anchor_count);
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
