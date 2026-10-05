#include "ssh_buf.h"

void ssh_buf_init(ssh_buf_t *b, uint8_t *data, size_t cap) {
    b->data = data;
    b->len  = 0;
    b->cap  = cap;
    b->pos  = 0;
}

void ssh_buf_init_read(ssh_buf_t *b, const uint8_t *data, size_t len) {
    b->data = (uint8_t *)data;
    b->len  = len;
    b->cap  = len;
    b->pos  = 0;
}

bool ssh_put_u8(ssh_buf_t *b, uint8_t v) {
    if (b->len + 1 > b->cap) return false;
    b->data[b->len++] = v;
    return true;
}

bool ssh_put_u32(ssh_buf_t *b, uint32_t v) {
    if (b->len + 4 > b->cap) return false;
    b->data[b->len++] = (uint8_t)(v >> 24);
    b->data[b->len++] = (uint8_t)(v >> 16);
    b->data[b->len++] = (uint8_t)(v >> 8);
    b->data[b->len++] = (uint8_t)v;
    return true;
}

bool ssh_put_raw(ssh_buf_t *b, const void *src, size_t len) {
    if (b->len + len > b->cap) return false;
    memcpy(b->data + b->len, src, len);
    b->len += len;
    return true;
}

bool ssh_put_string(ssh_buf_t *b, const void *src, size_t len) {
    if (!ssh_put_u32(b, (uint32_t)len)) return false;
    return ssh_put_raw(b, src, len);
}

bool ssh_put_str(ssh_buf_t *b, const char *s) {
    size_t slen = s ? strlen(s) : 0;
    return ssh_put_string(b, s, slen);
}

bool ssh_put_mpint(ssh_buf_t *b, const uint8_t *be_bytes, size_t len) {
    size_t i = 0;
    const uint8_t *val;
    size_t val_len;

    /* Skip leading zeroes */
    while (i < len && be_bytes[i] == 0) i++;
    if (i == len) {
        /* Value is zero: 00 00 00 00 */
        return ssh_put_u32(b, 0);
    }

    val = be_bytes + i;
    val_len = len - i;

    /* If high bit set, prefix with 0x00 */
    if (val[0] & 0x80) {
        if (!ssh_put_u32(b, (uint32_t)(val_len + 1))) return false;
        if (!ssh_put_u8(b, 0x00)) return false;
    } else {
        if (!ssh_put_u32(b, (uint32_t)val_len)) return false;
    }
    return ssh_put_raw(b, val, val_len);
}

bool ssh_get_u8(ssh_buf_t *b, uint8_t *out) {
    if (b->pos + 1 > b->len) return false;
    *out = b->data[b->pos++];
    return true;
}

bool ssh_get_u32(ssh_buf_t *b, uint32_t *out) {
    if (b->pos + 4 > b->len) return false;
    *out = ((uint32_t)b->data[b->pos] << 24) |
           ((uint32_t)b->data[b->pos + 1] << 16) |
           ((uint32_t)b->data[b->pos + 2] << 8) |
           ((uint32_t)b->data[b->pos + 3]);
    b->pos += 4;
    return true;
}

bool ssh_get_raw(ssh_buf_t *b, void *dst, size_t len) {
    if (b->pos + len > b->len) return false;
    memcpy(dst, b->data + b->pos, len);
    b->pos += len;
    return true;
}

bool ssh_get_string(ssh_buf_t *b, const uint8_t **out_data, size_t *out_len) {
    uint32_t slen = 0;
    if (!ssh_get_u32(b, &slen)) return false;
    if (b->pos + slen > b->len) return false;
    *out_data = b->data + b->pos;
    *out_len = slen;
    b->pos += slen;
    return true;
}
