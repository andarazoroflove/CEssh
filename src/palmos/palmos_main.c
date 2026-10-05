/*
 * CEssh - Palm OS Garnet / Palm T|X Main Application & Terminal UI
 *
 * Provides a lightweight ANSI terminal emulator on Palm OS, NetLib
 * socket management, and event-driven SSH-2 interactive sessions.
 */

#include <PalmOS.h>
#include "cessh_res.h"
#include "palmos_net.h"
#include "ssh/ssh2.h"

#define TERM_ROWS   14
#define TERM_COLS   30
#define FONT_H      11
#define FONT_W      5

static char s_screen[TERM_ROWS][TERM_COLS + 1];
static int s_cur_r = 0;
static int s_cur_c = 0;
static Boolean s_dirty = true;
static Boolean s_connected = false;

static char s_input_buf[80];
static int s_input_len = 0;

static void term_draw(void);

static void term_clear(void) {
    int r;
    for (r = 0; r < TERM_ROWS; r++) {
        MemSet(s_screen[r], 0, sizeof(s_screen[r]));
    }
    s_cur_r = 0;
    s_cur_c = 0;
    s_dirty = true;
}

static void term_scroll(void) {
    MemMove(s_screen[0], s_screen[1], sizeof(s_screen[0]) * (TERM_ROWS - 1));
    MemSet(s_screen[TERM_ROWS - 1], 0, sizeof(s_screen[TERM_ROWS - 1]));
    s_cur_r = TERM_ROWS - 1;
}

static void term_putc(char ch) {
    if (ch == '\r') {
        s_cur_c = 0;
    } else if (ch == '\n') {
        s_cur_r++;
        if (s_cur_r >= TERM_ROWS) {
            term_scroll();
        }
    } else if (ch == '\b') {
        if (s_cur_c > 0) {
            s_cur_c--;
            s_screen[s_cur_r][s_cur_c] = ' ';
        }
    } else if (ch == '\t') {
        int next_tab = (s_cur_c + 4) & ~3;
        while (s_cur_c < next_tab && s_cur_c < TERM_COLS) {
            s_screen[s_cur_r][s_cur_c++] = ' ';
        }
    } else if ((UInt8)ch >= 32 && (UInt8)ch <= 126) {
        if (s_cur_c >= TERM_COLS) {
            s_cur_c = 0;
            s_cur_r++;
            if (s_cur_r >= TERM_ROWS) {
                term_scroll();
            }
        }
        s_screen[s_cur_r][s_cur_c++] = ch;
        s_screen[s_cur_r][s_cur_c] = '\0';
    }
    s_dirty = true;
}

static void term_puts(const char *str) {
    if (!str) return;
    while (*str) {
        term_putc(*str++);
    }
}

static void palmos_ssh_output(const char *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        term_putc(data[i]);
    }
}

static void term_draw(void) {
    RectangleType rect;
    RectangleType cur_rect;
    int r;

    rect.topLeft.x = 0;
    rect.topLeft.y = 0;
    rect.extent.x = 160;
    rect.extent.y = 160;
    WinEraseRectangle(&rect, 0);

    FntSetFont(stdFont);

    for (r = 0; r < TERM_ROWS; r++) {
        Int16 len = StrLen(s_screen[r]);
        if (len > 0) {
            WinDrawChars(s_screen[r], len, 2, (Coord)(r * FONT_H));
        }
    }

    /* Draw block cursor */
    if (s_cur_r >= 0 && s_cur_r < TERM_ROWS && s_cur_c >= 0 && s_cur_c <= TERM_COLS) {
        cur_rect.topLeft.x = (Coord)(2 + s_cur_c * FONT_W);
        cur_rect.topLeft.y = (Coord)(s_cur_r * FONT_H);
        cur_rect.extent.x = FONT_W;
        cur_rect.extent.y = FONT_H;
        WinInvertRectangle(&cur_rect, 0);
    }
}

