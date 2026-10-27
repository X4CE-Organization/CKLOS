#include "console.h"
#include "fs2.h"
#include "http.h"
#include "interrupts.h"
#include "io.h"
#include "memory.h"
#include "network.h"
#include "resolver.h"
#include "string.h"
#include "tls.h"

#define HTTP_RESPONSE_MAX (4U * 1024U * 1024U)
#define HTTP_REQUEST_MAX 4096U
#define HTTP_URL_MAX 2048U

typedef struct {
    uint8_t *data;
    uint32_t size;
    bool disconnected;
} http_transfer;

static http_transfer transfer;

static void transfer_handler(uint8_t event, const uint8_t *data, uint32_t length) {
    if (event == TCP_EVENT_DATA) {
        uint32_t available = HTTP_RESPONSE_MAX - transfer.size;
        if (length > available) length = available;
        memcpy(transfer.data + transfer.size, data, length);
        transfer.size += length;
    } else if (event == TCP_EVENT_DISCONNECTED) {
        transfer.disconnected = true;
    }
}

static void append_text(char *output, uint32_t capacity, uint32_t *length,
                        const char *text) {
    while (text && *text && *length + 1 < capacity) {
        output[(*length)++] = *text++;
    }
    output[*length] = '\0';
}

static void append_uint(char *output, uint32_t capacity, uint32_t *length,
                        uint32_t value) {
    char number[16];
    uint_to_string(value, number, 10);
    append_text(output, capacity, length, number);
}

static bool parse_url(const char *url, char *host, uint32_t host_size,
                      uint16_t *port, char *path, uint32_t path_size,
                      bool *tls) {
    const char *cursor = url;
    if (strncmp(cursor, "http://", 7) == 0) {
        cursor += 7;
        *port = 80;
        *tls = false;
    } else if (strncmp(cursor, "https://", 8) == 0) {
        cursor += 8;
        *port = 443;
        *tls = true;
    } else {
        return false;
    }
    uint32_t length = 0;
    while (*cursor && *cursor != '/' && *cursor != ':'
           && *cursor != '?' && *cursor != '#') {
        if (length + 1 >= host_size) return false;
        host[length++] = *cursor++;
    }
    host[length] = '\0';
    if (!length) return false;
    if (*cursor == ':') {
        cursor++;
        uint32_t value = 0;
        uint32_t digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (uint32_t)(*cursor++ - '0');
            digits++;
            if (value > 65535) return false;
        }
        if (!digits || !value) return false;
        *port = (uint16_t)value;
    }
    if (*cursor == '\0' || *cursor == '#') {
        strcpy(path, "/");
    } else if (*cursor == '?') {
        if ((uint32_t)strlen(cursor) + 2 > path_size) return false;
        path[0] = '/';
        strcpy(path + 1, cursor);
    } else {
        length = (uint32_t)strlen(cursor);
        if (length + 1 > path_size) return false;
        strcpy(path, cursor);
    }
    return true;
}

static int parse_status(const uint8_t *data, uint32_t size) {
    if (size < 12 || strncmp((const char *)data, "HTTP/1.", 7) != 0
        || data[8] != ' ') return 0;
    const uint8_t *status = data + 9;
    if (status[0] < '0' || status[0] > '9'
        || status[1] < '0' || status[1] > '9'
        || status[2] < '0' || status[2] > '9') return 0;
    return (status[0] - '0') * 100 + (status[1] - '0') * 10 + status[2] - '0';
}

static uint32_t header_end(const uint8_t *data, uint32_t size) {
    for (uint32_t index = 0; index + 3 < size; index++) {
        if (data[index] == '\r' && data[index + 1] == '\n'
            && data[index + 2] == '\r' && data[index + 3] == '\n') {
            return index + 4;
        }
    }
    return size;
}

static char lower_ascii(char value) {
    return value >= 'A' && value <= 'Z' ? (char)(value - 'A' + 'a') : value;
}

static bool header_name_matches(const uint8_t *line, uint32_t length,
                                const char *name) {
    uint32_t index = 0;
    while (name[index]) {
        if (index >= length) return false;
        if (lower_ascii((char)line[index]) != lower_ascii(name[index])) {
            return false;
        }
        index++;
    }
    while (index < length && (line[index] == ' ' || line[index] == '\t')) {
        index++;
    }
    return index < length && line[index] == ':';
}

