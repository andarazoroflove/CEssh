#ifndef CESSH_FONT_H
#define CESSH_FONT_H

#include <stdint.h>

#define FONT_CHAR_W  8
#define FONT_CHAR_H 10

/* 8x8 Font matrix for ASCII 32 (' ') to 126 ('~') */
extern const uint8_t font8x8_basic[95][8];

#endif /* CESSH_FONT_H */
