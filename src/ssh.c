#include "console.h"
#include "crypto.h"
#include "fs2.h"
#include "interrupts.h"
#include "io.h"
#include "memory.h"
#include "network.h"
#include "resolver.h"
#include "ssh.h"
#include "string.h"
#include "version.h"

#include "keys.h"

#define SSH_SERVER_VERSION "SSH-2.0-" CKLOS_NAME "_" CKLOS_VERSION
#define SSH_MAX_PACKET 65536
#define SSH_INPUT_SIZE 512
#define SFTP_HANDLE_LIMIT 8
#define SFTP_BODY_MAX 60000
#define SSH_CHANNEL_LIMIT 12
#define SSH_CHANNEL_WINDOW (1024U * 1024U)
#define REMOTE_FORWARD_LIMIT 3
#define AGENT_SOCKET_PATH "/tmp/cklos-ssh-agent.sock"
#define SSH_AGENT_RX_CAPACITY 65536
#define SSH_AGENT_OPEN_TIMEOUT 5000
#define SSH_CHANNEL_OUTPUT_CAPACITY 262144

enum {
    SSH_MSG_DISCONNECT = 1,
    SSH_MSG_SERVICE_REQUEST = 5,
    SSH_MSG_SERVICE_ACCEPT = 6,
    SSH_MSG_KEXINIT = 20,
    SSH_MSG_NEWKEYS = 21,
    SSH_MSG_KEX_ECDH_INIT = 30,
    SSH_MSG_KEX_ECDH_REPLY = 31,
    SSH_MSG_USERAUTH_REQUEST = 50,
    SSH_MSG_USERAUTH_FAILURE = 51,
    SSH_MSG_USERAUTH_SUCCESS = 52,
    SSH_MSG_USERAUTH_PK_OK = 60,
    SSH_MSG_GLOBAL_REQUEST = 80,
    SSH_MSG_REQUEST_SUCCESS = 81,
    SSH_MSG_REQUEST_FAILURE = 82,
    SSH_MSG_CHANNEL_OPEN = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST = 93,
    SSH_MSG_CHANNEL_DATA = 94,
    SSH_MSG_CHANNEL_EOF = 96,
    SSH_MSG_CHANNEL_CLOSE = 97,
    SSH_MSG_CHANNEL_REQUEST = 98,
    SSH_MSG_CHANNEL_SUCCESS = 99,
    SSH_MSG_CHANNEL_FAILURE = 100
};

typedef enum {
    SSH_CHANNEL_NONE,
    SSH_CHANNEL_CONSOLE,
    SSH_CHANNEL_SFTP,
    SSH_CHANNEL_DIRECT,
    SSH_CHANNEL_FORWARDED,
    SSH_CHANNEL_AGENT
} ssh_channel_mode;

typedef struct {
    bool used;
    bool directory;
    char path[4096];
    uint32_t offset;
    bool read;
    bool write;
} sftp_handle;

typedef struct {
    const uint8_t *data;
    uint32_t length;
} ssh_string;

typedef enum {
    SSH_PHASE_VERSION,
    SSH_PHASE_KEX,
    SSH_PHASE_SERVICE,
    SSH_PHASE_AUTH,
    SSH_PHASE_CONNECTION,
    SSH_PHASE_FAILED
} ssh_phase;

typedef struct {
    bool used;
    ssh_channel_mode mode;
    uint32_t generation;
    uint32_t client_channel;
    uint32_t server_channel;
    uint32_t remote_window;
    uint32_t local_window;
    int stream;
    bool confirmed;
    bool close_sent;
    bool close_received;
    bool eof_sent;
    bool eof_received;
    bool pending_close;
    int forward_index;
    sftp_handle sftp_handles[SFTP_HANDLE_LIMIT];
    uint8_t *sftp_input;
    uint32_t sftp_input_length;
    uint8_t *pending;
    uint32_t pending_length;
    uint32_t pending_capacity;
    uint8_t *outbound;
    uint32_t outbound_length;
    uint32_t outbound_capacity;
} ssh_channel;

typedef struct {
    bool used;
    int listener;
    char address[64];
    uint16_t requested_port;
    uint16_t port;
} ssh_remote_forward;

static ssh_phase phase;
static char server_version[64] = SSH_SERVER_VERSION;
static char client_version[128];
static uint32_t client_version_length;
static uint8_t *input;
static uint32_t input_length;
static uint8_t *send_payload;
static uint8_t *packet_buffer;
static uint8_t *mac_buffer;
static uint8_t key_exchange_client[2048];
static uint32_t key_exchange_client_length;
static uint8_t key_exchange_server[512];
static uint32_t key_exchange_server_length;
static uint8_t session_id[32];
static uint32_t send_sequence;
static uint32_t receive_sequence;
static bool send_encrypted;
static bool receive_encrypted;
static uint8_t send_key[32];
static uint8_t receive_key[32];
static cklos_aes_ctr send_cipher;
static cklos_aes_ctr receive_cipher;
static bool shell_active;
static uint8_t console_input[SSH_INPUT_SIZE];
static uint32_t console_input_head;
static uint32_t console_input_tail;
static char status_text[48] = "listening";
static bool remote_exec_active;
static char remote_output_tail[8];
static uint32_t remote_output_length;
static ssh_channel *channels;
static ssh_channel *sftp_channel;
static uint32_t channel_generation = 1;
static uint32_t next_server_channel;
static ssh_remote_forward remote_forwards[REMOTE_FORWARD_LIMIT];
static int ssh_control_stream = -1;
static int ssh_control_listener = -1;
static bool processing_ssh_input;
static ssh_channel *agent_channel;
static uint8_t *agent_rx;
static uint32_t agent_rx_length;
static bool agent_rx_eof;

static void send_message(const uint8_t *payload, uint32_t length);
static void write_u32(uint8_t *output, uint32_t value);
static uint32_t put_string(uint8_t *output, const void *data, uint32_t length);
static void send_channel_data(ssh_channel *channel, const uint8_t *data,
                              uint32_t length);
static void channel_close(ssh_channel *channel, bool send_close);
static void ssh_agent_poll(void);
static void ssh_agent_receive(const uint8_t *data, uint32_t length);
static void reset_connection_channels(void);

static void queue_console_bytes(const uint8_t *data, uint32_t length) {
    for (uint32_t index = 0; index < length; index++) {
        uint32_t next = (console_input_head + 1) % SSH_INPUT_SIZE;
        if (next == console_input_tail) break;
        console_input[console_input_head] = data[index];
        console_input_head = next;
    }
}

static ssh_channel *channel_lookup_server(uint32_t server_id) {
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        if (channels[index].used
            && channels[index].server_channel == server_id) {
            return &channels[index];
        }
    }
    return 0;
}

static ssh_channel *channel_allocate(uint32_t client_id,
                                     uint32_t remote_window) {
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        ssh_channel *channel = &channels[index];
        if (channel->used) continue;
        memset(channel, 0, sizeof(*channel));
        channel->used = true;
        channel->generation = channel_generation++;
        if (!channel_generation) channel_generation = 1;
        channel->client_channel = client_id;
        channel->server_channel = next_server_channel++;
        if (next_server_channel > 0x7FFFFFFFU) next_server_channel = 0;
        channel->remote_window = remote_window;
        channel->local_window = SSH_CHANNEL_WINDOW;
        channel->stream = -1;
        return channel;
    }
    return 0;
}

static bool channel_valid(const ssh_channel *channel) {
    if (!channel || !channel->used) return false;
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        if (&channels[index] == channel) return true;
    }
    return false;
}

static void channel_free(ssh_channel *channel) {
    if (!channel_valid(channel)) return;
    if (channel->stream >= 0 && network_stream_valid(channel->stream)) {
        network_stream_close(channel->stream);
    }
    if (channel->sftp_input) kfree(channel->sftp_input);
    if (channel->pending) kfree(channel->pending);
    if (channel->outbound) kfree(channel->outbound);
    if (channel->mode == SSH_CHANNEL_CONSOLE) {
        shell_active = false;
        remote_exec_active = false;
    }
    if (channel->mode == SSH_CHANNEL_AGENT && agent_channel == channel) {
        agent_channel = 0;
        agent_rx_length = 0;
        agent_rx_eof = true;
    }
    memset(channel, 0, sizeof(*channel));
}

static void channel_send_eof_close(ssh_channel *channel) {
    if (!channel_valid(channel)) return;
    uint32_t offset;
    if (!channel->eof_sent) {
        offset = 0;
        send_payload[offset++] = SSH_MSG_CHANNEL_EOF;
        write_u32(send_payload + offset, channel->client_channel);
        offset += 4;
        send_message(send_payload, offset);
        channel->eof_sent = true;
    }
    if (!channel->close_sent) {
        offset = 0;
        send_payload[offset++] = SSH_MSG_CHANNEL_CLOSE;
        write_u32(send_payload + offset, channel->client_channel);
        offset += 4;
        send_message(send_payload, offset);
        channel->close_sent = true;
    }
}

static void channel_close(ssh_channel *channel, bool send_close) {
    if (!channel_valid(channel)) return;
    if (send_close) channel_send_eof_close(channel);
    if (channel->stream >= 0 && network_stream_valid(channel->stream)) {
        network_stream_close(channel->stream);
    }
    channel_free(channel);
}