static void process_cmd(char *cmd) {
    char host[64];
    char user[32];
    char pass[32];
    char *p = cmd;
    char *tok;
    int arg_idx;

    while (*p == ' ') p++;
    if (*p == '\0') {
        term_puts("CEssh> ");
        return;
    }

    if (StrCompare(p, "help") == 0 || StrCompare(p, "?") == 0) {
        term_puts("CEssh Commands:\r\n");
        term_puts(" ssh <host> [user] [pass]\r\n");
        term_puts(" ping <host>\r\n");
        term_puts(" clear\r\n");
        term_puts(" exit\r\n");
        term_puts("CEssh> ");
        return;
    }

    if (StrCompare(p, "clear") == 0) {
        term_clear();
        term_puts("CEssh v0.9a (Palm OS)\r\nCEssh> ");
        return;
    }

    if (StrCompare(p, "exit") == 0 || StrCompare(p, "quit") == 0) {
        EventType stopEvent;
        stopEvent.eType = appStopEvent;
        EvtAddEventToQueue(&stopEvent);
        return;
    }

    if (StrNCompare(p, "ping ", 5) == 0) {
        char *target = p + 5;
        UInt32 ip;
        int sock;
        char msg[64];

        while (*target == ' ') target++;
        if (*target == '\0') {
            term_puts("Usage: ping <host>\r\nCEssh> ");
            return;
        }

        term_puts("Resolving host...\r\n");
        if (!palmos_net_init()) {
            term_puts("NetLib init failed.\r\nCEssh> ");
            return;
        }

        ip = palmos_resolve(target);
        if (ip == 0) {
            term_puts("Could not resolve host.\r\nCEssh> ");
            return;
        }

        StrPrintF(msg, "IP: %u.%u.%u.%u\r\nTesting TCP...",
                  (unsigned)((ip >> 24) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)(ip & 0xFF));
        term_puts(msg);

        sock = palmos_net_connect(target, 22, 3000);
        if (sock == INVALID_SOCKET) {
            sock = palmos_net_connect(target, 80, 3000);
        }

        if (sock != INVALID_SOCKET) {
            term_puts(" Host UP!\r\n");
            palmos_net_close(sock);
        } else {
            term_puts(" Host unreachable.\r\n");
        }
        term_puts("CEssh> ");
        return;
    }

    if (StrNCompare(p, "ssh ", 4) == 0) {
        host[0] = '\0';
        user[0] = '\0';
        pass[0] = '\0';

        p += 4;
        while (*p == ' ') p++;

        tok = p;
        arg_idx = 0;
        while (*p) {
            if (*p == ' ') {
                *p = '\0';
                if (arg_idx == 0) StrNCopy(host, tok, sizeof(host) - 1);
                else if (arg_idx == 1) StrNCopy(user, tok, sizeof(user) - 1);
                arg_idx++;
                p++;
                while (*p == ' ') p++;
                tok = p;
            } else {
                p++;
            }
        }
        if (*tok) {
            if (arg_idx == 0) StrNCopy(host, tok, sizeof(host) - 1);
            else if (arg_idx == 1) StrNCopy(user, tok, sizeof(user) - 1);
            else if (arg_idx == 2) StrNCopy(pass, tok, sizeof(pass) - 1);
        }

        if (host[0] == '\0') {
            term_puts("Usage: ssh <host> [user] [pass]\r\nCEssh> ");
            return;
        }

        if (user[0] == '\0') {
            StrCopy(user, "root");
        }

        term_puts("Connecting via SSH-2...\r\n");
        if (!palmos_net_init()) {
            term_puts("Error: NetLib not available.\r\nCEssh> ");
            return;
        }

        ssh2_set_terminal_size(TERM_COLS, TERM_ROWS);
        if (ssh2_connect(host, 22, user, pass[0] ? pass : NULL)) {
            s_connected = true;
        } else {
            term_puts("\r\n[SSH Connection Failed]\r\nCEssh> ");
        }
        return;
    }

    term_puts("Unknown command. Type 'help' for info.\r\nCEssh> ");
}

