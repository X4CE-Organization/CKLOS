#ifndef CKLOS_CRYPTO_H
#define CKLOS_CRYPTO_H

#include "types.h"

typedef struct {
    uint8_t key[32];
    uint8_t iv[12];
    uint32_t counter;
    void *schedule;
} cklos_aes_ctr;

void cklos_sha256(const void *data, size_t length, uint8_t output[32]);
void cklos_hmac_sha256(const uint8_t *key, size_t key_length,
                       const void *data, size_t length, uint8_t output[32]);
void cklos_random_bytes(void *output, size_t length);

void cklos_ed25519_keypair(const uint8_t seed[32], uint8_t secret[64], uint8_t public_key[32]);
void cklos_ed25519_sign(const uint8_t secret[64], const void *message,
                        size_t length, uint8_t signature[64]);
bool cklos_ed25519_verify(const uint8_t public_key[32], const void *message,
                          size_t length, const uint8_t signature[64]);
void cklos_x25519_public(const uint8_t secret[32], uint8_t public_key[32]);
bool cklos_x25519_shared(const uint8_t secret[32], const uint8_t public_key[32],
                         uint8_t shared[32]);

void cklos_aes_ctr_init(cklos_aes_ctr *context, const uint8_t key[16],
                        const uint8_t iv[16]);
void cklos_aes_ctr_crypt(cklos_aes_ctr *context, void *data, size_t length);

#endif
