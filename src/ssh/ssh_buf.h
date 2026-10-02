#ifndef CESSH_SSH_BUF_H
#define CESSH_SSH_BUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    size_t   pos;
} ssh_buf_t;

void ssh_buf_init(ssh_buf_t *b, uint8_t *data, size_t cap);
void ssh_buf_init_read(ssh_buf_t *b, const uint8_t *data, size_t len);

bool ssh_put_u8(ssh_buf_t *b, uint8_t v);
bool ssh_put_u32(ssh_buf_t *b, uint32_t v);
bool ssh_put_raw(ssh_buf_t *b, const void *src, size_t len);
bool ssh_put_string(ssh_buf_t *b, const void *src, size_t len);
bool ssh_put_str(ssh_buf_t *b, const char *s);
bool ssh_put_mpint(ssh_buf_t *b, const uint8_t *be_bytes, size_t len);

bool ssh_get_u8(ssh_buf_t *b, uint8_t *out);
bool ssh_get_u32(ssh_buf_t *b, uint32_t *out);
bool ssh_get_raw(ssh_buf_t *b, void *dst, size_t len);
bool ssh_get_string(ssh_buf_t *b, const uint8_t **out_data, size_t *out_len);

#endif /* CESSH_SSH_BUF_H */
