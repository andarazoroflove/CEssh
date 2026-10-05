#ifndef CESSH_TERMINAL_H
#define CESSH_TERMINAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TERM_MAX_COLS 80
#define TERM_MAX_ROWS 64

#define TERM_COLS term_get_cols()
#define TERM_ROWS term_get_rows()

/* Color Palette */
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_BLACK   0x00000000
#define COLOR_GRAY    0x00808080
#define COLOR_LTGRAY  0x00D0D0D0
#define COLOR_DKGRAY  0x00404040

typedef struct {
    char     ch;
    uint32_t fg;
    uint32_t bg;
    uint8_t  attr; /* 1 = bold, 2 = underline, 4 = reverse */
} term_cell_t;

void term_init(void);
void term_set_size(int cols, int rows);
int  term_get_cols(void);
int  term_get_rows(void);
void term_clear(void);
void term_putc(char c);
void term_write(const char *data, size_t len);
void term_puts(const char *str);
void term_printf(const char *fmt, ...);

void term_get_cursor(int *col, int *row);
void term_set_cursor(int col, int row);

void term_set_fg(uint32_t color);
void term_set_bg(uint32_t color);
void term_toggle_invert(void);
bool term_is_inverted(void);
void term_set_cursor_visible(bool visible);

void term_set_watermark(bool enabled);
bool term_get_watermark(void);
void term_set_watermark_text(const char *text);
void term_render_cell(int col, int row, char ch, uint32_t fg, uint32_t bg, uint8_t attr);

void term_render(uint32_t *framebuffer, int width, int height, int pitch_pixels);

#endif /* CESSH_TERMINAL_H */