static void close_remote_channel(void) {
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        ssh_channel *channel = &channels[index];
        if (!channel->used || channel->mode != SSH_CHANNEL_CONSOLE) continue;
        uint32_t offset = 0;
        send_payload[offset++] = SSH_MSG_CHANNEL_REQUEST;
        write_u32(send_payload + offset, channel->client_channel);
        offset += 4;
        offset += put_string(send_payload + offset, "exit-status", 11);
        send_payload[offset++] = 0;
        write_u32(send_payload + offset, 0);
        offset += 4;
        send_message(send_payload, offset);
        channel_send_eof_close(channel);
        return;
    }
}

static void write_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void write_u64(uint8_t *output, uint64_t value) {
    write_u32(output, (uint32_t)(value >> 32));
    write_u32(output + 4, (uint32_t)value);
}

static uint32_t read_u32(const uint8_t *input_data) {
    return (uint32_t)input_data[0] << 24 | (uint32_t)input_data[1] << 16
         | (uint32_t)input_data[2] << 8 | input_data[3];
}

static uint64_t read_u64(const uint8_t *input_data) {
    return (uint64_t)read_u32(input_data) << 32 | read_u32(input_data + 4);
}

static bool read_byte(const uint8_t *data, uint32_t length, uint32_t *offset,
                      uint8_t *value) {
    if (*offset >= length) {
        return false;
    }
    *value = data[(*offset)++];
    return true;
}

static bool read_string(const uint8_t *data, uint32_t length, uint32_t *offset,
                        ssh_string *value) {
    if (*offset + 4 > length) {
        return false;
    }
    uint32_t string_length = read_u32(data + *offset);
    *offset += 4;
    if (string_length > length - *offset) {
        return false;
    }
    value->data = data + *offset;
    value->length = string_length;
    *offset += string_length;
    return true;
}

static bool string_equals(ssh_string value, const char *text) {
    uint32_t length = (uint32_t)strlen(text);
    return value.length == length && memcmp(value.data, text, length) == 0;
}

static uint32_t put_string(uint8_t *output, const void *data, uint32_t length) {
    write_u32(output, length);
    if (length) {
        memcpy(output + 4, data, length);
    }
    return length + 4;
}

static void send_channel_data(ssh_channel *channel, const uint8_t *data,
                              uint32_t length) {
    if (!channel_valid(channel) || !length) return;
    uint32_t generation = channel->generation;
    if (channel->outbound_length) {
        if (length > channel->outbound_capacity - channel->outbound_length) {
            channel_close(channel, true);
            return;
        }
        memcpy(channel->outbound + channel->outbound_length, data, length);
        channel->outbound_length += length;
        return;
    }
    uint32_t sent = 0;
    while (sent < length && channel_valid(channel)
           && channel->generation == generation
           && !channel->close_received && channel->remote_window) {
        uint32_t chunk = length - sent;
        if (chunk > channel->remote_window) chunk = channel->remote_window;
        if (chunk > SSH_MAX_PACKET - 9) chunk = SSH_MAX_PACKET - 9;
        uint32_t offset = 0;
        send_payload[offset++] = SSH_MSG_CHANNEL_DATA;
        write_u32(send_payload + offset, channel->client_channel);
        offset += 4;
        offset += put_string(send_payload + offset, data + sent, chunk);
        send_message(send_payload, offset);
        channel->remote_window -= chunk;
        sent += chunk;
    }
    if (!channel_valid(channel) || channel->generation != generation
        || sent == length) return;
    uint32_t remaining = length - sent;
    if (remaining > SSH_CHANNEL_OUTPUT_CAPACITY) {
        channel_close(channel, true);
        return;
    }
    if (!channel->outbound) {
        channel->outbound_capacity = SSH_CHANNEL_OUTPUT_CAPACITY;
        channel->outbound = (uint8_t *)kmalloc(channel->outbound_capacity);
        if (!channel->outbound) {
            channel_close(channel, true);
            return;
        }
    }
    memmove(channel->outbound, data + sent, remaining);
    channel->outbound_length = remaining;
}

static void send_window_adjust(ssh_channel *channel, uint32_t amount) {
    if (!channel_valid(channel) || !amount || channel->close_received) return;
    uint32_t offset = 0;
    send_payload[offset++] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    write_u32(send_payload + offset, channel->client_channel);
    offset += 4;
    write_u32(send_payload + offset, amount);
    offset += 4;
    send_message(send_payload, offset);
    channel->local_window += amount;
}

static void channel_flush_outbound(ssh_channel *channel) {
    if (!channel_valid(channel) || !channel->outbound_length
        || !channel->remote_window || channel->close_received) return;
    uint32_t generation = channel->generation;
    uint32_t length = channel->outbound_length;
    uint8_t *data = channel->outbound;
    channel->outbound_length = 0;
    send_channel_data(channel, data, length);
    if (!channel_valid(channel) || channel->generation != generation) return;
}

static void send_raw_packet(const uint8_t *payload, uint32_t payload_length) {
    uint32_t block_size = send_encrypted ? 16 : 8;
    uint32_t padding_length = block_size - ((payload_length + 5) % block_size);
    if (padding_length < 4) {
        padding_length += block_size;
    }
    uint32_t packet_length = payload_length + padding_length + 1;
    uint32_t plain_length = packet_length + 4;
    write_u32(packet_buffer, packet_length);
    packet_buffer[4] = (uint8_t)padding_length;
    memcpy(packet_buffer + 5, payload, payload_length);
    if (send_encrypted) {
        cklos_random_bytes(packet_buffer + 5 + payload_length, padding_length);
    } else {
        memset(packet_buffer + 5 + payload_length, 0, padding_length);
    }

    uint8_t mac[32];
    uint32_t mac_length = 0;
    if (send_encrypted) {
        write_u32(mac_buffer, send_sequence);
        memcpy(mac_buffer + 4, packet_buffer, plain_length);
        cklos_hmac_sha256(send_key, 32, mac_buffer, 4 + plain_length, mac);
        mac_length = 32;
        cklos_aes_ctr_crypt(&send_cipher, packet_buffer, plain_length);
    }
    if (!network_stream_write(ssh_control_stream, packet_buffer, plain_length)) {
        console_write("SSH: transport write failed\n");
        return;
    }
    if (mac_length) {
        network_stream_write(ssh_control_stream, mac, mac_length);
    }
    send_sequence++;
}

static void send_message(const uint8_t *payload, uint32_t length) {
    send_raw_packet(payload, length);
}

static uint32_t build_kexinit(uint8_t *output) {
    uint32_t offset = 0;
    output[offset++] = SSH_MSG_KEXINIT;
    cklos_random_bytes(output + offset, 16);
    offset += 16;
    const char *names[] = {
        "curve25519-sha256",
        "ssh-ed25519",
        "aes128-ctr", "aes128-ctr",
        "hmac-sha2-256", "hmac-sha2-256",
        "none", "none", "", ""
    };
    for (uint32_t index = 0; index < 10; index++) {
        offset += put_string(output + offset, names[index], (uint32_t)strlen(names[index]));
    }
    output[offset++] = 0;
    write_u32(output + offset, 0);
    offset += 4;
    return offset;
}

static void begin_key_exchange(void) {
    uint32_t length = build_kexinit(send_payload);
    memcpy(key_exchange_server, send_payload, length);
    key_exchange_server_length = length;
    phase = SSH_PHASE_KEX;
    send_message(send_payload, length);
}

static void derive_key(const uint8_t *k, uint32_t k_length, const uint8_t *hash,
                       char letter, uint8_t *output, uint32_t output_length) {
    uint8_t material[1024];
    uint32_t offset = 0;
    offset += put_string(material + offset, k, k_length);
    memcpy(material + offset, hash, 32);
    offset += 32;
    material[offset++] = (uint8_t)letter;
    memcpy(material + offset, session_id, 32);
    offset += 32;
    uint8_t digest[32];
    cklos_sha256(material, offset, digest);
    memcpy(output, digest, output_length > 32 ? 32 : output_length);
}

