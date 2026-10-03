#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "note.h"
#include "path_util.h"
#include "../ui/terminal.h"

#define NOTE_MAX_TEXT   16384
#define NOTE_MAX_VLINES 512

extern void win_main_flip(void);

typedef struct {
    size_t start;
    size_t len;
} note_vline_t;

void note_run(const char *filename) {
    char target_name[64];
    if (filename && filename[0] != '\0') {
        while (*filename == ' ' || *filename == '\t') filename++;
        strncpy(target_name, filename, sizeof(target_name) - 1);
        target_name[sizeof(target_name) - 1] = '\0';
    } else {
        strcpy(target_name, "notes.txt");
    }

    /* Resolve file strictly in usr/ folder */
    wchar_t full_pathW[MAX_PATH];
    char full_pathA[MAX_PATH];
    path_resolve_usr(target_name, full_pathW, full_pathA, sizeof(full_pathA));

    char text[NOTE_MAX_TEXT];
    size_t text_len = 0;
    size_t cursor_pos = 0;
    char status_msg[160] = {0};

    /* Attempt to read existing file */
    HANDLE hFile = CreateFileW(full_pathW, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD read_bytes = 0;
        if (ReadFile(hFile, text, NOTE_MAX_TEXT - 1, &read_bytes, NULL)) {
            text_len = read_bytes;
            text[text_len] = '\0';
            cursor_pos = text_len;
            snprintf(status_msg, sizeof(status_msg), "Read %lu bytes from %.40s", (unsigned long)text_len, target_name);
        }
        CloseHandle(hFile);
    } else {
        text[0] = '\0';
        text_len = 0;
        cursor_pos = 0;
        snprintf(status_msg, sizeof(status_msg), "New File: %.40s", target_name);
    }

    /* Switch to raw full-terminal mode (no watermark while editing) */
    term_set_watermark(false);
    term_clear();

    const int term_cols = TERM_COLS; /* 80 */
    const int term_rows = TERM_ROWS; /* 21 */
    const int edit_rows = term_rows - 3; /* Row 0: title, Rows 1..18: text (18 rows), Row 19: status, Row 20: help */
    int scroll_row = 0;

    note_vline_t vlines[NOTE_MAX_VLINES];
    int num_vlines = 0;

    bool running = true;
    bool dirty = false;

    while (running) {
        /* 1. Calculate visual lines with word wrap */
        num_vlines = 0;
        size_t idx = 0;
        int cur_vrow = 0;
        int cur_vcol = 0;
        bool cur_found = false;

        while (idx <= text_len && num_vlines < NOTE_MAX_VLINES) {
            if (idx == text_len) {
                if (!cur_found && cursor_pos == idx) {
                    cur_vrow = (num_vlines > 0) ? (num_vlines - 1) : 0;
                    cur_vcol = (num_vlines > 0) ? vlines[cur_vrow].len : 0;
                    cur_found = true;
                }
                break;
            }

            size_t line_start = idx;
            size_t line_len = 0;
            size_t last_space = 0;
            bool found_space = false;

            while (idx < text_len && text[idx] != '\n' && text[idx] != '\r' && line_len < (size_t)term_cols) {
                if (idx == cursor_pos) {
                    cur_vrow = num_vlines;
                    cur_vcol = line_len;
                    cur_found = true;
                }
                if (text[idx] == ' ' || text[idx] == '\t') {
                    last_space = line_len;
                    found_space = true;
                }
                line_len++;
                idx++;
            }

            if (idx == cursor_pos && !cur_found) {
                cur_vrow = num_vlines;
                cur_vcol = line_len;
                cur_found = true;
            }

            if (idx < text_len && (text[idx] == '\n' || text[idx] == '\r')) {
                vlines[num_vlines].start = line_start;
                vlines[num_vlines].len = line_len;
                num_vlines++;
                if (text[idx] == '\r' && idx + 1 < text_len && text[idx + 1] == '\n') {
                    idx += 2;
                } else {
                    idx++;
                }
            } else if (line_len >= (size_t)term_cols) {
                if (found_space && last_space > 0 && last_space < line_len) {
                    size_t rewind = line_len - (last_space + 1);
                    idx -= rewind;
                    line_len = last_space;
                }
                vlines[num_vlines].start = line_start;
                vlines[num_vlines].len = line_len;
                num_vlines++;
            } else {
                vlines[num_vlines].start = line_start;
                vlines[num_vlines].len = line_len;
                num_vlines++;
            }
        }

        if (num_vlines == 0) {
            vlines[0].start = 0;
            vlines[0].len = 0;
            num_vlines = 1;
        }

        if (!cur_found) {
            cur_vrow = num_vlines - 1;
            cur_vcol = vlines[cur_vrow].len;
        }

        /* 2. Adjust scrolling window */
        if (cur_vrow < scroll_row) scroll_row = cur_vrow;
        if (cur_vrow >= scroll_row + edit_rows) scroll_row = cur_vrow - edit_rows + 1;

        /* 3. Render Header Bar (Row 0) */
        char hdr[128];
        snprintf(hdr, sizeof(hdr), "  CEssh Note Editor (usr/%.40s)%s", target_name, dirty ? " [Modified]" : "");
        size_t hlen = strlen(hdr);
        for (int c = 0; c < term_cols; c++) {
            char ch = (c < (int)hlen) ? hdr[c] : ' ';
            term_render_cell(c, 0, ch, COLOR_BLACK, COLOR_LTGRAY, 1);
        }

        /* 4. Render Text Area (Rows 1..edit_rows) */
        for (int r = 0; r < edit_rows; r++) {
            int vidx = scroll_row + r;
            int screen_r = 1 + r;
            if (vidx < num_vlines) {
                size_t s = vlines[vidx].start;
                size_t l = vlines[vidx].len;
                for (int c = 0; c < term_cols; c++) {
                    char ch = (c < (int)l) ? text[s + c] : ' ';
                    term_render_cell(c, screen_r, ch, COLOR_BLACK, COLOR_WHITE, 0);
                }
            } else {
                term_render_cell(0, screen_r, '~', COLOR_GRAY, COLOR_WHITE, 0);
                for (int c = 1; c < term_cols; c++) {
                    term_render_cell(c, screen_r, ' ', COLOR_BLACK, COLOR_WHITE, 0);
                }
            }
        }

        /* 5. Render Status Bar (Row 19) */
        int status_r = 1 + edit_rows; /* Row 19 */
        char sbar[84];
        if (status_msg[0] != '\0') {
            snprintf(sbar, sizeof(sbar), "[ %.70s ]", status_msg);
        } else {
            snprintf(sbar, sizeof(sbar), "[ Line %d/%d  Col %d  Pos %lu/%lu ]",
                     cur_vrow + 1, num_vlines, cur_vcol + 1,
                     (unsigned long)cursor_pos, (unsigned long)text_len);
        }
        size_t slen = strlen(sbar);
        for (int c = 0; c < term_cols; c++) {
            char ch = (c < (int)slen) ? sbar[c] : ' ';
            term_render_cell(c, status_r, ch, COLOR_DKGRAY, COLOR_LTGRAY, 0);
        }

        /* 6. Render Help Bar (Row 20) */
        int help_r = status_r + 1; /* Row 20 */
        const char *help = "^O WriteOut  ^X Save&Exit  ^K Cancel  ^D Exit App";
        size_t helplen = strlen(help);
        for (int c = 0; c < term_cols; c++) {
            char ch = (c < (int)helplen) ? help[c] : ' ';
            term_render_cell(c, help_r, ch, COLOR_BLACK, COLOR_LTGRAY, 1);
        }

        /* 7. Position cursor */
        int render_row = 1 + (cur_vrow - scroll_row);
        term_set_cursor(cur_vcol, render_row);

        win_main_flip();

        /* 8. Event Loop */
        MSG msg;
        if (!GetMessage(&msg, NULL, 0, 0)) {
            break;
        }

        if (msg.message == WM_CHAR) {
            char c = (char)msg.wParam;
            status_msg[0] = '\0'; /* Clear status message on keystroke */

            if (c == 4) { /* Ctrl+D: Exit whole application */
                ExitProcess(0);
            } else if (c == 15) { /* Ctrl+O: WriteOut (save file without exit) */
                HANDLE hSave = CreateFileW(full_pathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hSave != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(hSave, text, (DWORD)text_len, &written, NULL);
                    CloseHandle(hSave);
                    dirty = false;
                    snprintf(status_msg, sizeof(status_msg), "Wrote %lu bytes to usr/%.40s", (unsigned long)text_len, target_name);
                } else {
                    snprintf(status_msg, sizeof(status_msg), "Error saving file usr/%.40s", target_name);
                }
            } else if (c == 24) { /* Ctrl+X: Save and exit */
                HANDLE hSave = CreateFileW(full_pathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hSave != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(hSave, text, (DWORD)text_len, &written, NULL);
                    CloseHandle(hSave);
                }
                running = false;
            } else if (c == 11) { /* Ctrl+K: Cancel / exit without saving */
                running = false;
            } else if (c == '\b' || c == 127) { /* Backspace */
                if (cursor_pos > 0) {
                    memmove(&text[cursor_pos - 1], &text[cursor_pos], text_len - cursor_pos);
                    cursor_pos--;
                    text_len--;
                    text[text_len] = '\0';
                    dirty = true;
                }
            } else if (c == '\r' || c == '\n') { /* Enter */
                if (text_len < NOTE_MAX_TEXT - 1) {
                    memmove(&text[cursor_pos + 1], &text[cursor_pos], text_len - cursor_pos);
                    text[cursor_pos] = '\n';
                    cursor_pos++;
                    text_len++;
                    text[text_len] = '\0';
                    dirty = true;
                }
            } else if ((unsigned char)c >= 32) { /* Printable char */
                if (text_len < NOTE_MAX_TEXT - 1) {
                    memmove(&text[cursor_pos + 1], &text[cursor_pos], text_len - cursor_pos);
                    text[cursor_pos] = c;
                    cursor_pos++;
                    text_len++;
                    text[text_len] = '\0';
                    dirty = true;
                }
            }
        } else if (msg.message == WM_KEYDOWN) {
            status_msg[0] = '\0';
            int vk = (int)msg.wParam;
            if (vk == VK_LEFT) {
                if (cursor_pos > 0) cursor_pos--;
            } else if (vk == VK_RIGHT) {
                if (cursor_pos < text_len) cursor_pos++;
            } else if (vk == VK_UP) {
                if (cur_vrow > 0) {
                    int prev_row = cur_vrow - 1;
                    size_t target_col = ((size_t)cur_vcol < vlines[prev_row].len) ? (size_t)cur_vcol : vlines[prev_row].len;
                    cursor_pos = vlines[prev_row].start + target_col;
                }
            } else if (vk == VK_DOWN) {
                if (cur_vrow < num_vlines - 1) {
                    int next_row = cur_vrow + 1;
                    size_t target_col = ((size_t)cur_vcol < vlines[next_row].len) ? (size_t)cur_vcol : vlines[next_row].len;
                    cursor_pos = vlines[next_row].start + target_col;
                }
            } else if (vk == VK_HOME) {
                cursor_pos = vlines[cur_vrow].start;
            } else if (vk == VK_END) {
                cursor_pos = vlines[cur_vrow].start + vlines[cur_vrow].len;
            } else if (vk == VK_DELETE) {
                if (cursor_pos < text_len) {
                    memmove(&text[cursor_pos], &text[cursor_pos + 1], text_len - cursor_pos - 1);
                    text_len--;
                    text[text_len] = '\0';
                    dirty = true;
                }
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    /* Restore watermark terminal mode */
    term_set_watermark(true);
    term_clear();
}
