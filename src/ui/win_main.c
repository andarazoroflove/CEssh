#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "terminal.h"
#include "font.h"
#include "../cli/prompt.h"
#include "../ssh/ssh2.h"
#include "../net/winsock_ce.h"

#define SCREEN_W 640
#define SCREEN_H_MAX 240
#define SCREEN_H_DEFAULT 214

static int    s_screen_w = SCREEN_W;
static int    s_screen_h = SCREEN_H_DEFAULT;

static HWND   s_hwnd = NULL;
static HDC    s_hdc_mem = NULL;
static HBITMAP s_hbm_dib = NULL;
static HBITMAP s_hbm_old = NULL;
static void  *s_dib_bits = NULL;

static bool   s_dib_bottom_up = false;
static bool   s_is_555 = false;
static bool   s_is_24bpp = false;

static uint32_t s_backbuffer[SCREEN_W * SCREEN_H_MAX];
static bool   s_cursor_blink_state = true;
static DWORD  s_last_blink_tick = 0;
static volatile bool s_abort_flag = false;

void kbd_set_abort(void) {
    s_abort_flag = true;
}

bool kbd_check_abort(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_CHAR) {
            if (msg.wParam == 4) { /* Ctrl+D: Kill full program */
                ssh2_disconnect();
                ftp_disconnect();
                ExitProcess(0);
            }
            if (msg.wParam == 11 || msg.wParam == 3) { /* Ctrl+K / Ctrl+C */
                s_abort_flag = true;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (s_abort_flag) {
        s_abort_flag = false;
        return true;
    }
    return false;
}

static void dib_init(HWND hwnd) {
    HDC hdc_win = GetDC(hwnd);
    if (!hdc_win) return;

    s_hdc_mem = CreateCompatibleDC(hdc_win);

    /* Attempt 1: Top-down 16bpp 5:6:5 (BI_BITFIELDS) */
    struct {
        BITMAPINFOHEADER bmiHeader;
        DWORD bmiColors[3];
    } bmi16;
    memset(&bmi16, 0, sizeof(bmi16));
    bmi16.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi16.bmiHeader.biWidth = s_screen_w;
    bmi16.bmiHeader.biHeight = -s_screen_h; /* Top-down */
    bmi16.bmiHeader.biPlanes = 1;
    bmi16.bmiHeader.biBitCount = 16;
    bmi16.bmiHeader.biCompression = BI_BITFIELDS;
    bmi16.bmiColors[0] = 0xF800; /* Red */
    bmi16.bmiColors[1] = 0x07E0; /* Green */
    bmi16.bmiColors[2] = 0x001F; /* Blue */

    s_hbm_dib = CreateDIBSection(hdc_win, (BITMAPINFO *)&bmi16, DIB_RGB_COLORS, &s_dib_bits, NULL, 0);
    if (s_hbm_dib && s_dib_bits) {
        s_dib_bottom_up = false;
        s_is_555 = false;
    } else {
        /* Attempt 2: Bottom-up 16bpp 5:6:5 */
        bmi16.bmiHeader.biHeight = s_screen_h;
        s_hbm_dib = CreateDIBSection(hdc_win, (BITMAPINFO *)&bmi16, DIB_RGB_COLORS, &s_dib_bits, NULL, 0);
        if (s_hbm_dib && s_dib_bits) {
            s_dib_bottom_up = true;
            s_is_555 = false;
        } else {
            /* Attempt 3: Bottom-up 16bpp 5:5:5 (BI_RGB) */
            BITMAPINFOHEADER bmi555;
            memset(&bmi555, 0, sizeof(bmi555));
            bmi555.biSize = sizeof(BITMAPINFOHEADER);
            bmi555.biWidth = s_screen_w;
            bmi555.biHeight = s_screen_h;
            bmi555.biPlanes = 1;
            bmi555.biBitCount = 16;
            bmi555.biCompression = BI_RGB;

            s_hbm_dib = CreateDIBSection(hdc_win, (BITMAPINFO *)&bmi555, DIB_RGB_COLORS, &s_dib_bits, NULL, 0);
            if (s_hbm_dib && s_dib_bits) {
                s_dib_bottom_up = true;
                s_is_555 = true;
            } else {
                /* Attempt 4: Bottom-up 24bpp (BI_RGB) */
                BITMAPINFOHEADER bmi24;
                memset(&bmi24, 0, sizeof(bmi24));
                bmi24.biSize = sizeof(BITMAPINFOHEADER);
                bmi24.biWidth = s_screen_w;
                bmi24.biHeight = s_screen_h;
                bmi24.biPlanes = 1;
                bmi24.biBitCount = 24;
                bmi24.biCompression = BI_RGB;

                s_hbm_dib = CreateDIBSection(hdc_win, (BITMAPINFO *)&bmi24, DIB_RGB_COLORS, &s_dib_bits, NULL, 0);
                if (s_hbm_dib && s_dib_bits) {
                    s_dib_bottom_up = true;
                    s_is_24bpp = true;
                }
            }
        }
    }

    if (s_hbm_dib && s_hdc_mem) {
        s_hbm_old = (HBITMAP)SelectObject(s_hdc_mem, s_hbm_dib);
    }

    ReleaseDC(hwnd, hdc_win);
}

void flip_screen(void) {
    if (!s_hwnd || !s_hdc_mem || !s_dib_bits) return;

    /* 1. Render terminal grid into 32bpp backbuffer */
    term_render(s_backbuffer, s_screen_w, s_screen_h, SCREEN_W);

    /* 2. Convert to DIBSection memory */
    if (s_is_24bpp) {
        uint8_t *dst = (uint8_t *)s_dib_bits;
        for (int y = 0; y < s_screen_h; y++) {
            int dy = s_dib_bottom_up ? (s_screen_h - 1 - y) : y;
            uint8_t *row = dst + (dy * s_screen_w * 3);
            const uint32_t *src = s_backbuffer + (y * SCREEN_W);
            for (int x = 0; x < s_screen_w; x++) {
                uint32_t c = src[x];
                row[x * 3 + 0] = (uint8_t)(c & 0xFF);         /* B */
                row[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);  /* G */
                row[x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF); /* R */
            }
        }
    } else {
        uint16_t *dst = (uint16_t *)s_dib_bits;
        for (int y = 0; y < s_screen_h; y++) {
            int dy = s_dib_bottom_up ? (s_screen_h - 1 - y) : y;
            uint16_t *row = dst + (dy * s_screen_w);
            const uint32_t *src = s_backbuffer + (y * SCREEN_W);
            if (s_is_555) {
                for (int x = 0; x < s_screen_w; x++) {
                    uint32_t c = src[x];
                    uint16_t r = (uint16_t)((c >> 19) & 0x1F);
                    uint16_t g = (uint16_t)((c >> 11) & 0x1F);
                    uint16_t b = (uint16_t)((c >> 3) & 0x1F);
                    row[x] = (r << 10) | (g << 5) | b;
                }
            } else {
                for (int x = 0; x < s_screen_w; x++) {
                    uint32_t c = src[x];
                    uint16_t r = (uint16_t)((c >> 19) & 0x1F);
                    uint16_t g = (uint16_t)((c >> 10) & 0x3F);
                    uint16_t b = (uint16_t)((c >> 3) & 0x1F);
                    row[x] = (r << 11) | (g << 5) | b;
                }
            }
        }
    }

    /* 3. Blit to screen DC */
    HDC hdc = GetDC(s_hwnd);
    if (hdc) {
        BitBlt(hdc, 0, 0, s_screen_w, s_screen_h, s_hdc_mem, 0, 0, SRCCOPY);
        ReleaseDC(s_hwnd, hdc);
    }
}

void win_main_flip(void) {
    flip_screen();
}

HWND win_main_get_hwnd(void) {
    return s_hwnd;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            return 0;

        case WM_CHAR: {
            char c = (char)wParam;
            if (c == 4) { /* Ctrl+D: Kill entire program */
                ssh2_disconnect();
                ftp_disconnect();
                DestroyWindow(hwnd);
                ExitProcess(0);
                return 0;
            }
            if (c == 11) { /* Ctrl+K: abort running command */
                s_abort_flag = true;
            }
            prompt_handle_char(c);
            flip_screen();
            return 0;
        }

        case WM_KEYDOWN:
            prompt_handle_keydown((int)wParam);
            flip_screen();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            flip_screen();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER: {
            bool need_flip = false;

            /* Poll SSH network traffic */
            if (ssh2_is_connected()) {
                if (ssh2_poll()) {
                    need_flip = true;
                }
            }

            /* Cursor blink timer (every 500ms) */
            DWORD now = GetTickCount();
            if (now - s_last_blink_tick >= 500) {
                s_last_blink_tick = now;
                s_cursor_blink_state = !s_cursor_blink_state;
                term_set_cursor_visible(s_cursor_blink_state);
                need_flip = true;
            }

            if (need_flip) {
                flip_screen();
            }
            return 0;
        }

        case WM_CLOSE:
            ssh2_disconnect();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (s_hdc_mem && s_hbm_old) {
                SelectObject(s_hdc_mem, s_hbm_old);
            }
            if (s_hbm_dib) {
                DeleteObject(s_hbm_dib);
            }
            if (s_hdc_mem) {
                DeleteDC(s_hdc_mem);
            }
            winsock_ce_cleanup();
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    /* Initialize subsystems */
    winsock_ce_init();
    term_init();
    prompt_init();

    const wchar_t szClassName[] = L"CEsshWindowClass";

    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = szClassName;

    RegisterClass(&wc);

    /* Determine desktop work area so CEssh ends above the taskbar */
    RECT rcWork;
    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0) && rcWork.bottom > rcWork.top && rcWork.bottom <= 240) {
        s_screen_w = rcWork.right - rcWork.left;
        s_screen_h = rcWork.bottom - rcWork.top;
        if (s_screen_h > SCREEN_H_DEFAULT) s_screen_h = SCREEN_H_DEFAULT;
    } else {
        s_screen_w = SCREEN_W;
        s_screen_h = SCREEN_H_DEFAULT;
    }

    /* HP Jornada 720 terminal window docked cleanly above taskbar */
    s_hwnd = CreateWindowEx(
        0,
        szClassName,
        L"CEssh - SSH-2 Terminal for HP Jornada 720",
        WS_POPUP | WS_VISIBLE,
        0, 0, s_screen_w, s_screen_h,
        NULL, NULL, hInstance, NULL
    );

    if (!s_hwnd) {
        MessageBoxW(NULL, L"Failed to create CEssh window.", L"CEssh Error", MB_OK);
        return 1;
    }

    dib_init(s_hwnd);
    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);

    flip_screen();

    /* 50Hz polling timer (20ms interval) for smooth remote terminal response */
    SetTimer(s_hwnd, 1, 20, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
