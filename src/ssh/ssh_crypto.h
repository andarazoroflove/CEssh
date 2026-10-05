#ifndef CESSH_SSH_CRYPTO_H
#define CESSH_SSH_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef PALMOS
#include <PalmOS.h>
#ifndef bool
#define bool Boolean
#endif
#else
#include <stdbool.h>
#endif

#include "bearssl_hash.h"
#include "bearssl_hmac.h"
#include "bearssl_block.h"
#include "bearssl_ec.h"

/* AES-CTR Cipher Context (RFC 4344 128-bit streaming) */
typedef struct {
    br_aes_big_ctr_keys key;
    uint8_t ctr[16];
    uint8_t pad[16];
    size_t  pad_idx;
} ssh_cipher_ctx_t;

/* HMAC Context */
typedef struct {
    br_hmac_key_context kctx;
    br_hmac_context     hctx;
    size_t              mac_len;
} ssh_mac_ctx_t;

void ssh_random_bytes(uint8_t *buf, size_t len);

void ssh_cipher_init(ssh_cipher_ctx_t *c, const uint8_t *key, const uint8_t *iv);
void ssh_cipher_crypt(ssh_cipher_ctx_t *c, uint8_t *data, size_t len);

void ssh_mac_init(ssh_mac_ctx_t *m, const uint8_t *key, size_t key_len);
void ssh_mac_compute(ssh_mac_ctx_t *m, uint32_t seq, const uint8_t *data, size_t len, uint8_t *out_tag);

void ssh_kdf(const uint8_t *k, size_t k_len,
             const uint8_t *h, size_t h_len,
             char x,
             const uint8_t *session_id, size_t session_id_len,
             uint8_t *out, size_t out_len);

void ssh_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

bool ssh_curve25519_mulgen(uint8_t pub[32], const uint8_t priv[32]);
bool ssh_curve25519_mul(uint8_t shared[32], const uint8_t pub[32], const uint8_t priv[32]);

#endif /* CESSH_SSH_CRYPTO_H */
