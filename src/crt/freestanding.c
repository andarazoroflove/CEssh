#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include "freestanding.h"

static void emit_char(char **buf, size_t *rem, size_t *total, char c) {
    if (buf && *buf) {
        if (*rem > 1) {
            **buf = c;
            (*buf)++;
            (*rem)--;
        }
    }
    (*total)++;
}

static void print_uint(char **buf, size_t *rem, size_t *total, uint64_t val, int base, int width, char pad, bool uppercase) {
    char num_buf[65];
    int idx = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        num_buf[idx++] = '0';
    } else {
        while (val > 0) {
            num_buf[idx++] = digits[val % (uint64_t)base];
            val /= (uint64_t)base;
        }
    }

    while (idx < width) {
        num_buf[idx++] = pad;
    }

    for (int i = idx - 1; i >= 0; i--) {
        emit_char(buf, rem, total, num_buf[i]);
    }
}

static void print_int(char **buf, size_t *rem, size_t *total, int64_t val, int width, char pad) {
    if (val < 0) {
        emit_char(buf, rem, total, '-');
        val = -val;
        if (width > 0) width--;
    }
    print_uint(buf, rem, total, (uint64_t)val, 10, width, pad, false);
}

int cessh_vsnprintf(char *out_buf, size_t max_size, const char *fmt, va_list args) {
    char *buf_ptr = out_buf;
    size_t rem = max_size;
    size_t total = 0;

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, fmt[i]);
            continue;
        }

        i++;
        char pad = ' ';
        int width = 0;
        bool is_long = false;
        bool is_longlong = false;

        if (fmt[i] == '0') {
            pad = '0';
            i++;
        }

        while (fmt[i] >= '0' && fmt[i] <= '9') {
            width = width * 10 + (fmt[i] - '0');
            i++;
        }

        if (fmt[i] == 'l') {
            is_long = true;
            i++;
            if (fmt[i] == 'l') {
                is_longlong = true;
                i++;
            }
        } else if (fmt[i] == 'z') {
            is_long = (sizeof(size_t) == sizeof(long));
            is_longlong = (sizeof(size_t) == sizeof(long long));
            i++;
        }

        switch (fmt[i]) {
            case 'd':
            case 'i': {
                int64_t v;
                if (is_longlong) v = va_arg(args, long long);
                else if (is_long) v = va_arg(args, long);
                else v = va_arg(args, int);
                print_int(out_buf ? &buf_ptr : NULL, &rem, &total, v, width, pad);
                break;
            }
            case 'u': {
                uint64_t v;
                if (is_longlong) v = va_arg(args, unsigned long long);
                else if (is_long) v = va_arg(args, unsigned long);
                else v = va_arg(args, unsigned int);
                print_uint(out_buf ? &buf_ptr : NULL, &rem, &total, v, 10, width, pad, false);
                break;
            }
            case 'x': {
                uint64_t v;
                if (is_longlong) v = va_arg(args, unsigned long long);
                else if (is_long) v = va_arg(args, unsigned long);
                else v = va_arg(args, unsigned int);
                print_uint(out_buf ? &buf_ptr : NULL, &rem, &total, v, 16, width, pad, false);
                break;
            }
            case 'X': {
                uint64_t v;
                if (is_longlong) v = va_arg(args, unsigned long long);
                else if (is_long) v = va_arg(args, unsigned long);
                else v = va_arg(args, unsigned int);
                print_uint(out_buf ? &buf_ptr : NULL, &rem, &total, v, 16, width, pad, true);
                break;
            }
            case 'p': {
                uintptr_t v = (uintptr_t)va_arg(args, void *);
                emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, '0');
                emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, 'x');
                print_uint(out_buf ? &buf_ptr : NULL, &rem, &total, v, 16, sizeof(void *) * 2, '0', false);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, c);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                int slen = 0;
                while (s[slen]) slen++;
                while (slen < width) {
                    emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, ' ');
                    width--;
                }
                while (*s) {
                    emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, *s++);
                }
                break;
            }
            case '%':
                emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, '%');
                break;
            default:
                emit_char(out_buf ? &buf_ptr : NULL, &rem, &total, fmt[i]);
                break;
        }
    }

    if (out_buf && max_size > 0) {
        if (rem > 0) {
            *buf_ptr = '\0';
        } else {
            out_buf[max_size - 1] = '\0';
        }
    }

    return (int)total;
}

int cessh_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = cessh_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int cessh_sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = cessh_vsnprintf(buf, 0x7FFFFFFF, fmt, args);
    va_end(args);
    return ret;
}

/* Freestanding CRT Overrides */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    return cessh_vsnprintf(buf, size, fmt, args);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = cessh_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = cessh_vsnprintf(buf, 0x7FFFFFFF, fmt, args);
    va_end(args);
    return ret;
}

int _vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    return cessh_vsnprintf(buf, size, fmt, args);
}

int _snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = cessh_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int atoi(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}