static void process_key_exchange(const uint8_t *payload, uint32_t length) {
    if (length < 5 || payload[0] != SSH_MSG_KEX_ECDH_INIT) {
        phase = SSH_PHASE_FAILED;
        return;
    }
    uint32_t offset = 1;
    ssh_string client_ephemeral;
    if (!read_string(payload, length, &offset, &client_ephemeral)
        || client_ephemeral.length != 32) {
        phase = SSH_PHASE_FAILED;
        return;
    }

    uint8_t ephemeral_secret[32];
    uint8_t ephemeral_public[32];
    uint8_t shared[32];
    cklos_random_bytes(ephemeral_secret, 32);
    cklos_x25519_public(ephemeral_secret, ephemeral_public);
    if (!cklos_x25519_shared(ephemeral_secret, client_ephemeral.data, shared)) {
        phase = SSH_PHASE_FAILED;
        return;
    }

    uint8_t k_value[33];
    uint32_t k_offset = 0;
    if (shared[0] & 0x80) {
        k_value[k_offset++] = 0;
    }
    uint32_t shared_offset = 0;
    while (shared_offset < 31 && shared[shared_offset] == 0) {
        shared_offset++;
    }
    memcpy(k_value + k_offset, shared + shared_offset, 32 - shared_offset);
    uint32_t k_length = k_offset + 32 - shared_offset;

    uint8_t host_secret[64];
    uint8_t host_public[32];
    cklos_ed25519_keypair(ssh_host_seed, host_secret, host_public);
    uint8_t host_key[64];
    uint32_t host_key_length = put_string(host_key, "ssh-ed25519", 11);
    host_key_length += put_string(host_key + host_key_length, host_public, 32);

    uint8_t exchange[2048];
    uint32_t exchange_length = 0;
    exchange_length += put_string(exchange + exchange_length, client_version, client_version_length);
    exchange_length += put_string(exchange + exchange_length, server_version, (uint32_t)strlen(server_version));
    exchange_length += put_string(exchange + exchange_length, key_exchange_client, key_exchange_client_length);
    exchange_length += put_string(exchange + exchange_length, key_exchange_server, key_exchange_server_length);
    exchange_length += put_string(exchange + exchange_length, host_key, host_key_length);
    exchange_length += put_string(exchange + exchange_length, client_ephemeral.data, 32);
    exchange_length += put_string(exchange + exchange_length, ephemeral_public, 32);
    exchange_length += put_string(exchange + exchange_length, k_value, k_length);

    uint8_t exchange_hash[32];
    cklos_sha256(exchange, exchange_length, exchange_hash);
    memcpy(session_id, exchange_hash, 32);
    uint8_t signature[64];
    cklos_ed25519_sign(host_secret, exchange_hash, 32, signature);
    uint8_t signature_blob[96];
    uint32_t signature_length = put_string(signature_blob, "ssh-ed25519", 11);
    signature_length += put_string(signature_blob + signature_length, signature, 64);

    uint32_t reply_length = 0;
    send_payload[reply_length++] = SSH_MSG_KEX_ECDH_REPLY;
    reply_length += put_string(send_payload + reply_length, host_key, host_key_length);
    reply_length += put_string(send_payload + reply_length, ephemeral_public, 32);
    reply_length += put_string(send_payload + reply_length, signature_blob, signature_length);
    send_message(send_payload, reply_length);

    uint8_t iv_c2s[16];
    uint8_t iv_s2c[16];
    uint8_t key_c2s[32];
    uint8_t key_s2c[32];
    uint8_t hmac_c2s[32];
    uint8_t hmac_s2c[32];
    derive_key(k_value, k_length, exchange_hash, 'A', iv_c2s, 16);
    derive_key(k_value, k_length, exchange_hash, 'B', iv_s2c, 16);
    derive_key(k_value, k_length, exchange_hash, 'C', key_c2s, 32);
    derive_key(k_value, k_length, exchange_hash, 'D', key_s2c, 32);
    derive_key(k_value, k_length, exchange_hash, 'E', hmac_c2s, 32);
    derive_key(k_value, k_length, exchange_hash, 'F', hmac_s2c, 32);
    cklos_aes_ctr_init(&receive_cipher, key_c2s, iv_c2s);
    cklos_aes_ctr_init(&send_cipher, key_s2c, iv_s2c);
    memcpy(receive_key, hmac_c2s, 32);
    memcpy(send_key, hmac_s2c, 32);

    const uint8_t newkeys = SSH_MSG_NEWKEYS;
    send_message(&newkeys, 1);
    send_encrypted = true;
}

static void send_auth_failure(void) {
    uint32_t offset = 0;
    send_payload[offset++] = SSH_MSG_USERAUTH_FAILURE;
    offset += put_string(send_payload + offset, "publickey", 9);
    send_payload[offset++] = 0;
    send_message(send_payload, offset);
}

static void process_userauth(const uint8_t *payload, uint32_t length) {
    uint32_t offset = 1;
    ssh_string user;
    ssh_string service;
    ssh_string method;
    uint8_t has_signature;
    ssh_string algorithm;
    ssh_string key_blob;
    if (!read_string(payload, length, &offset, &user)
        || !read_string(payload, length, &offset, &service)
        || !read_string(payload, length, &offset, &method)
        || !read_byte(payload, length, &offset, &has_signature)
        || !read_string(payload, length, &offset, &algorithm)
        || !read_string(payload, length, &offset, &key_blob)) {
        send_auth_failure();
        return;
    }
    if (!string_equals(user, "cklos") || !string_equals(service, "ssh-connection")
        || !string_equals(method, "publickey")
        || !string_equals(algorithm, "ssh-ed25519")
        || key_blob.length != 51 || memcmp(key_blob.data + 19, ssh_authorized_public_key, 32) != 0) {
        send_auth_failure();
        return;
    }

    if (!has_signature) {
        uint32_t out = 0;
        send_payload[out++] = SSH_MSG_USERAUTH_PK_OK;
        out += put_string(send_payload + out, algorithm.data, algorithm.length);
        out += put_string(send_payload + out, key_blob.data, key_blob.length);
        send_message(send_payload, out);
        return;
    }

    uint32_t signed_end = offset;
    ssh_string signature;
    if (!read_string(payload, length, &offset, &signature) || signature.length != 83) {
        send_auth_failure();
        return;
    }
    uint8_t signed_data[512];
    uint32_t signed_length = 0;
    signed_length += put_string(signed_data, session_id, 32);
    memcpy(signed_data + signed_length, payload, signed_end);
    signed_length += signed_end;
    if (read_u32(signature.data + 15) != 64
        || !cklos_ed25519_verify(ssh_authorized_public_key, signed_data, signed_length,
                                 signature.data + 19)) {
        send_auth_failure();
        return;
    }
    phase = SSH_PHASE_CONNECTION;
    const uint8_t success = SSH_MSG_USERAUTH_SUCCESS;
    send_message(&success, 1);
}

enum {
    SFTP_INIT = 1,
    SFTP_VERSION = 2,
    SFTP_OPEN = 3,
    SFTP_CLOSE = 4,
    SFTP_READ = 5,
    SFTP_WRITE = 6,
    SFTP_LSTAT = 7,
    SFTP_FSTAT = 8,
    SFTP_SETSTAT = 9,
    SFTP_FSETSTAT = 10,
    SFTP_OPENDIR = 11,
    SFTP_READDIR = 12,
    SFTP_REMOVE = 13,
    SFTP_MKDIR = 14,
    SFTP_RMDIR = 15,
    SFTP_REALPATH = 16,
    SFTP_STAT = 17,
    SFTP_RENAME = 18,
    SFTP_STATUS = 101,
    SFTP_HANDLE = 102,
    SFTP_DATA = 103,
    SFTP_NAME = 104,
    SFTP_ATTRS = 105,
    SFTP_STATUS_OK = 0,
    SFTP_STATUS_EOF = 1,
    SFTP_STATUS_NO_FILE = 2,
    SFTP_STATUS_FAILURE = 4,
    SFTP_STATUS_UNSUPPORTED = 8
};

static uint8_t *sftp_response;
static uint32_t sftp_response_length;
static uint32_t sftp_readdir_length;
static uint32_t sftp_readdir_count;
static bool agent_forward_enabled;

#define SFTP_HANDLES (sftp_channel->sftp_handles)
#define SFTP_INPUT (sftp_channel->sftp_input)
#define SFTP_INPUT_LENGTH (sftp_channel->sftp_input_length)

static bool sftp_packet_begin(uint8_t type, uint32_t id) {
    if (SFTP_BODY_MAX < 9) return false;
    write_u32(sftp_response, 0);
    sftp_response[4] = type;
    write_u32(sftp_response + 5, id);
    sftp_response_length = 9;
    return true;
}

static void sftp_packet_end(void) {
    write_u32(sftp_response, sftp_response_length - 4);
    send_channel_data(sftp_channel, sftp_response, sftp_response_length);
}

static void sftp_status(uint32_t id, uint32_t code, const char *message) {
    sftp_packet_begin(SFTP_STATUS, id);
    write_u32(sftp_response + sftp_response_length, code);
    sftp_response_length += 4;
    sftp_response_length += put_string(sftp_response + sftp_response_length,
                                       message, (uint32_t)strlen(message));
    sftp_response_length += put_string(sftp_response + sftp_response_length,
                                       "", 0);
    sftp_packet_end();
}

static bool sftp_path(ssh_string raw, char *output, uint32_t capacity) {
    uint32_t prefix_length = 0;
    if (!raw.length || raw.data[0] != '/') {
        const char *prefix = "/home/cklos/";
        prefix_length = (uint32_t)strlen(prefix);
        if (prefix_length + raw.length + 1 > capacity) return false;
        memcpy(output, prefix, prefix_length);
    } else if (raw.length + 1 > capacity) {
        return false;
    }
    memcpy(output + prefix_length, raw.data, raw.length);
    output[prefix_length + raw.length] = '\0';
    return true;
}

static void sftp_put_attrs(uint32_t size, uint8_t type) {
    write_u32(sftp_response + sftp_response_length, 0x00000007);
    sftp_response_length += 4;
    write_u64(sftp_response + sftp_response_length, size);
    sftp_response_length += 8;
    write_u32(sftp_response + sftp_response_length, 0);
    sftp_response_length += 4;
    write_u32(sftp_response + sftp_response_length, 0);
    sftp_response_length += 4;
    write_u32(sftp_response + sftp_response_length,
              type == FS2_TYPE_DIRECTORY ? 0040755 : 0100644);
    sftp_response_length += 4;
}