static bool header_value(const uint8_t *data, uint32_t header_size,
                         const char *name, char *output, uint32_t capacity) {
    uint32_t line = 0;
    while (line < header_size) {
        uint32_t end = line;
        while (end < header_size && data[end] != '\r' && data[end] != '\n') {
            end++;
        }
        if (header_name_matches(data + line, end - line, name)) {
            uint32_t at = line;
            while (at < end && data[at] != ':') at++;
            at++;
            while (at < end && (data[at] == ' ' || data[at] == '\t')) at++;
            uint32_t length = 0;
            while (at < end && length + 1 < capacity) {
                output[length++] = (char)data[at++];
            }
            while (length && (output[length - 1] == ' '
                              || output[length - 1] == '\t')) {
                length--;
            }
            output[length] = '\0';
            return true;
        }
        while (end < header_size && (data[end] == '\r' || data[end] == '\n')) {
            end++;
        }
        if (end <= line) break;
        line = end;
    }
    return false;
}

static bool redirect_status(int status) {
    return status == 301 || status == 302 || status == 303
        || status == 307 || status == 308;
}

static bool resolve_redirect(const char *base, bool tls, const char *host,
                             uint16_t port, const char *location,
                             char *output, uint32_t capacity) {
    if (strncmp(location, "http://", 7) == 0
        || strncmp(location, "https://", 8) == 0) {
        if ((uint32_t)strlen(location) + 1 > capacity) return false;
        strcpy(output, location);
        return true;
    }
    if (location[0] != '/') {
        (void)base;
        return false;
    }
    uint32_t length = 0;
    output[0] = '\0';
    append_text(output, capacity, &length, tls ? "https://" : "http://");
    append_text(output, capacity, &length, host);
    if ((tls && port != 443) || (!tls && port != 80)) {
        append_text(output, capacity, &length, ":");
        append_uint(output, capacity, &length, port);
    }
    append_text(output, capacity, &length, location);
    return length + 1 < capacity;
}

static bool build_request(const http_options *options, bool tls,
                          const char *host, uint16_t port, const char *path,
                          uint32_t resume_from,
                          char *request, uint32_t capacity, uint32_t *request_size) {
    uint32_t length = 0;
    request[0] = '\0';
    const char *method = options->method && *options->method
                       ? options->method : "GET";
    append_text(request, capacity, &length, method);
    append_text(request, capacity, &length, " ");
    append_text(request, capacity, &length, path);
    append_text(request, capacity, &length, " HTTP/1.1\r\nHost: ");
    append_text(request, capacity, &length, host);
    if ((tls && port != 443) || (!tls && port != 80)) {
        append_text(request, capacity, &length, ":");
        append_uint(request, capacity, &length, port);
    }
    append_text(request, capacity, &length, "\r\nUser-Agent: ");
    append_text(request, capacity, &length,
                options->user_agent && *options->user_agent
                    ? options->user_agent : "CKLOS-curl/0.13");
    append_text(request, capacity, &length,
                "\r\nAccept: */*\r\nConnection: close\r\n");
    if (options->referer && *options->referer) {
        append_text(request, capacity, &length, "Referer: ");
        append_text(request, capacity, &length, options->referer);
        append_text(request, capacity, &length, "\r\n");
    }
    if (options->authorization && *options->authorization) {
        append_text(request, capacity, &length, options->authorization);
        append_text(request, capacity, &length, "\r\n");
    }
    if (options->cookie && *options->cookie) {
        append_text(request, capacity, &length, "Cookie: ");
        append_text(request, capacity, &length, options->cookie);
        append_text(request, capacity, &length, "\r\n");
    }
    if (resume_from) {
        append_text(request, capacity, &length, "Range: bytes=");
        append_uint(request, capacity, &length, resume_from);
        append_text(request, capacity, &length, "-\r\n");
    }
    if (options->headers && *options->headers) {
        append_text(request, capacity, &length, options->headers);
        append_text(request, capacity, &length, "\r\n");
    }
    if (options->data) {
        append_text(request, capacity, &length,
                    "Content-Type: application/x-www-form-urlencoded\r\n"
                    "Content-Length: ");
        append_uint(request, capacity, &length, (uint32_t)strlen(options->data));
        append_text(request, capacity, &length, "\r\n");
    }
    append_text(request, capacity, &length, "\r\n");
    if (options->data) {
        append_text(request, capacity, &length, options->data);
    }
    *request_size = length;
    return length + 1 < capacity;
}

