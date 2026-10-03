#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "terminal.h"
#include "font.h"

static term_cell_t s_grid[TERM_ROWS][TERM_COLS];
static int s_cursor_col = 0;
static int s_cursor_row = 0;
static uint32_t s_cur_fg = COLOR_BLACK;
static uint32_t s_cur_bg = COLOR_WHITE;
static uint8_t  s_cur_attr = 0;
static bool s_inverted_mode = false;
static bool s_cursor_visible = true;

/* ANSI parser state */
enum {
    STATE_NORMAL = 0,
    STATE_ESC,
    STATE_CSI
};

static int s_parser_state = STATE_NORMAL;
static char s_csi_buf[32];
static size_t s_csi_pos = 0;

/* High contrast ANSI colors for white background */
static const uint32_t s_ansi_colors[16] = {
    0x00000000, /* 0: Black */
    0x00B22222, /* 1: Firebrick Red */
    0x00006400, /* 2: Dark Green */
    0x008B4513, /* 3: Saddle Brown / Olive */
    0x000000CD, /* 4: Medium Blue */
    0x008B008B, /* 5: Dark Magenta */
    0x00008B8B, /* 6: Dark Cyan */
    0x00A0A0A0, /* 7: Medium Gray */
    0x00606060, /* 8: Dim Gray */
    0x00FF0000, /* 9: Bright Red */
    0x00008000, /* 10: Bright Green */
    0x00B8860B, /* 11: Dark Goldenrod */
    0x001E90FF, /* 12: Dodger Blue */
    0x00BA55D3, /* 13: Medium Orchid */
    0x0020B2AA, /* 14: Light Sea Green */
    0x00FFFFFF  /* 15: White */
};

static bool s_watermark_enabled = true;
static char s_watermark_title[80] = "CEssh Suite 0.9a";

void term_set_watermark(bool enabled) {
    s_watermark_enabled = enabled;
}

bool term_get_watermark(void) {
    return s_watermark_enabled;
}

void term_set_watermark_text(const char *text) {
    if (text) {
        strncpy(s_watermark_title, text, sizeof(s_watermark_title) - 1);
        s_watermark_title[sizeof(s_watermark_title) - 1] = '\0';
    }
}

void term_render_cell(int col, int row, char ch, uint32_t fg, uint32_t bg, uint8_t attr) {
    if (col < 0 || col >= TERM_COLS || row < 0 || row >= TERM_ROWS) return;
    s_grid[row][col].ch = ch;
    s_grid[row][col].fg = fg;
    s_grid[row][col].bg = bg;
    s_grid[row][col].attr = attr;
}

static void draw_watermark(void) {
    if (!s_watermark_enabled) return;
    /* Row 0: Watermark title, e.g. "CEssh Suite 0.9a" */
    for (int c = 0; c < TERM_COLS; c++) {
        s_grid[0][c].ch = ' ';
        s_grid[0][c].fg = COLOR_DKGRAY;
        s_grid[0][c].bg = COLOR_LTGRAY;
        s_grid[0][c].attr = 1; /* bold */
    }
    size_t wlen = strlen(s_watermark_title);
    int pad = (TERM_COLS > (int)wlen) ? (TERM_COLS - (int)wlen) / 2 : 2;
    for (size_t i = 0; i < wlen && (pad + (int)i) < TERM_COLS; i++) {
        s_grid[0][pad + i].ch = s_watermark_title[i];
    }

    /* Row 1: Line of dashes */
    for (int c = 0; c < TERM_COLS; c++) {
        s_grid[1][c].ch = '-';
        s_grid[1][c].fg = COLOR_GRAY;
        s_grid[1][c].bg = COLOR_WHITE;
        s_grid[1][c].attr = 0;
    }
}