static sftp_handle *sftp_handle_by_name(ssh_string handle) {
    if (!handle.length || handle.length > 4) return 0;
    uint32_t index = 0;
    for (uint32_t offset = 0; offset < handle.length; offset++) {
        if (handle.data[offset] < '0' || handle.data[offset] > '9') return 0;
        index = index * 10 + handle.data[offset] - '0';
    }
    if (index >= SFTP_HANDLE_LIMIT || !SFTP_HANDLES[index].used) return 0;
    return &SFTP_HANDLES[index];
}

static sftp_handle *sftp_handle_allocate(const char *path, bool directory) {
    for (uint32_t index = 0; index < SFTP_HANDLE_LIMIT; index++) {
        if (!SFTP_HANDLES[index].used) {
            sftp_handle *handle = &SFTP_HANDLES[index];
            memset(handle, 0, sizeof(*handle));
            handle->used = true;
            handle->directory = directory;
            handle->path[0] = '\0';
            strcpy(handle->path, path);
            return handle;
        }
    }
    return 0;
}

static void sftp_list_callback(const char *name, uint32_t size, uint8_t type) {
    uint32_t name_length = (uint32_t)strlen(name);
    uint32_t required = 4 + name_length + 4 + name_length + 22;
    if (sftp_readdir_length + required >= SFTP_BODY_MAX) return;
    sftp_readdir_length += put_string(sftp_response + sftp_readdir_length,
                                      name, name_length);
    sftp_readdir_length += put_string(sftp_response + sftp_readdir_length,
                                      name, name_length);
    uint32_t save = sftp_response_length;
    sftp_response_length = sftp_readdir_length;
    sftp_put_attrs(size, type);
    sftp_readdir_length = sftp_response_length;
    sftp_response_length = save;
    sftp_readdir_count++;
}

static void sftp_process_packet(const uint8_t *packet, uint32_t length) {
    if (length < 5) return;
    uint8_t type = packet[0];
    uint32_t id = read_u32(packet + 1);
    uint32_t offset = 5;
    if (type == SFTP_INIT) {
        write_u32(sftp_response, 5);
        sftp_response[4] = SFTP_VERSION;
        write_u32(sftp_response + 5, 3);
        sftp_response_length = 9;
        sftp_packet_end();
        return;
    }
    if (type == SFTP_SETSTAT) {
        ssh_string raw;
        if (!read_string(packet, length, &offset, &raw)) {
            sftp_status(id, SFTP_STATUS_FAILURE, "bad setstat");
        } else {
            sftp_status(id, SFTP_STATUS_OK, "attributes accepted");
        }
        return;
    }
    if (type == SFTP_FSETSTAT) {
        ssh_string raw_handle;
        sftp_handle *handle = 0;
        if (read_string(packet, length, &offset, &raw_handle)) {
            handle = sftp_handle_by_name(raw_handle);
        }
        if (!handle) sftp_status(id, SFTP_STATUS_FAILURE, "bad handle");
        else sftp_status(id, SFTP_STATUS_OK, "attributes accepted");
        return;
    }
    if (type == SFTP_REALPATH || type == SFTP_STAT || type == SFTP_LSTAT) {
        ssh_string raw;
        char path[4096];
        if (!read_string(packet, length, &offset, &raw)
            || !sftp_path(raw, path, sizeof(path))) {
            sftp_status(id, SFTP_STATUS_FAILURE, "invalid path");
            return;
        }
        uint32_t size;
        uint8_t file_type;
        if (fs2_stat(path, &size, &file_type) != 0) {
            sftp_status(id, SFTP_STATUS_NO_FILE, "not found");
            return;
        }
        if (type == SFTP_REALPATH) {
            sftp_packet_begin(SFTP_NAME, id);
            write_u32(sftp_response + sftp_response_length, 1);
            sftp_response_length += 4;
            sftp_response_length += put_string(sftp_response + sftp_response_length,
                                               path, (uint32_t)strlen(path));
            sftp_response_length += put_string(sftp_response + sftp_response_length,
                                               path, (uint32_t)strlen(path));
            sftp_put_attrs(size, file_type);
            sftp_packet_end();
        } else {
            sftp_packet_begin(SFTP_ATTRS, id);
            sftp_put_attrs(size, file_type);
            sftp_packet_end();
        }
        return;
    }
    if (type == SFTP_OPEN || type == SFTP_OPENDIR || type == SFTP_MKDIR
        || type == SFTP_REMOVE || type == SFTP_RMDIR) {
        ssh_string raw;
        char path[4096];
        if (!read_string(packet, length, &offset, &raw)
            || !sftp_path(raw, path, sizeof(path))) {
            sftp_status(id, SFTP_STATUS_FAILURE, "invalid path");
            return;
        }
        if (type == SFTP_OPEN || type == SFTP_OPENDIR) {
            uint32_t flags = type == SFTP_OPEN && offset + 4 <= length
                           ? read_u32(packet + offset) : 1;
            if (type == SFTP_OPEN && (flags & 16)) {
                fs2_truncate(path, 0);
            }
            uint32_t size = 0;
            uint8_t file_type = FS2_TYPE_FILE;
            fs2_stat(path, &size, &file_type);
            if (type == SFTP_OPENDIR && file_type != FS2_TYPE_DIRECTORY) {
                sftp_status(id, SFTP_STATUS_FAILURE, "not a directory");
                return;
            }
            if (type == SFTP_OPEN && (flags & 8) && !size) {
                fs2_write(path, "", 0, false);
            }
            sftp_handle *handle =
                sftp_handle_allocate(path, type == SFTP_OPENDIR);
            if (!handle) {
                sftp_status(id, SFTP_STATUS_FAILURE, "too many handles");
                return;
            }
            char handle_text[8];
            uint_to_string((uint32_t)(handle - SFTP_HANDLES), handle_text, 10);
            sftp_packet_begin(SFTP_HANDLE, id);
            sftp_response_length += put_string(
                sftp_response + sftp_response_length, handle_text,
                (uint32_t)strlen(handle_text));
            sftp_packet_end();
        } else if (type == SFTP_MKDIR) {
            if (fs2_mkdir(path) != 0) sftp_status(id, SFTP_STATUS_FAILURE, "mkdir failed");
            else sftp_status(id, SFTP_STATUS_OK, "created");
        } else {
            if (fs2_remove(path) != 0) sftp_status(id, SFTP_STATUS_FAILURE, "remove failed");
            else sftp_status(id, SFTP_STATUS_OK, "removed");
        }
        return;
    }
    if (type == SFTP_RENAME) {
        ssh_string old_raw;
        ssh_string new_raw;
        char old_path[4096];
        char new_path[4096];
        if (!read_string(packet, length, &offset, &old_raw)
            || !read_string(packet, length, &offset, &new_raw)
            || !sftp_path(old_raw, old_path, sizeof(old_path))
            || !sftp_path(new_raw, new_path, sizeof(new_path))
            || fs2_rename(old_path, new_path) != 0) {
            sftp_status(id, SFTP_STATUS_FAILURE, "rename failed");
        } else {
            sftp_status(id, SFTP_STATUS_OK, "renamed");
        }
        return;
    }
    if (type == SFTP_CLOSE || type == SFTP_READ || type == SFTP_WRITE
        || type == SFTP_READDIR || type == SFTP_FSTAT) {
        ssh_string raw_handle;
        if (!read_string(packet, length, &offset, &raw_handle)) {
            sftp_status(id, SFTP_STATUS_FAILURE, "bad handle");
            return;
        }
        sftp_handle *handle = sftp_handle_by_name(raw_handle);
        if (!handle) {
            sftp_status(id, SFTP_STATUS_FAILURE, "bad handle");
            return;
        }
        if (type == SFTP_CLOSE) {
            handle->used = false;
            sftp_status(id, SFTP_STATUS_OK, "closed");
        } else if (type == SFTP_FSTAT) {
            uint32_t size;
            uint8_t file_type;
            if (fs2_stat(handle->path, &size, &file_type) != 0) {
                sftp_status(id, SFTP_STATUS_NO_FILE, "not found");
            } else {
                sftp_packet_begin(SFTP_ATTRS, id);
                sftp_put_attrs(size, file_type);
                sftp_packet_end();
            }
        } else if (type == SFTP_READ) {
            if (offset + 12 > length) {
                sftp_status(id, SFTP_STATUS_FAILURE, "bad read");
                return;
            }
            uint64_t read_offset = read_u64(packet + offset);
            uint32_t count = read_u32(packet + offset + 8);
            if (read_offset > 0xFFFFFFFFULL) {
                sftp_status(id, SFTP_STATUS_FAILURE, "read offset too large");
                return;
            }
            if (count > SFTP_BODY_MAX - 13) count = SFTP_BODY_MAX - 13;
            uint32_t actual = 0;
            sftp_packet_begin(SFTP_DATA, id);
            int read_result = fs2_read_at(
                handle->path, (uint32_t)read_offset,
                sftp_response + sftp_response_length + 4, count, &actual);
            if (read_result != 0) {
                sftp_status(id, SFTP_STATUS_FAILURE, "read failed");
            } else if (!actual) {
                sftp_status(id, SFTP_STATUS_EOF, "eof");
            } else {
                write_u32(sftp_response + sftp_response_length, actual);
                sftp_response_length += 4 + actual;
                sftp_packet_end();
            }
        } else if (type == SFTP_WRITE) {
            if (offset + 12 > length) {
                sftp_status(id, SFTP_STATUS_FAILURE, "bad write");
                return;
            }
            uint64_t write_offset = read_u64(packet + offset);
            ssh_string data;
            offset += 8;
            if (write_offset > 0xFFFFFFFFULL
                || !read_string(packet, length, &offset, &data)
                || fs2_write_at(handle->path, (uint32_t)write_offset,
                                data.data, data.length) != 0) {
                sftp_status(id, SFTP_STATUS_FAILURE, "write failed");
            } else {
                sftp_status(id, SFTP_STATUS_OK, "written");
            }
        } else {
            if (handle->offset) {
                sftp_status(id, SFTP_STATUS_EOF, "eof");
                return;
            }
            sftp_packet_begin(SFTP_NAME, id);
            sftp_readdir_length = sftp_response_length + 4;
            sftp_readdir_count = 0;
            fs2_list(handle->path, sftp_list_callback);
            write_u32(sftp_response + sftp_response_length, sftp_readdir_count);
            sftp_response_length = sftp_readdir_length;
            handle->offset = 1;
            sftp_packet_end();
        }
        return;
    }
    sftp_status(id, SFTP_STATUS_UNSUPPORTED, "unsupported operation");
}

