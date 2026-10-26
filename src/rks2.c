#include "fs2.h"
#include "memory.h"
#include "rks2.h"
#include "string.h"

#include "bearssl_hash.h"

typedef struct {
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint64_t payload_size;
    uint8_t sha256[32];
    uint32_t flags;
    uint8_t reserved[12];
} __attribute__((packed)) rks2_header;

bool rks2_is_container(const uint8_t *data, uint32_t size) {
    if (!data || size < RKS2_HEADER_SIZE
        || memcmp(data, "RKS2", 4) != 0) {
        return false;
    }
    const rks2_header *header = (const rks2_header *)data;
    return header->version == 2
        && header->header_size == RKS2_HEADER_SIZE;
}

static void hash_payload(const uint8_t *payload, uint32_t size, uint8_t hash[32]) {
    br_sha256_context context;
    br_sha256_init(&context);
    br_sha256_update(&context, payload, size);
    br_sha256_out(&context, hash);
}

bool rks2_wrap_file(const char *path) {
    uint32_t size;
    if (fs2_stat(path, &size, 0) != 0 || size < 4 || size > RKS2_MAX_PAYLOAD) {
        return false;
    }
    uint8_t *payload = (uint8_t *)kmalloc(size);
    if (!payload) return false;
    uint32_t actual;
    if (fs2_read(path, payload, size, &actual) != 0 || actual != size
        || memcmp(payload, "\x7F" "ELF", 4) != 0) {
        kfree(payload);
        return false;
    }
    uint8_t *container = (uint8_t *)kmalloc(RKS2_HEADER_SIZE + size);
    if (!container) {
        kfree(payload);
        return false;
    }
    memset(container, 0, RKS2_HEADER_SIZE);
    rks2_header *header = (rks2_header *)container;
    memcpy(header->magic, "RKS2", 4);
    header->version = 2;
    header->header_size = RKS2_HEADER_SIZE;
    header->payload_size = size;
    header->flags = 1;
    hash_payload(payload, size, header->sha256);
    memcpy(container + RKS2_HEADER_SIZE, payload, size);
    bool result = fs2_write(path, container, RKS2_HEADER_SIZE + size, false) == 0;
    kfree(container);
    kfree(payload);
    return result;
}

bool rks2_extract(const char *path, uint8_t **payload, uint32_t *size) {
    uint32_t file_size;
    if (fs2_stat(path, &file_size, 0) != 0
        || file_size < RKS2_HEADER_SIZE
        || file_size > RKS2_HEADER_SIZE + RKS2_MAX_PAYLOAD) {
        return false;
    }
    uint8_t *container = (uint8_t *)kmalloc(file_size);
    if (!container) return false;
    uint32_t actual;
    if (fs2_read(path, container, file_size, &actual) != 0
        || actual != file_size || !rks2_is_container(container, file_size)) {
        kfree(container);
        return false;
    }
    const rks2_header *header = (const rks2_header *)container;
    uint64_t payload_size = header->payload_size;
    if (payload_size < 4
        || payload_size != file_size - RKS2_HEADER_SIZE
        || memcmp(container + RKS2_HEADER_SIZE, "\x7F" "ELF", 4) != 0) {
        kfree(container);
        return false;
    }
    uint8_t digest[32];
    hash_payload(container + RKS2_HEADER_SIZE, (uint32_t)payload_size, digest);
    if (memcmp(digest, header->sha256, sizeof(digest)) != 0) {
        kfree(container);
        return false;
    }
    uint8_t *result = (uint8_t *)kmalloc((uint32_t)payload_size);
    if (!result) {
        kfree(container);
        return false;
    }
    memcpy(result, container + RKS2_HEADER_SIZE, (uint32_t)payload_size);
    kfree(container);
    *payload = result;
    *size = (uint32_t)payload_size;
    return true;
}