static void scroll_up(void) {
    if (s_watermark_enabled) {
        memmove(&s_grid[2][0], &s_grid[3][0], sizeof(term_cell_t) * TERM_COLS * (TERM_ROWS - 3));
        for (int col = 0; col < TERM_COLS; col++) {
            s_grid[TERM_ROWS - 1][col].ch = ' ';
            s_grid[TERM_ROWS - 1][col].fg = s_cur_fg;
            s_grid[TERM_ROWS - 1][col].bg = s_cur_bg;
            s_grid[TERM_ROWS - 1][col].attr = 0;
        }
        return;
    }
    memmove(&s_grid[0][0], &s_grid[1][0], sizeof(term_cell_t) * TERM_COLS * (TERM_ROWS - 1));
    for (int col = 0; col < TERM_COLS; col++) {
        s_grid[TERM_ROWS - 1][col].ch = ' ';
        s_grid[TERM_ROWS - 1][col].fg = s_cur_fg;
        s_grid[TERM_ROWS - 1][col].bg = s_cur_bg;
        s_grid[TERM_ROWS - 1][col].attr = 0;
    }
}

void term_init(void) {
    s_cur_fg = COLOR_BLACK;
    s_cur_bg = COLOR_WHITE;
    s_cur_attr = 0;
    s_inverted_mode = false;
    term_clear();
}

void term_clear(void) {
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            s_grid[r][c].ch = ' ';
            s_grid[r][c].fg = s_cur_fg;
            s_grid[r][c].bg = s_cur_bg;
            s_grid[r][c].attr = 0;
        }
    }
    if (s_watermark_enabled) {
        draw_watermark();
        s_cursor_col = 0;
        s_cursor_row = 2;
    } else {
        s_cursor_col = 0;
        s_cursor_row = 0;
    }
    s_parser_state = STATE_NORMAL;
}

void term_toggle_invert(void) {
    s_inverted_mode = !s_inverted_mode;
    uint32_t old_fg = s_cur_fg;
    s_cur_fg = s_cur_bg;
    s_cur_bg = old_fg;
}

bool term_is_inverted(void) {
    return s_inverted_mode;
}

void term_set_cursor_visible(bool visible) {
    s_cursor_visible = visible;
}

void term_set_fg(uint32_t color) {
    s_cur_fg = color;
}

void term_set_bg(uint32_t color) {
    s_cur_bg = color;
}

void term_get_cursor(int *col, int *row) {
    if (col) *col = s_cursor_col;
    if (row) *row = s_cursor_row;
}

void term_set_cursor(int col, int row) {
    int min_row = s_watermark_enabled ? 2 : 0;
    if (col < 0) col = 0;
    if (col >= TERM_COLS) col = TERM_COLS - 1;
    if (row < min_row) row = min_row;
    if (row >= TERM_ROWS) row = TERM_ROWS - 1;
    s_cursor_col = col;
    s_cursor_row = row;
}