bool http_perform(const http_options *options, http_result *result) {
    if (result) memset(result, 0, sizeof(*result));
    if (!options || !options->url) return false;

    char current_url[HTTP_URL_MAX];
    if ((uint32_t)strlen(options->url) + 1 > sizeof(current_url)) return false;
    strcpy(current_url, options->url);
    http_options current = *options;
    current.url = current_url;
    uint32_t redirects = 0;
    uint32_t max_redirects = options->max_redirects ? options->max_redirects : 5;

    for (;;) {
        char host[256];
        char path[1024];
        uint16_t port;
        bool tls;
        if (!parse_url(current.url, host, sizeof(host), &port, path,
                       sizeof(path), &tls)) {
            if (result) strcpy(result->error, "invalid URL");
            return false;
        }
        uint32_t address;
        if (!resolver_lookup_ipv4(host, &address, 4000)) {
            if (result) strcpy(result->error, "host resolution failed");
            return false;
        }
        if (options->verbose) {
            console_write("Connecting to ");
            console_write(host);
            console_write("...\n");
        }
        transfer.data = (uint8_t *)kmalloc(HTTP_RESPONSE_MAX);
        if (!transfer.data) {
            if (result) strcpy(result->error, "response buffer unavailable");
            return false;
        }
        transfer.size = 0;
        transfer.disconnected = false;

        char request[HTTP_REQUEST_MAX];
        uint32_t request_size;
        uint32_t resume_from = options->resume ? options->resume_from : 0;
        if (!build_request(&current, tls, host, port, path, resume_from, request,
                           sizeof(request), &request_size)) {
            kfree(transfer.data);
            transfer.data = 0;
            if (result) strcpy(result->error, "request too large");
            return false;
        }
        bool transferred;
        if (tls) {
            uint32_t size = 0;
            transferred = tls_https_request(host, address, port,
                                            (const uint8_t *)request, request_size,
                                            transfer.data, HTTP_RESPONSE_MAX,
                                            &size, options->verbose);
            transfer.size = size;
        } else {
            transferred = network_tcp_connect(
                address, port, transfer_handler,
                options->connect_timeout_ms ? options->connect_timeout_ms : 5000);
            if (transferred) {
                transferred = network_tcp_client_write(
                    (const uint8_t *)request, request_size);
            }
            uint32_t timeout = options->transfer_timeout_ms
                             ? options->transfer_timeout_ms : 10000;
            uint32_t deadline = timer_ticks() + timeout / 10;
            if (transferred) {
                while ((int32_t)(timer_ticks() - deadline) < 0) {
                    network_poll();
                    if (transfer.disconnected
                        || !network_tcp_client_established()) break;
                    cpu_pause();
                }
            }
            network_tcp_client_close();
        }
        if (!transferred) {
            kfree(transfer.data);
            transfer.data = 0;
            if (result && !result->error[0]) {
                strcpy(result->error, tls ? "TLS transfer failed"
                                          : "TCP transfer failed");
            }
            return false;
        }

        int status = parse_status(transfer.data, transfer.size);
        uint32_t headers = header_end(transfer.data, transfer.size);
        uint32_t body_size = transfer.size >= headers
                           ? transfer.size - headers : 0;
        if (result) {
            result->status = status;
            result->bytes = body_size;
            result->header_bytes = headers;
            result->redirects = redirects;
            header_value(transfer.data, headers, "Content-Type",
                         result->content_type, sizeof(result->content_type));
        }
        char location[512];
        location[0] = '\0';
        header_value(transfer.data, headers, "Location",
                     location, sizeof(location));

        if (options->follow_redirects && redirect_status(status) && location[0]) {
            char next_url[HTTP_URL_MAX];
            if (redirects >= max_redirects
                || !resolve_redirect(current_url, tls, host, port, location,
                                     next_url, sizeof(next_url))) {
                kfree(transfer.data);
                transfer.data = 0;
                if (result) strcpy(result->error, "redirect limit or invalid location");
                return false;
            }
            if (result) {
                strcpy(result->location, location);
                result->redirects = redirects + 1;
            }
            strcpy(current_url, next_url);
            current.url = current_url;
            redirects++;
            if (status == 301 || status == 302 || status == 303) {
                current.method = "GET";
                current.data = 0;
            }
            kfree(transfer.data);
            transfer.data = 0;
            continue;
        }

        bool successful = status >= 200 && status < 300;
        bool write_body = successful;
        bool saved = false;
        uint32_t output_start = options->include_headers ? 0 : headers;
        uint32_t output_size = options->include_headers
                             ? transfer.size : body_size;
        bool append = options->resume && status == 206;
        if (write_body && options->output_path && *options->output_path) {
            saved = fs2_write(options->output_path,
                              transfer.data + output_start, output_size,
                              append) == 0;
        } else if (write_body && !options->output_path) {
            for (uint32_t index = 0; index < output_size; index++) {
                console_putc((char)transfer.data[output_start + index]);
            }
        }
        if (options->verbose || (options->fail_on_http_error && !successful)) {
            console_printf("HTTP %d  %u bytes", status, body_size);
            if (options->follow_redirects && redirects) {
                console_printf("  after %u redirect(s)", redirects);
            }
            if (saved) {
                console_write(" -> ");
                console_write(options->output_path);
            }
            console_putc('\n');
        }
        kfree(transfer.data);
        transfer.data = 0;
        if (options->fail_on_http_error && !successful) return false;
        if (options->output_path && *options->output_path) return saved;
        return true;
    }
}