static void sftp_receive(ssh_channel *channel, const uint8_t *data,
                         uint32_t length) {
    sftp_channel = channel;
    if (SFTP_INPUT_LENGTH + length > SSH_MAX_PACKET) return;
    memcpy(SFTP_INPUT + SFTP_INPUT_LENGTH, data, length);
    SFTP_INPUT_LENGTH += length;
    while (SFTP_INPUT_LENGTH >= 4) {
        uint32_t packet_length = SFTP_INPUT[0] << 24
                               | SFTP_INPUT[1] << 16
                               | SFTP_INPUT[2] << 8 | SFTP_INPUT[3];
        if (packet_length < 1 || packet_length > SSH_MAX_PACKET - 4) {
            SFTP_INPUT_LENGTH = 0;
            return;
        }
        if (SFTP_INPUT_LENGTH < packet_length + 4) return;
        sftp_process_packet(SFTP_INPUT + 4, packet_length);
        memmove(SFTP_INPUT, SFTP_INPUT + packet_length + 4,
                SFTP_INPUT_LENGTH - packet_length - 4);
        SFTP_INPUT_LENGTH -= packet_length + 4;
    }
}

static void send_open_failure(uint32_t client_channel, uint32_t reason,
                              const char *description) {
    uint32_t out = 0;
    send_payload[out++] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    write_u32(send_payload + out, client_channel);
    out += 4;
    write_u32(send_payload + out, reason);
    out += 4;
    out += put_string(send_payload + out, description,
                      (uint32_t)strlen(description));
    out += put_string(send_payload + out, "", 0);
    send_message(send_payload, out);
}

static void send_open_confirmation(ssh_channel *channel) {
    uint32_t out = 0;
    send_payload[out++] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
    write_u32(send_payload + out, channel->client_channel);
    out += 4;
    write_u32(send_payload + out, channel->server_channel);
    out += 4;
    write_u32(send_payload + out, channel->local_window);
    out += 4;
    write_u32(send_payload + out, 32768);
    out += 4;
    send_message(send_payload, out);
}

static bool channel_queue_data(ssh_channel *channel, const uint8_t *data,
                               uint32_t length) {
    if (!channel_valid(channel)) return false;
    if (!channel->pending) {
        channel->pending_capacity = 32768;
        channel->pending = (uint8_t *)kmalloc(channel->pending_capacity);
        if (!channel->pending) return false;
    }
    if (length > channel->pending_capacity - channel->pending_length) {
        return false;
    }
    memcpy(channel->pending + channel->pending_length, data, length);
    channel->pending_length += length;
    return true;
}

static void channel_flush_pending(ssh_channel *channel) {
    if (!channel_valid(channel) || !channel->confirmed
        || !channel->pending_length) return;
    uint32_t generation = channel->generation;
    send_channel_data(channel, channel->pending, channel->pending_length);
    if (!channel_valid(channel) || channel->generation != generation) return;
    channel->pending_length = 0;
    if (channel->pending_close) {
        channel->pending_close = false;
        channel_close(channel, true);
    }
}

static void forward_stream_event(int id, uint8_t event, const uint8_t *data,
                                 uint32_t length, void *context) {
    ssh_channel *channel = (ssh_channel *)context;
    if (!channel_valid(channel) || channel->stream != id) return;
    if (event == TCP_EVENT_CONNECTED) {
        if (channel->mode == SSH_CHANNEL_DIRECT && !channel->confirmed) {
            channel->confirmed = true;
            send_open_confirmation(channel);
            channel_flush_pending(channel);
        }
    } else if (event == TCP_EVENT_DATA) {
        if (!channel->confirmed) {
            if (!channel_queue_data(channel, data, length)) {
                channel_close(channel, true);
            }
        } else {
            send_channel_data(channel, data, length);
        }
    } else if (event == TCP_EVENT_DISCONNECTED) {
        if (channel->confirmed) channel_close(channel, true);
        else channel->pending_close = true;
    }
}

static void forwarded_stream_event(int id, uint8_t event, const uint8_t *data,
                                   uint32_t length, void *context) {
    ssh_remote_forward *forward = (ssh_remote_forward *)context;
    if (event == TCP_EVENT_CONNECTED || event == TCP_EVENT_DATA) {
        ssh_channel *channel = 0;
        for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
            if (channels[index].used && channels[index].stream == id
                && channels[index].mode == SSH_CHANNEL_FORWARDED) {
                channel = &channels[index];
                break;
            }
        }
        if (event == TCP_EVENT_CONNECTED) {
            if (!forward || !forward->used) {
                network_stream_close(id);
                return;
            }
            channel = channel_allocate(next_server_channel++, 0);
            if (!channel) {
                network_stream_close(id);
                return;
            }
            channel->mode = SSH_CHANNEL_FORWARDED;
            channel->stream = id;
            channel->confirmed = false;
            for (uint32_t index = 0; index < REMOTE_FORWARD_LIMIT; index++) {
                if (&remote_forwards[index] == forward) {
                    channel->forward_index = (int)index;
                    break;
                }
            }
            uint32_t out = 0;
            send_payload[out++] = SSH_MSG_CHANNEL_OPEN;
            out += put_string(send_payload + out, "forwarded-tcpip", 15);
            write_u32(send_payload + out, channel->server_channel);
            out += 4;
            write_u32(send_payload + out, channel->local_window);
            out += 4;
            write_u32(send_payload + out, 32768);
            out += 4;
            out += put_string(send_payload + out, forward->address,
                              (uint32_t)strlen(forward->address));
            write_u32(send_payload + out, forward->port);
            out += 4;
            out += put_string(send_payload + out, "0.0.0.0", 7);
            write_u32(send_payload + out, 0);
            out += 4;
            send_message(send_payload, out);
            return;
        }
        if (channel) {
            if (!channel->confirmed) {
                if (!channel_queue_data(channel, data, length)) {
                    channel->pending_close = true;
                }
            } else {
                send_channel_data(channel, data, length);
            }
        } else {
            network_stream_close(id);
        }
    } else if (event == TCP_EVENT_DISCONNECTED) {
        for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
            ssh_channel *channel = &channels[index];
            if (!channel->used || channel->stream != id
                || channel->mode != SSH_CHANNEL_FORWARDED) continue;
            if (channel->confirmed) channel_close(channel, true);
            else channel->pending_close = true;
            break;
        }
    }
}

static void remote_forward_cancel(ssh_remote_forward *forward) {
    if (!forward || !forward->used) return;
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        ssh_channel *channel = &channels[index];
        if (channel->used && channel->mode == SSH_CHANNEL_FORWARDED
            && &remote_forwards[channel->forward_index] == forward) {
            channel_close(channel, true);
        }
    }
    network_stream_cancel_listen(forward->listener);
    memset(forward, 0, sizeof(*forward));
    forward->listener = -1;
}

static void remote_forward_cancel_all(void) {
    for (uint32_t index = 0; index < REMOTE_FORWARD_LIMIT; index++) {
        remote_forward_cancel(&remote_forwards[index]);
    }
}

static void request_reply(bool want_reply, bool success,
                          uint32_t extra_value, bool include_extra) {
    if (!want_reply) return;
    uint32_t out = 0;
    send_payload[out++] = success ? SSH_MSG_REQUEST_SUCCESS
                                  : SSH_MSG_REQUEST_FAILURE;
    if (success && include_extra) {
        write_u32(send_payload + out, extra_value);
        out += 4;
    }
    send_message(send_payload, out);
}