static Boolean MainFormHandleEvent(EventType *eventP) {
    Boolean handled = false;
    FormType *frmP = FrmGetActiveForm();

    switch (eventP->eType) {
        case frmOpenEvent:
            FrmDrawForm(frmP);
            term_draw();
            handled = true;
            break;

        case frmUpdateEvent:
            FrmDrawForm(frmP);
            term_draw();
            handled = true;
            break;

        case menuEvent:
            switch (eventP->data.menu.itemID) {
                case MenuOptsDisconnect:
                    if (s_connected) {
                        ssh2_disconnect();
                        s_connected = false;
                        term_puts("\r\n[Session closed]\r\nCEssh> ");
                    }
                    handled = true;
                    break;
                case MenuOptsClear:
                    term_clear();
                    if (!s_connected) term_puts("CEssh> ");
                    handled = true;
                    break;
                case MenuOptsAbout:
                    FrmAlert(AboutAlert);
                    handled = true;
                    break;
                default:
                    break;
            }
            break;

        case keyDownEvent: {
            WChar chr = eventP->data.keyDown.chr;

            if (s_connected) {
                char send_ch;
                if (chr == 0x1d) { /* Ctrl+] */
                    ssh2_disconnect();
                    s_connected = false;
                    term_puts("\r\n[Disconnected]\r\nCEssh> ");
                    handled = true;
                    break;
                }

                if (chr == returnChr || chr == linefeedChr) {
                    send_ch = '\r';
                } else if (chr == backspaceChr) {
                    send_ch = '\b';
                } else if (chr >= 32 && chr <= 126) {
                    send_ch = (char)chr;
                } else {
                    send_ch = (char)chr;
                }
                ssh2_send_data(&send_ch, 1);
                handled = true;
            } else {
                if (chr == returnChr || chr == linefeedChr) {
                    term_puts("\r\n");
                    s_input_buf[s_input_len] = '\0';
                    process_cmd(s_input_buf);
                    s_input_len = 0;
                    handled = true;
                } else if (chr == backspaceChr) {
                    if (s_input_len > 0) {
                        s_input_len--;
                        term_puts("\b \b");
                    }
                    handled = true;
                } else if (chr >= 32 && chr <= 126) {
                    if (s_input_len < (int)sizeof(s_input_buf) - 1) {
                        s_input_buf[s_input_len++] = (char)chr;
                        s_input_buf[s_input_len] = '\0';
                        term_putc((char)chr);
                    }
                    handled = true;
                }
            }
            break;
        }

        default:
            break;
    }

    return handled;
}

static Boolean AppHandleEvent(EventType *eventP) {
    UInt16 formId;
    FormType *frmP;

    if (eventP->eType == frmLoadEvent) {
        formId = eventP->data.frmLoad.formID;
        frmP = FrmInitForm(formId);
        FrmSetActiveForm(frmP);
        FrmSetEventHandler(frmP, MainFormHandleEvent);
        return true;
    }
    return false;
}

static void AppEventLoop(void) {
    EventType event;
    UInt16 error;
    UInt32 wait;

    do {
        wait = (s_connected) ? 2 : 20;
        EvtGetEvent(&event, wait);

        if (!SysHandleEvent(&event)) {
            if (!MenuHandleEvent(0, &event, &error)) {
                if (!AppHandleEvent(&event)) {
                    FrmDispatchEvent(&event);
                }
            }
        }

        if (s_connected) {
            ssh2_poll();
        }

        if (s_dirty) {
            s_dirty = false;
            term_draw();
        }
    } while (event.eType != appStopEvent);
}

static Err RomVersionCompatible(UInt32 requiredVersion, UInt16 launchFlags) {
    UInt32 romVersion;
    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);
    if (romVersion < requiredVersion) {
        if ((launchFlags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) ==
            (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) {
            FrmAlert(RomIncompatibleAlert);
        }
        return sysErrRomIncompatible;
    }
    return errNone;
}

UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags) {
    Err error;

    (void)cmdPBP;

    error = RomVersionCompatible(sysMakeROMVersion(4, 0, 0, sysROMStageRelease, 0), launchFlags);
    if (error) return error;

    if (cmd == sysAppLaunchCmdNormalLaunch) {
        term_clear();
        term_puts("CEssh v0.9a (Palm OS)\r\n");
        term_puts("SSH-2 Client / Garnet 5.4.9\r\n");
        term_puts("Type 'help' for commands.\r\n\r\nCEssh> ");

        ssh2_init(palmos_ssh_output);

        FrmGotoForm(MainFormID);
        AppEventLoop();

        if (s_connected) {
            ssh2_disconnect();
            s_connected = false;
        }
        palmos_net_close_all();
        FrmCloseAllForms();
    }

    return errNone;
}
