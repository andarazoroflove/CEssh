#ifndef CESSH_FREESTANDING_H
#define CESSH_FREESTANDING_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

int cessh_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int cessh_snprintf(char *buf, size_t size, const char *fmt, ...);
int cessh_sprintf(char *buf, const char *fmt, ...);

#endif /* CESSH_FREESTANDING_H */