static void handle_global_request(const uint8_t *payload, uint32_t length,
                                  uint32_t offset) {
    ssh_string request;
    uint8_t want_reply;
    if (!read_string(payload, length, &offset, &request)
        || !read_byte(payload, length, &offset, &want_reply)) return;
    if (string_equals(request, "tcpip-forward")) {
        ssh_string address;
        if (offset + 4 > length
            || !read_string(payload, length, &offset, &address)) {
            request_reply(want_reply, false, 0, false);
            return;
        }
        uint32_t requested_port = read_u32(payload + offset);
        if (requested_port > 65535) {
            request_reply(want_reply, false, 0, false);
            return;
        }
        ssh_remote_forward *forward = 0;
        for (uint32_t index = 0; index < REMOTE_FORWARD_LIMIT; index++) {
            if (!remote_forwards[index].used) {
                forward = &remote_forwards[index];
                break;
            }
        }
        if (!forward || address.length >= sizeof(forward->address)) {
            request_reply(want_reply, false, 0, false);
            return;
        }
        memset(forward, 0, sizeof(*forward));
        forward->listener = -1;
        if (address.length) {
            memcpy(forward->address, address.data, address.length);
            forward->address[address.length] = '\0';
        } else {
            strcpy(forward->address, "0.0.0.0");
        }
        forward->requested_port = (uint16_t)requested_port;
        int listener = network_stream_listen((uint16_t)requested_port,
                                             forwarded_stream_event, forward);
        if (listener < 0) {
            memset(forward, 0, sizeof(*forward));
            forward->listener = -1;
            request_reply(want_reply, false, 0, false);
            return;
        }
        forward->used = true;
        forward->listener = listener;
        forward->port = network_stream_listener_port(listener);
        request_reply(want_reply, true, forward->port,
                      requested_port == 0);
        return;
    }
    if (string_equals(request, "cancel-tcpip-forward")) {
        ssh_string address;
        if (offset + 4 > length
            || !read_string(payload, length, &offset, &address)) {
            request_reply(want_reply, false, 0, false);
            return;
        }
        uint32_t port = read_u32(payload + offset);
        bool cancelled = false;
        for (uint32_t index = 0; index < REMOTE_FORWARD_LIMIT; index++) {
            ssh_remote_forward *forward = &remote_forwards[index];
            if (!forward->used || forward->port != port) continue;
            uint32_t stored_length = (uint32_t)strlen(forward->address);
            if (address.length == stored_length
                && (!stored_length
                    || memcmp(forward->address, address.data,
                              stored_length) == 0)) {
                remote_forward_cancel(forward);
                cancelled = true;
                break;
            }
        }
        request_reply(want_reply, cancelled, 0, false);
        return;
    }
    request_reply(want_reply, false, 0, false);
}

static void handle_channel_open(const uint8_t *payload, uint32_t length,
                                uint32_t offset) {
    ssh_string type;
    if (!read_string(payload, length, &offset, &type) || offset + 12 > length) {
        return;
    }
    uint32_t client_id = read_u32(payload + offset);
    uint32_t remote_window = read_u32(payload + offset + 4);
    offset += 12;
    if (string_equals(type, "session")) {
        if (!remote_window) {
            send_open_failure(client_id, 1, "zero window");
            return;
        }
        ssh_channel *channel = channel_allocate(client_id, remote_window);
        if (!channel) {
            send_open_failure(client_id, 4, "too many channels");
            return;
        }
        channel->mode = SSH_CHANNEL_NONE;
        channel->confirmed = true;
        send_open_confirmation(channel);
        return;
    }
    if (string_equals(type, "direct-tcpip")) {
        ssh_string host_string;
        if (!read_string(payload, length, &offset, &host_string)
            || offset + 4 > length) {
            send_open_failure(client_id, 1, "malformed direct-tcpip");
            return;
        }
        char host[256];
        if (!host_string.length || host_string.length >= sizeof(host)) {
            send_open_failure(client_id, 1, "invalid target host");
            return;
        }
        memcpy(host, host_string.data, host_string.length);
        host[host_string.length] = '\0';
        uint32_t requested_port = read_u32(payload + offset);
        if (requested_port > 65535) {
            send_open_failure(client_id, 1, "invalid target port");
            return;
        }
        uint16_t port = (uint16_t)requested_port;
        uint32_t address;
        bool resolved = resolver_lookup_ipv4(host, &address, 4000);
        if (!remote_window || !port || !resolved) {
            console_printf("SSH: direct target %s:%u window %u resolved %u\n",
                           host, port, remote_window, resolved ? 1U : 0U);
            send_open_failure(client_id, 2, "connect failed");
            return;
        }
        ssh_channel *channel = channel_allocate(client_id, remote_window);
        if (!channel) {
            send_open_failure(client_id, 4, "too many channels");
            return;
        }
        channel->mode = SSH_CHANNEL_DIRECT;
        int stream = network_stream_begin_open(address, port,
                                               forward_stream_event,
                                               channel, 5000);
        if (stream < 0) {
            console_printf("SSH: direct-tcpip stream open failed %d\n", stream);
            channel_free(channel);
            send_open_failure(client_id, 2, "connect failed");
            return;
        }
        channel->stream = stream;
        channel->confirmed = false;
        return;
    }
    send_open_failure(client_id, 1, "unsupported channel");
}

static void handle_channel_open_reply(const uint8_t *payload, uint32_t length,
                                      uint32_t offset, bool success) {
    if (offset + 4 > length) return;
    ssh_channel *channel = channel_lookup_server(read_u32(payload + offset));
    if (!channel || (channel->mode != SSH_CHANNEL_FORWARDED
                     && channel->mode != SSH_CHANNEL_AGENT)
        || channel->confirmed) return;
    if (!success) {
        channel_close(channel, false);
        return;
    }
    if (offset + 12 > length) {
        channel_close(channel, false);
        return;
    }
    channel->client_channel = read_u32(payload + offset + 4);
    channel->remote_window = read_u32(payload + offset + 8);
    channel->confirmed = true;
    if (!channel->remote_window) {
        channel_close(channel, true);
        return;
    }
    if (channel->mode == SSH_CHANNEL_FORWARDED) channel_flush_pending(channel);
}

static void handle_channel_request(const uint8_t *payload, uint32_t length,
                                   uint32_t offset) {
    if (offset + 4 > length) return;
    uint32_t recipient = read_u32(payload + offset);
    offset += 4;
    ssh_string request;
    uint8_t want_reply;
    if (!read_string(payload, length, &offset, &request)
        || !read_byte(payload, length, &offset, &want_reply)) return;
    ssh_channel *channel = channel_lookup_server(recipient);
    bool accepted = false;
    if (channel) {
        accepted = string_equals(request, "pty-req")
                 || string_equals(request, "shell")
                 || string_equals(request, "exec")
                 || string_equals(request, "subsystem")
                 || string_equals(request, "auth-agent-req@openssh.com");
    }
    if (channel && string_equals(request, "subsystem")) {
        ssh_string subsystem;
        if (!read_string(payload, length, &offset, &subsystem)
            || !string_equals(subsystem, "sftp")) {
            accepted = false;
        } else {
            if (!channel->sftp_input) {
                channel->sftp_input = (uint8_t *)kmalloc(SSH_MAX_PACKET);
            }
            if (!channel->sftp_input) {
                accepted = false;
            } else {
                channel->mode = SSH_CHANNEL_SFTP;
                channel->sftp_input_length = 0;
                sftp_channel = channel;
                memset(SFTP_HANDLES, 0, sizeof(SFTP_HANDLES));
                status_text[0] = '\0';
                strcpy(status_text, "sftp");
            }
        }
    }
    if (channel && string_equals(request, "exec")) {
        ssh_string command;
        if (!read_string(payload, length, &offset, &command)) {
            accepted = false;
        } else {
            queue_console_bytes(command.data, command.length);
            const uint8_t newline = '\n';
            queue_console_bytes(&newline, 1);
        }
    }
    if (channel && string_equals(request, "shell") && accepted) {
        channel->mode = SSH_CHANNEL_CONSOLE;
        shell_active = true;
        status_text[0] = '\0';
        strcpy(status_text, "authenticated shell");
        console_write("\r\nCKLOS SSH terminal\r\n");
        if (agent_forward_enabled) {
            console_printf("SSH agent: %s\r\n", AGENT_SOCKET_PATH);
        }
    } else if (channel && string_equals(request, "exec") && accepted) {
        channel->mode = SSH_CHANNEL_CONSOLE;
        shell_active = true;
        remote_exec_active = true;
        remote_output_length = 0;
        status_text[0] = '\0';
        strcpy(status_text, "remote command");
    } else if (channel && string_equals(request, "auth-agent-req@openssh.com")
               && accepted) {
        agent_forward_enabled = true;
        strcpy(status_text, "agent forwarding");
    }
    if (want_reply) {
        uint32_t out = 0;
        send_payload[out++] = accepted ? SSH_MSG_CHANNEL_SUCCESS
                                      : SSH_MSG_CHANNEL_FAILURE;
        write_u32(send_payload + out,
                  channel ? channel->client_channel : recipient);
        out += 4;
        send_message(send_payload, out);
    }
}

static void handle_channel_data(const uint8_t *payload, uint32_t length,
                                uint32_t offset) {
    if (offset + 4 > length) return;
    ssh_channel *channel = channel_lookup_server(read_u32(payload + offset));
    offset += 4;
    ssh_string data;
    if (!channel || !read_string(payload, length, &offset, &data)) return;
    if (data.length > channel->local_window) {
        phase = SSH_PHASE_FAILED;
        return;
    }
    channel->local_window -= data.length;
    if (channel->mode == SSH_CHANNEL_SFTP) {
        sftp_receive(channel, data.data, data.length);
    } else if (channel->mode == SSH_CHANNEL_DIRECT
               || channel->mode == SSH_CHANNEL_FORWARDED) {
        if (!network_stream_write(channel->stream, data.data, data.length)) {
            channel_close(channel, true);
            return;
        }
    } else if (channel->mode == SSH_CHANNEL_AGENT) {
        ssh_agent_receive(data.data, data.length);
    } else {
        queue_console_bytes(data.data, data.length);
    }
    send_window_adjust(channel, data.length);
}

