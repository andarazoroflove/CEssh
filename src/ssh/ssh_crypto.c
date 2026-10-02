#include <windows.h>
#include <string.h>

#include "ssh_crypto.h"

static uint64_t s_rng_state = 0x9e3779b97f4a7c15ULL;

void ssh_random_bytes(uint8_t *buf, size_t len) {
    if (s_rng_state == 0x9e3779b97f4a7c15ULL) {
        LARGE_INTEGER qpc;
        if (QueryPerformanceCounter(&qpc)) {
            s_rng_state ^= (uint64_t)qpc.QuadPart;
        }
        s_rng_state ^= ((uint64_t)GetTickCount() << 32) | (uintptr_t)buf;
    }

    for (size_t i = 0; i < len; i++) {
        /* 64-bit Xorshift* PRNG */
        s_rng_state ^= s_rng_state >> 12;
        s_rng_state ^= s_rng_state << 25;
        s_rng_state ^= s_rng_state >> 27;
        uint64_t val = s_rng_state * 0x2545F4914F6CDD1DULL;
        buf[i] = (uint8_t)(val >> 32);
    }
}

void br_aes_big_encrypt(unsigned num_rounds, const uint32_t *skey, void *data);

static void inc_ctr128(uint8_t *ctr) {
    for (int i = 15; i >= 0; i--) {
        if (++ctr[i] != 0) break;
    }
}

void ssh_cipher_init(ssh_cipher_ctx_t *c, const uint8_t *key, const uint8_t *iv) {
    br_aes_big_ctr_init(&c->key, key, 16);
    memcpy(c->ctr, iv, 16);
    c->pad_idx = 16;
}

void ssh_cipher_crypt(ssh_cipher_ctx_t *c, uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (c->pad_idx >= 16) {
            memcpy(c->pad, c->ctr, 16);
            br_aes_big_encrypt(c->key.num_rounds, c->key.skey, c->pad);
            inc_ctr128(c->ctr);
            c->pad_idx = 0;
        }
        data[i] ^= c->pad[c->pad_idx++];
    }
}

void ssh_mac_init(ssh_mac_ctx_t *m, const uint8_t *key, size_t key_len) {
    m->mac_len = 32;
    br_hmac_key_init(&m->kctx, &br_sha256_vtable, key, key_len);
}

void ssh_mac_compute(ssh_mac_ctx_t *m, uint32_t seq, const uint8_t *data, size_t len, uint8_t *out_tag) {
    br_hmac_init(&m->hctx, &m->kctx, 0);

    uint8_t seq_bytes[4];
    seq_bytes[0] = (uint8_t)(seq >> 24);
    seq_bytes[1] = (uint8_t)(seq >> 16);
    seq_bytes[2] = (uint8_t)(seq >> 8);
    seq_bytes[3] = (uint8_t)seq;

    br_hmac_update(&m->hctx, seq_bytes, 4);
    br_hmac_update(&m->hctx, data, len);
    br_hmac_out(&m->hctx, out_tag);
}

void ssh_kdf(const uint8_t *k, size_t k_len,
             const uint8_t *h, size_t h_len,
             char x,
             const uint8_t *session_id, size_t session_id_len,
             uint8_t *out, size_t out_len)
{
    br_sha256_context ctx;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, k, k_len);
    br_sha256_update(&ctx, h, h_len);
    br_sha256_update(&ctx, &x, 1);
    br_sha256_update(&ctx, session_id, session_id_len);

    uint8_t digest[32];
    br_sha256_out(&ctx, digest);

    size_t copy_first = (out_len < 32) ? out_len : 32;
    memcpy(out, digest, copy_first);

    size_t generated = copy_first;
    while (generated < out_len) {
        br_sha256_init(&ctx);
        br_sha256_update(&ctx, k, k_len);
        br_sha256_update(&ctx, h, h_len);
        br_sha256_update(&ctx, digest, 32);
        br_sha256_out(&ctx, digest);

        size_t to_copy = out_len - generated;
        if (to_copy > 32) to_copy = 32;
        memcpy(out + generated, digest, to_copy);
        generated += to_copy;
    }
}

void ssh_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    br_sha256_context ctx;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, data, len);
    br_sha256_out(&ctx, out);
}

bool ssh_curve25519_mulgen(uint8_t pub[32], const uint8_t priv[32]) {
    br_ec_c25519_m31.mulgen(pub, priv, 32, BR_EC_curve25519);
    return true;
}

bool ssh_curve25519_mul(uint8_t shared[32], const uint8_t pub[32], const uint8_t priv[32]) {
    memcpy(shared, pub, 32);
    br_ec_c25519_m31.mul(shared, 32, priv, 32, BR_EC_curve25519);
    return true;
}