static void handle_csi(char final_cmd) {
    s_csi_buf[s_csi_pos] = '\0';
    int p1 = 0, p2 = 0;
    int parsed = sscanf(s_csi_buf, "%d;%d", &p1, &p2);

    switch (final_cmd) {
        case 'm': { /* SGR (Set Graphics Rendition) */
            char *token = strtok(s_csi_buf, ";");
            if (!token) {
                /* ESC[m resets attributes */
                s_cur_fg = s_inverted_mode ? COLOR_WHITE : COLOR_BLACK;
                s_cur_bg = s_inverted_mode ? COLOR_BLACK : COLOR_WHITE;
                s_cur_attr = 0;
                break;
            }
            while (token) {
                int code = atoi(token);
                if (code == 0) {
                    s_cur_fg = s_inverted_mode ? COLOR_WHITE : COLOR_BLACK;
                    s_cur_bg = s_inverted_mode ? COLOR_BLACK : COLOR_WHITE;
                    s_cur_attr = 0;
                } else if (code == 1) {
                    s_cur_attr |= 1; /* Bold */
                } else if (code == 4) {
                    s_cur_attr |= 2; /* Underline */
                } else if (code == 7) {
                    s_cur_attr |= 4; /* Reverse */
                } else if (code >= 30 && code <= 37) {
                    s_cur_fg = s_ansi_colors[code - 30];
                } else if (code == 39) {
                    s_cur_fg = s_inverted_mode ? COLOR_WHITE : COLOR_BLACK;
                } else if (code >= 40 && code <= 47) {
                    s_cur_bg = s_ansi_colors[code - 40];
                } else if (code == 49) {
                    s_cur_bg = s_inverted_mode ? COLOR_BLACK : COLOR_WHITE;
                } else if (code >= 90 && code <= 97) {
                    s_cur_fg = s_ansi_colors[code - 90 + 8];
                } else if (code >= 100 && code <= 107) {
                    s_cur_bg = s_ansi_colors[code - 100 + 8];
                }
                token = strtok(NULL, ";");
            }
            break;
        }

        case 'H':
        case 'f': { /* Cursor Position */
            int r = (parsed >= 1 && p1 > 0) ? (p1 - 1) : 0;
            int c = (parsed >= 2 && p2 > 0) ? (p2 - 1) : 0;
            term_set_cursor(c, r);
            break;
        }

        case 'J': { /* Erase in Display */
            if (p1 == 2) {
                term_clear();
            } else if (p1 == 0) {
                for (int c = s_cursor_col; c < TERM_COLS; c++) {
                    s_grid[s_cursor_row][c].ch = ' ';
                }
                for (int r = s_cursor_row + 1; r < TERM_ROWS; r++) {
                    for (int c = 0; c < TERM_COLS; c++) {
                        s_grid[r][c].ch = ' ';
                    }
                }
            }
            break;
        }

        case 'K': { /* Erase in Line */
            if (p1 == 0) {
                for (int c = s_cursor_col; c < TERM_COLS; c++) {
                    s_grid[s_cursor_row][c].ch = ' ';
                }
            } else if (p1 == 1) {
                for (int c = 0; c <= s_cursor_col; c++) {
                    s_grid[s_cursor_row][c].ch = ' ';
                }
            } else if (p1 == 2) {
                for (int c = 0; c < TERM_COLS; c++) {
                    s_grid[s_cursor_row][c].ch = ' ';
                }
            }
            break;
        }

        case 'A': { /* Cursor Up */
            int count = (p1 > 0) ? p1 : 1;
            s_cursor_row -= count;
            if (s_cursor_row < 0) s_cursor_row = 0;
            break;
        }

        case 'B': { /* Cursor Down */
            int count = (p1 > 0) ? p1 : 1;
            s_cursor_row += count;
            if (s_cursor_row >= TERM_ROWS) s_cursor_row = TERM_ROWS - 1;
            break;
        }

        case 'C': { /* Cursor Forward */
            int count = (p1 > 0) ? p1 : 1;
            s_cursor_col += count;
            if (s_cursor_col >= TERM_COLS) s_cursor_col = TERM_COLS - 1;
            break;
        }

        case 'D': { /* Cursor Backward */
            int count = (p1 > 0) ? p1 : 1;
            s_cursor_col -= count;
            if (s_cursor_col < 0) s_cursor_col = 0;
            break;
        }
    }
}