static void process_connection(const uint8_t *payload, uint32_t length) {
    if (!length) return;
    uint32_t offset = 1;
    if (payload[0] == SSH_MSG_SERVICE_REQUEST) {
        ssh_string service;
        if (read_string(payload, length, &offset, &service)
            && string_equals(service, "ssh-userauth")) {
            uint32_t out = 0;
            send_payload[out++] = SSH_MSG_SERVICE_ACCEPT;
            out += put_string(send_payload + out, service.data, service.length);
            phase = SSH_PHASE_AUTH;
            send_message(send_payload, out);
        }
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_OPEN) {
        handle_channel_open(payload, length, offset);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
        handle_channel_open_reply(payload, length, offset, true);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_OPEN_FAILURE) {
        handle_channel_open_reply(payload, length, offset, false);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_REQUEST) {
        handle_channel_request(payload, length, offset);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_DATA) {
        handle_channel_data(payload, length, offset);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
        if (offset + 8 > length) return;
        ssh_channel *channel = channel_lookup_server(read_u32(payload + offset));
        if (!channel) return;
        channel->remote_window += read_u32(payload + offset + 4);
        channel_flush_outbound(channel);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_EOF) {
        if (offset + 4 > length) return;
        ssh_channel *channel = channel_lookup_server(read_u32(payload + offset));
        if (!channel) return;
        channel->eof_received = true;
        if (channel->mode == SSH_CHANNEL_SFTP) channel_close(channel, true);
        return;
    }
    if (payload[0] == SSH_MSG_CHANNEL_CLOSE) {
        if (offset + 4 > length) return;
        ssh_channel *channel = channel_lookup_server(read_u32(payload + offset));
        if (!channel) return;
        channel->close_received = true;
        bool send_close = !channel->close_sent;
        if (send_close) {
            uint32_t out = 0;
            send_payload[out++] = SSH_MSG_CHANNEL_CLOSE;
            write_u32(send_payload + out, channel->client_channel);
            out += 4;
            send_message(send_payload, out);
            channel->close_sent = true;
        }
        channel_free(channel);
        console_input_head = console_input_tail = 0;
        if (!shell_active) strcpy(status_text, "listening");
        return;
    }
    if (payload[0] == SSH_MSG_GLOBAL_REQUEST) {
        handle_global_request(payload, length, offset);
        return;
    }
    if (payload[0] == SSH_MSG_USERAUTH_REQUEST) {
        process_userauth(payload, length);
    }
}

static void process_payload(const uint8_t *payload, uint32_t length) {
    if (!length || phase == SSH_PHASE_FAILED) {
        return;
    }
    switch (payload[0]) {
        case SSH_MSG_KEXINIT:
            memcpy(key_exchange_client, payload, length);
            key_exchange_client_length = length;
            break;
        case SSH_MSG_KEX_ECDH_INIT:
            process_key_exchange(payload, length);
            break;
        case SSH_MSG_NEWKEYS:
            if (send_encrypted) {
                receive_encrypted = true;
            }
            break;
        case SSH_MSG_SERVICE_REQUEST:
            process_connection(payload, length);
            break;
        case SSH_MSG_USERAUTH_REQUEST:
            if (phase == SSH_PHASE_AUTH) {
                process_userauth(payload, length);
            }
            break;
        case SSH_MSG_CHANNEL_OPEN:
        case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
        case SSH_MSG_CHANNEL_OPEN_FAILURE:
        case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        case SSH_MSG_CHANNEL_REQUEST:
        case SSH_MSG_CHANNEL_DATA:
        case SSH_MSG_CHANNEL_EOF:
        case SSH_MSG_CHANNEL_CLOSE:
        case SSH_MSG_GLOBAL_REQUEST:
            process_connection(payload, length);
            break;
        case SSH_MSG_DISCONNECT:
            if (ssh_control_stream >= 0) {
                network_stream_close(ssh_control_stream);
                ssh_control_stream = -1;
            }
            reset_connection_channels();
            phase = SSH_PHASE_FAILED;
            break;
        default:
            break;
    }
}

static void process_packets(void) {
    while (input_length >= 5) {
        uint8_t header[16];
        uint32_t peek_length = input_length < 16 ? input_length : 16;
        memcpy(header, input, peek_length);
        if (receive_encrypted) {
            cklos_aes_ctr peek = receive_cipher;
            cklos_aes_ctr_crypt(&peek, header, peek_length);
        }
        uint32_t packet_length = read_u32(header);
        if (packet_length < 5 || packet_length > SSH_MAX_PACKET
            || (receive_encrypted && ((packet_length + 4) & 15)) != 0) {
            console_printf("SSH: invalid packet length %u\n", packet_length);
            phase = SSH_PHASE_FAILED;
            return;
        }
        uint32_t encrypted_length = packet_length + 4;
        uint32_t total = encrypted_length + (receive_encrypted ? 32 : 0);
        if (input_length < total) {
            return;
        }
        if (receive_encrypted) {
            uint8_t mac[32];
            cklos_aes_ctr_crypt(&receive_cipher, input, encrypted_length);
            write_u32(mac_buffer, receive_sequence);
            memcpy(mac_buffer + 4, input, encrypted_length);
            cklos_hmac_sha256(receive_key, 32, mac_buffer,
                              4 + encrypted_length, mac);
            uint8_t difference = 0;
            for (uint32_t index = 0; index < 32; index++) {
                difference |= mac[index] ^ input[encrypted_length + index];
            }
            if (difference) {
                console_printf("SSH: MAC verification failed at sequence %u\n",
                               receive_sequence);
                phase = SSH_PHASE_FAILED;
                return;
            }
        }
        uint8_t padding_length = input[4];
        if (padding_length < 4 || padding_length + 1 >= packet_length) {
            console_printf("SSH: bad padding %u packet %u\n",
                           padding_length, packet_length);
            phase = SSH_PHASE_FAILED;
            return;
        }
        process_payload(input + 5, packet_length - padding_length - 1);
        receive_sequence++;
        memmove(input, input + total, input_length - total);
        input_length -= total;
    }
}

static void process_version(void) {
    while (input_length >= 2) {
        uint32_t index = 0;
        while (index + 1 < input_length
               && !(input[index] == '\r' && input[index + 1] == '\n')) {
            index++;
        }
        if (index + 1 >= input_length) {
            return;
        }
        if (index < 4 || memcmp(input, "SSH-", 4) != 0) {
            memmove(input, input + index + 2, input_length - index - 2);
            input_length -= index + 2;
            continue;
        }
        if (index >= sizeof(client_version)) {
            phase = SSH_PHASE_FAILED;
            return;
        }
        memcpy(client_version, input, index);
        client_version_length = index;
        memmove(input, input + index + 2, input_length - index - 2);
        input_length -= index + 2;
        phase = SSH_PHASE_KEX;
        begin_key_exchange();
        return;
    }
}

static ssh_channel *active_console_channel(void) {
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        if (channels[index].used
            && channels[index].mode == SSH_CHANNEL_CONSOLE) {
            return &channels[index];
        }
    }
    return 0;
}

static void reset_connection_channels(void) {
    remote_forward_cancel_all();
    for (uint32_t index = 0; index < SSH_CHANNEL_LIMIT; index++) {
        if (channels[index].used) channel_free(&channels[index]);
    }
    shell_active = false;
    remote_exec_active = false;
    remote_output_length = 0;
    agent_forward_enabled = false;
    agent_channel = 0;
    agent_rx_length = 0;
    agent_rx_eof = true;
    console_input_head = console_input_tail = 0;
}

static void ssh_stream_event(int id, uint8_t event, const uint8_t *data,
                             uint32_t length, void *context) {
    (void)context;
    if (event == TCP_EVENT_CONNECTED) {
        ssh_control_stream = id;
        reset_connection_channels();
        phase = SSH_PHASE_VERSION;
        input_length = 0;
        send_sequence = 0;
        receive_sequence = 0;
        send_encrypted = false;
        receive_encrypted = false;
        status_text[0] = '\0';
        static const char banner[] = SSH_SERVER_VERSION "\r\n";
        network_stream_write(id, (const uint8_t *)banner, sizeof(banner) - 1);
    } else if (event == TCP_EVENT_DATA) {
        if (id != ssh_control_stream) return;
        if (input_length + length <= SSH_MAX_PACKET) {
            memcpy(input + input_length, data, length);
            input_length += length;
            if (processing_ssh_input) return;
            processing_ssh_input = true;
            if (phase == SSH_PHASE_VERSION) {
                process_version();
            }
            process_packets();
            processing_ssh_input = false;
        }
    } else if (event == TCP_EVENT_DISCONNECTED) {
        if (id != ssh_control_stream) return;
        ssh_control_stream = -1;
        reset_connection_channels();
        phase = SSH_PHASE_VERSION;
        input_length = 0;
        processing_ssh_input = false;
        strcpy(status_text, "listening");
    }
}

