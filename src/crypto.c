#include "crypto.h"
#include "interrupts.h"
#include "io.h"
#include "memory.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

#include "bearssl.h"

typedef struct {
    br_aes_ct64_ctr_keys keys;
} aes_schedule;

void cklos_sha256(const void *data, size_t length, uint8_t output[32]) {
    br_sha256_context context;
    br_sha256_init(&context);
    br_sha256_update(&context, data, length);
    br_sha256_out(&context, output);
}

void cklos_hmac_sha256(const uint8_t *key, size_t key_length,
                       const void *data, size_t length, uint8_t output[32]) {
    br_hmac_key_context key_context;
    br_hmac_context context;
    br_hmac_key_init(&key_context, &br_sha256_vtable, key, key_length);
    br_hmac_init(&context, &key_context, 32);
    br_hmac_update(&context, data, length);
    br_hmac_out(&context, output);
}

void cklos_random_bytes(void *output, size_t length) {
    uint8_t seed[64];
    uint32_t *words = (uint32_t *)seed;
    for (uint32_t index = 0; index < 16; index++) {
        uint32_t low;
        uint32_t high;
        __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
        words[index] = low ^ high ^ timer_ticks()
                     ^ (uint32_t)(uintptr_t)output ^ index;
        cpu_pause();
    }
    br_hmac_drbg_context drbg;
    br_hmac_drbg_init(&drbg, &br_sha256_vtable, seed, sizeof(seed));
    br_hmac_drbg_generate(&drbg, output, length);
    memset(seed, 0, sizeof(seed));
}

void cklos_ed25519_keypair(const uint8_t seed[32], uint8_t secret[64], uint8_t public_key[32]) {
    uint8_t local_seed[32];
    memcpy(local_seed, seed, 32);
    crypto_ed25519_key_pair(secret, public_key, local_seed);
    memset(local_seed, 0, sizeof(local_seed));
}

void cklos_ed25519_sign(const uint8_t secret[64], const void *message,
                        size_t length, uint8_t signature[64]) {
    crypto_ed25519_sign(signature, secret, (const uint8_t *)message, length);
}

bool cklos_ed25519_verify(const uint8_t public_key[32], const void *message,
                          size_t length, const uint8_t signature[64]) {
    return crypto_ed25519_check(signature, public_key,
                                (const uint8_t *)message, length) == 0;
}

void cklos_x25519_public(const uint8_t secret[32], uint8_t public_key[32]) {
    crypto_x25519_public_key(public_key, secret);
}

bool cklos_x25519_shared(const uint8_t secret[32], const uint8_t public_key[32],
                         uint8_t shared[32]) {
    crypto_x25519(shared, secret, public_key);
    uint8_t accumulator = 0;
    for (uint32_t index = 0; index < 32; index++) {
        accumulator |= shared[index];
    }
    return accumulator != 0;
}

void cklos_aes_ctr_init(cklos_aes_ctr *context, const uint8_t key[16],
                        const uint8_t iv[16]) {
    memcpy(context->key, key, 16);
    memcpy(context->iv, iv, 12);
    context->counter = (uint32_t)iv[12] << 24 | (uint32_t)iv[13] << 16
                     | (uint32_t)iv[14] << 8 | iv[15];
    context->schedule = kmalloc(sizeof(aes_schedule));
    if (context->schedule) {
        br_aes_ct64_ctr_init(&((aes_schedule *)context->schedule)->keys, key, 16);
    }
}

void cklos_aes_ctr_crypt(cklos_aes_ctr *context, void *data, size_t length) {
    if (!context->schedule) {
        return;
    }
    uint32_t next = br_aes_ct64_ctr_run(&((aes_schedule *)context->schedule)->keys,
                                        context->iv, context->counter, data, length);
    if (next < context->counter) {
        for (int index = 11; index >= 0; index--) {
            if (++context->iv[index]) {
                break;
            }
        }
    }
    context->counter = next;
}