void term_putc(char c) {
    if (s_parser_state == STATE_ESC) {
        if (c == '[') {
            s_parser_state = STATE_CSI;
            s_csi_pos = 0;
            memset(s_csi_buf, 0, sizeof(s_csi_buf));
            return;
        } else {
            s_parser_state = STATE_NORMAL;
            /* Ignore non-CSI escape for now */
            return;
        }
    } else if (s_parser_state == STATE_CSI) {
        if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
            if (s_csi_pos < sizeof(s_csi_buf) - 1) {
                s_csi_buf[s_csi_pos++] = c;
            }
            return;
        } else {
            handle_csi(c);
            s_parser_state = STATE_NORMAL;
            return;
        }
    }

    if (c == '\x1b') {
        s_parser_state = STATE_ESC;
        return;
    }

    if (c == '\r') {
        s_cursor_col = 0;
        return;
    }

    if (c == '\n') {
        s_cursor_col = 0;
        s_cursor_row++;
        if (s_cursor_row >= TERM_ROWS) {
            scroll_up();
            s_cursor_row = TERM_ROWS - 1;
        }
        return;
    }

    if (c == '\b') {
        if (s_cursor_col > 0) {
            s_cursor_col--;
            s_grid[s_cursor_row][s_cursor_col].ch = ' ';
        }
        return;
    }

    if (c == '\t') {
        int next_tab = (s_cursor_col + 8) & ~7;
        while (s_cursor_col < next_tab && s_cursor_col < TERM_COLS) {
            s_grid[s_cursor_row][s_cursor_col].ch = ' ';
            s_grid[s_cursor_row][s_cursor_col].fg = s_cur_fg;
            s_grid[s_cursor_row][s_cursor_col].bg = s_cur_bg;
            s_grid[s_cursor_row][s_cursor_col].attr = 0;
            s_cursor_col++;
        }
        if (s_cursor_col >= TERM_COLS) {
            s_cursor_col = 0;
            s_cursor_row++;
            if (s_cursor_row >= TERM_ROWS) {
                scroll_up();
                s_cursor_row = TERM_ROWS - 1;
            }
        }
        return;
    }

    /* Regular printable character */
    if ((unsigned char)c >= 32) {
        s_grid[s_cursor_row][s_cursor_col].ch = c;
        s_grid[s_cursor_row][s_cursor_col].fg = s_cur_fg;
        s_grid[s_cursor_row][s_cursor_col].bg = s_cur_bg;
        s_grid[s_cursor_row][s_cursor_col].attr = s_cur_attr;

        s_cursor_col++;
        if (s_cursor_col >= TERM_COLS) {
            s_cursor_col = 0;
            s_cursor_row++;
            if (s_cursor_row >= TERM_ROWS) {
                scroll_up();
                s_cursor_row = TERM_ROWS - 1;
            }
        }
    }
}

void term_write(const char *data, size_t len) {
    if (!data) return;
    for (size_t i = 0; i < len; i++) {
        term_putc(data[i]);
    }
}

void term_puts(const char *str) {
    if (!str) return;
    while (*str) {
        term_putc(*str++);
    }
}

void term_printf(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    term_puts(buf);
}

void term_render(uint32_t *framebuffer, int width, int height, int pitch_pixels) {
    if (!framebuffer) return;

    for (int r = 0; r < TERM_ROWS; r++) {
        int py = r * FONT_CHAR_H;
        if (py + FONT_CHAR_H > height) break;

        for (int c = 0; c < TERM_COLS; c++) {
            int px = c * FONT_CHAR_W;
            if (px + FONT_CHAR_W > width) break;

            term_cell_t *cell = &s_grid[r][c];
            uint32_t fg = cell->fg;
            uint32_t bg = cell->bg;

            /* Handle reverse video attribute */
            if (cell->attr & 4) {
                uint32_t tmp = fg;
                fg = bg;
                bg = tmp;
            }

            /* Draw cursor inverse */
            bool is_cursor = (r == s_cursor_row && c == s_cursor_col && s_cursor_visible);
            if (is_cursor) {
                uint32_t tmp = fg;
                fg = bg;
                bg = tmp;
            }

            unsigned char ch = (unsigned char)cell->ch;
            const uint8_t *glyph = NULL;
            if (ch >= 32 && ch <= 126) {
                glyph = font8x8_basic[ch - 32];
            }

            /* Render 8x10 cell (1 scanline top padding, 8 scanlines glyph, 1 scanline bottom padding) */
            /* Scanline 0: top padding */
            uint32_t *line_ptr = framebuffer + py * pitch_pixels + px;
            for (int dx = 0; dx < 8; dx++) line_ptr[dx] = bg;

            /* Scanlines 1..8: glyph rows */
            for (int dy = 0; dy < 8; dy++) {
                line_ptr = framebuffer + (py + 1 + dy) * pitch_pixels + px;
                uint8_t row_bits = glyph ? glyph[dy] : 0;
                for (int dx = 0; dx < 8; dx++) {
                    line_ptr[dx] = (row_bits & (0x80 >> dx)) ? fg : bg;
                }
            }

            /* Scanline 9: bottom padding (or underline) */
            line_ptr = framebuffer + (py + 9) * pitch_pixels + px;
            uint32_t bottom_col = (cell->attr & 2) ? fg : bg;
            for (int dx = 0; dx < 8; dx++) line_ptr[dx] = bottom_col;
        }
    }
}