bool ssh_agent_available(void) {
    return agent_forward_enabled;
}

const char *ssh_agent_socket_path(void) {
    return AGENT_SOCKET_PATH;
}

static void ssh_agent_poll(void) {
    if (agent_channel && !channel_valid(agent_channel)) {
        agent_channel = 0;
        agent_rx_length = 0;
        agent_rx_eof = true;
    }
}

static void ssh_agent_receive(const uint8_t *data, uint32_t length) {
    if (!agent_channel || !channel_valid(agent_channel)) return;
    if (length > SSH_AGENT_RX_CAPACITY - agent_rx_length) {
        channel_close(agent_channel, true);
        return;
    }
    memcpy(agent_rx + agent_rx_length, data, length);
    agent_rx_length += length;
}

bool ssh_agent_connect(void) {
    ssh_agent_poll();
    if (!agent_forward_enabled || !agent_rx) return false;
    if (agent_channel && channel_valid(agent_channel)) return true;
    agent_rx_length = 0;
    agent_rx_eof = false;
    ssh_channel *channel = channel_allocate(next_server_channel++, 0);
    if (!channel) return false;
    channel->mode = SSH_CHANNEL_AGENT;
    agent_channel = channel;
    uint32_t out = 0;
    send_payload[out++] = SSH_MSG_CHANNEL_OPEN;
    out += put_string(send_payload + out, "auth-agent@openssh.com", 22);
    write_u32(send_payload + out, channel->server_channel);
    out += 4;
    write_u32(send_payload + out, channel->local_window);
    out += 4;
    write_u32(send_payload + out, 32768);
    out += 4;
    send_message(send_payload, out);
    uint32_t target = timer_ticks() + SSH_AGENT_OPEN_TIMEOUT / 10;
    while ((int32_t)(timer_ticks() - target) < 0) {
        network_poll();
        ssh_agent_poll();
        if (!agent_channel) return false;
        if (agent_channel->confirmed) return true;
        cpu_pause();
    }
    ssh_agent_close();
    console_write("SSH: agent open timed out\n");
    return false;
}

bool ssh_agent_connect_async(void) {
    ssh_agent_poll();
    if (!agent_forward_enabled || !agent_rx) return false;
    if (agent_channel && channel_valid(agent_channel)) return true;
    agent_rx_length = 0;
    agent_rx_eof = false;
    ssh_channel *channel = channel_allocate(next_server_channel++, 0);
    if (!channel) return false;
    channel->mode = SSH_CHANNEL_AGENT;
    agent_channel = channel;
    uint32_t out = 0;
    send_payload[out++] = SSH_MSG_CHANNEL_OPEN;
    out += put_string(send_payload + out, "auth-agent@openssh.com", 22);
    write_u32(send_payload + out, channel->server_channel);
    out += 4;
    write_u32(send_payload + out, channel->local_window);
    out += 4;
    write_u32(send_payload + out, 32768);
    out += 4;
    send_message(send_payload, out);
    return true;
}

bool ssh_agent_connected(void) {
    ssh_agent_poll();
    return agent_channel && channel_valid(agent_channel)
        && agent_channel->confirmed;
}

int32_t ssh_agent_read(uint8_t *data, uint32_t capacity, uint32_t timeout_ms) {
    if (!data || !capacity) return -1;
    uint32_t target = timer_ticks() + timeout_ms / 10;
    for (;;) {
        ssh_agent_poll();
        if (agent_rx_length) {
            uint32_t count = agent_rx_length < capacity
                           ? agent_rx_length : capacity;
            memcpy(data, agent_rx, count);
            memmove(agent_rx, agent_rx + count, agent_rx_length - count);
            agent_rx_length -= count;
            return (int32_t)count;
        }
        if (!agent_channel || agent_rx_eof) return 0;
        if (timeout_ms && (int32_t)(timer_ticks() - target) >= 0) return -1;
        network_poll();
        cpu_pause();
    }
}

bool ssh_agent_readable(void) {
    ssh_agent_poll();
    return agent_rx_length > 0 || !agent_channel || agent_rx_eof;
}

int32_t ssh_agent_read_nonblock(uint8_t *data, uint32_t capacity) {
    if (!data || !capacity) return -1;
    ssh_agent_poll();
    if (!agent_rx_length) {
        if (!agent_channel || agent_rx_eof) return 0;
        return -1;
    }
    uint32_t count = agent_rx_length < capacity ? agent_rx_length : capacity;
    memcpy(data, agent_rx, count);
    memmove(agent_rx, agent_rx + count, agent_rx_length - count);
    agent_rx_length -= count;
    return (int32_t)count;
}

bool ssh_agent_write(const uint8_t *data, uint32_t length) {
    if (!agent_channel || !channel_valid(agent_channel)
        || !agent_channel->confirmed || !data) {
        return false;
    }
    send_channel_data(agent_channel, data, length);
    return channel_valid(agent_channel);
}

void ssh_agent_close(void) {
    if (agent_channel && channel_valid(agent_channel)) {
        channel_close(agent_channel, true);
    }
    agent_channel = 0;
    agent_rx_length = 0;
    agent_rx_eof = true;
}

bool ssh_agent_request_identities(uint32_t *count) {
    if (count) *count = 0;
    if (!ssh_agent_connect()) return false;
    const uint8_t request[5] = {0, 0, 0, 1, 11};
    if (!ssh_agent_write(request, sizeof(request))) {
        ssh_agent_close();
        return false;
    }
    uint8_t frame_length[4];
    uint32_t received = 0;
    while (received < sizeof(frame_length)) {
        int32_t part = ssh_agent_read(frame_length + received,
                                      sizeof(frame_length) - received, 5000);
        if (part <= 0) {
            ssh_agent_close();
            return false;
        }
        received += (uint32_t)part;
    }
    uint32_t response_length = read_u32(frame_length);
    if (response_length < 1 || response_length > SSH_AGENT_RX_CAPACITY) {
        ssh_agent_close();
        return false;
    }
    uint8_t response[5];
    received = 0;
    uint32_t wanted = response_length < sizeof(response)
                    ? response_length : sizeof(response);
    while (received < wanted) {
        int32_t part = ssh_agent_read(response + received,
                                      wanted - received, 5000);
        if (part <= 0) {
            ssh_agent_close();
            return false;
        }
        received += (uint32_t)part;
    }
    ssh_agent_close();
    if (response[0] != 12 || response_length < 5) return false;
    if (count) *count = read_u32(response + 1);
    return true;
}

bool ssh_init(void) {
    input = (uint8_t *)kmalloc(SSH_MAX_PACKET);
    send_payload = (uint8_t *)kmalloc(SSH_MAX_PACKET);
    packet_buffer = (uint8_t *)kmalloc(SSH_MAX_PACKET + 64);
    mac_buffer = (uint8_t *)kmalloc(SSH_MAX_PACKET + 4);
    sftp_response = (uint8_t *)kmalloc(SFTP_BODY_MAX);
    channels = (ssh_channel *)kmalloc(sizeof(*channels) * SSH_CHANNEL_LIMIT);
    agent_rx = (uint8_t *)kmalloc(SSH_AGENT_RX_CAPACITY);
    if (!input || !send_payload || !packet_buffer || !mac_buffer
        || !sftp_response || !channels || !agent_rx) {
        return false;
    }
    agent_rx_length = 0;
    agent_rx_eof = true;
    memset(channels, 0, sizeof(*channels) * SSH_CHANNEL_LIMIT);
    for (uint32_t index = 0; index < REMOTE_FORWARD_LIMIT; index++) {
        remote_forwards[index].listener = -1;
    }
    phase = SSH_PHASE_VERSION;
    ssh_control_stream = -1;
    ssh_control_listener = network_stream_listen(22, ssh_stream_event, 0);
    return ssh_control_listener >= 0;
}

void ssh_poll(void) {
    ssh_agent_poll();
}

bool ssh_console_active(void) {
    return active_console_channel() && shell_active;
}

void ssh_console_putc(char character) {
    ssh_channel *channel = active_console_channel();
    if (!channel || !shell_active) return;
    send_channel_data(channel, (const uint8_t *)&character, 1);
    if (remote_exec_active) {
        if (remote_output_length < sizeof(remote_output_tail) - 1) {
            remote_output_tail[remote_output_length++] = character;
        } else {
            memmove(remote_output_tail, remote_output_tail + 1,
                    sizeof(remote_output_tail) - 2);
            remote_output_tail[sizeof(remote_output_tail) - 2] = character;
        }
        remote_output_tail[remote_output_length] = '\0';
        if (remote_output_length == 7
            && memcmp(remote_output_tail, "CKLOS> ", 7) == 0) {
            close_remote_channel();
        }
    }
}

bool ssh_console_read(char *character) {
    if (console_input_head >= SSH_INPUT_SIZE
        || console_input_tail >= SSH_INPUT_SIZE) {
        console_input_head = console_input_tail = 0;
        return false;
    }
    if (console_input_tail == console_input_head) {
        return false;
    }
    *character = (char)console_input[console_input_tail];
    console_input_tail = (console_input_tail + 1) % SSH_INPUT_SIZE;
    return true;
}

const char *ssh_status(void) {
    if (!network_available()) {
        return "offline";
    }
    return status_text;
}
