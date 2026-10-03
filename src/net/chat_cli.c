#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "chat_cli.h"
#include "winsock_ce.h"
#include "../ui/terminal.h"

extern void win_main_flip(void);

#define CHAT_MAX_LINES 128
#define CHAT_MSG_LEN   120

typedef struct {
    char     text[CHAT_MSG_LEN];
    uint32_t fg;
    uint32_t bg;
    uint8_t  attr;
} chat_line_t;

static chat_line_t s_chat_lines[CHAT_MAX_LINES];
static int         s_chat_line_count = 0;

static SOCKET      s_irc_sock = INVALID_SOCKET;
static char        s_irc_host[128] = "irc.libera.chat";
static uint16_t    s_irc_port = 6667;
static char        s_irc_nick[32] = "jornada";
static char        s_irc_chan[32] = "#jornada";
static bool        s_irc_connected = false;

static char        s_rx_buf[2048];
static size_t      s_rx_len = 0;

static void chat_add_line(const char *text, uint32_t fg) {
    if (!text) return;

    /* Word-wrap text to 78 chars */
    const int max_w = TERM_COLS - 2;
    const char *p = text;

    while (*p) {
        while (*p == ' ' && p != text) p++;
        if (!*p) break;

        size_t len = strlen(p);
        size_t take = (len > (size_t)max_w) ? (size_t)max_w : len;

        if (len > (size_t)max_w) {
            /* Break at last space */
            int last_sp = -1;
            for (size_t i = 0; i < take; i++) {
                if (p[i] == ' ') last_sp = (int)i;
            }
            if (last_sp > 10) take = (size_t)last_sp;
        }

        if (s_chat_line_count < CHAT_MAX_LINES) {
            strncpy(s_chat_lines[s_chat_line_count].text, p, take);
            s_chat_lines[s_chat_line_count].text[take] = '\0';
            s_chat_lines[s_chat_line_count].fg = fg;
            s_chat_lines[s_chat_line_count].bg = COLOR_WHITE;
            s_chat_lines[s_chat_line_count].attr = 0;
            s_chat_line_count++;
        } else {
            memmove(&s_chat_lines[0], &s_chat_lines[1], sizeof(chat_line_t) * (CHAT_MAX_LINES - 1));
            strncpy(s_chat_lines[CHAT_MAX_LINES - 1].text, p, take);
            s_chat_lines[CHAT_MAX_LINES - 1].text[take] = '\0';
            s_chat_lines[CHAT_MAX_LINES - 1].fg = fg;
            s_chat_lines[CHAT_MAX_LINES - 1].bg = COLOR_WHITE;
            s_chat_lines[CHAT_MAX_LINES - 1].attr = 0;
        }

        p += take;
    }
}

static void chat_send_raw(const char *cmd) {
    if (s_irc_sock == INVALID_SOCKET || !cmd) return;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    winsock_ce_send(s_irc_sock, buf, (int)strlen(buf));
}

static void chat_disconnect(void) {
    if (s_irc_sock != INVALID_SOCKET) {
        chat_send_raw("QUIT :CEssh Jornada Disconnect");
        winsock_ce_close(s_irc_sock);
        s_irc_sock = INVALID_SOCKET;
    }
    s_irc_connected = false;
    chat_add_line("*** Disconnected from IRC server.", COLOR_GRAY);
}

static bool chat_connect(const char *host, uint16_t port) {
    if (s_irc_sock != INVALID_SOCKET) {
        chat_disconnect();
    }

    if (host && host[0] != '\0') {
        strncpy(s_irc_host, host, sizeof(s_irc_host) - 1);
        s_irc_host[sizeof(s_irc_host) - 1] = '\0';
    }
    if (port > 0) s_irc_port = port;

    char msg[128];
    snprintf(msg, sizeof(msg), "*** Connecting to %s:%u...", s_irc_host, s_irc_port);
    chat_add_line(msg, COLOR_DKGRAY);

    s_irc_sock = winsock_ce_connect(s_irc_host, s_irc_port, 5000);
    if (s_irc_sock == INVALID_SOCKET) {
        chat_add_line("Error: Connection to IRC server failed or timed out.", 0x00B22222);
        return false;
    }

    s_irc_connected = true;
    s_rx_len = 0;

    /* RFC 1459 registration */
    char reg[128];
    snprintf(reg, sizeof(reg), "NICK %s", s_irc_nick);
    chat_send_raw(reg);

    snprintf(reg, sizeof(reg), "USER %s 0 * :HP Jornada User", s_irc_nick);
    chat_send_raw(reg);

    snprintf(msg, sizeof(msg), "*** Connected! Registered as %s. Type /join #channel", s_irc_nick);
    chat_add_line(msg, 0x00006400);

    /* Auto-join default channel if set */
    if (s_irc_chan[0] == '#') {
        snprintf(reg, sizeof(reg), "JOIN %s", s_irc_chan);
        chat_send_raw(reg);
    }

    return true;
}

static void chat_process_line(char *line) {
    while (*line == ' ') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    /* Handle PING from server */
    if (strncmp(line, "PING ", 5) == 0) {
        char pong[256];
        snprintf(pong, sizeof(pong), "PONG %s", line + 5);
        chat_send_raw(pong);
        return;
    }

    /* Parse Prefix :prefix CMD params */
    char *prefix = NULL;
    char *cmd = line;
    if (line[0] == ':') {
        prefix = line + 1;
        char *sp = strchr(prefix, ' ');
        if (!sp) return;
        *sp = '\0';
        cmd = sp + 1;
        while (*cmd == ' ') cmd++;
    }

    char *trailing = strstr(cmd, " :");
    if (trailing) {
        *trailing = '\0';
        trailing += 2;
    }

    char *param1 = strchr(cmd, ' ');
    if (param1) {
        *param1 = '\0';
        param1++;
        while (*param1 == ' ') param1++;
    }

    /* Extract sender nick from prefix (nick!user@host) */
    char sender[32] = {0};
    if (prefix) {
        char *ex = strchr(prefix, '!');
        if (ex) {
            size_t nl = ex - prefix;
            if (nl > sizeof(sender) - 1) nl = sizeof(sender) - 1;
            strncpy(sender, prefix, nl);
            sender[nl] = '\0';
        } else {
            strncpy(sender, prefix, sizeof(sender) - 1);
        }
    }

    if (strcmp(cmd, "PRIVMSG") == 0 && param1 && trailing) {
        char m[CHAT_MSG_LEN];
        if (param1[0] == '#') {
            snprintf(m, sizeof(m), "<%s> %s", sender, trailing);
            chat_add_line(m, COLOR_BLACK);
        } else {
            snprintf(m, sizeof(m), "*%s* %s", sender, trailing);
            chat_add_line(m, 0x008B008B);
        }
    } else if (strcmp(cmd, "NOTICE") == 0 && trailing) {
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "-%s- %s", sender[0] ? sender : "Server", trailing);
        chat_add_line(m, COLOR_DKGRAY);
    } else if (strcmp(cmd, "JOIN") == 0) {
        const char *chan = trailing ? trailing : param1;
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** %s has joined %s", sender, chan ? chan : "");
        chat_add_line(m, 0x00006400);
        if (strcmp(sender, s_irc_nick) == 0 && chan && chan[0] == '#') {
            strncpy(s_irc_chan, chan, sizeof(s_irc_chan) - 1);
        }
    } else if (strcmp(cmd, "PART") == 0) {
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** %s has left %s", sender, param1 ? param1 : "");
        chat_add_line(m, COLOR_GRAY);
    } else if (strcmp(cmd, "QUIT") == 0) {
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** %s has quit (%s)", sender, trailing ? trailing : "");
        chat_add_line(m, COLOR_GRAY);
    } else if (strcmp(cmd, "NICK") == 0) {
        const char *new_nick = trailing ? trailing : param1;
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** %s is now known as %s", sender, new_nick ? new_nick : "");
        chat_add_line(m, COLOR_DKGRAY);
        if (strcmp(sender, s_irc_nick) == 0 && new_nick) {
            strncpy(s_irc_nick, new_nick, sizeof(s_irc_nick) - 1);
        }
    } else if (strcmp(cmd, "332") == 0) { /* RPL_TOPIC */
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** Topic: %s", trailing ? trailing : "");
        chat_add_line(m, 0x008B4513);
    } else if (strcmp(cmd, "353") == 0) { /* RPL_NAMREPLY */
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** Users: %s", trailing ? trailing : "");
        chat_add_line(m, COLOR_DKGRAY);
    } else if (trailing && (cmd[0] >= '0' && cmd[0] <= '9')) {
        /* Numeric reply with trailing text (e.g. MOTD) */
        char m[CHAT_MSG_LEN];
        snprintf(m, sizeof(m), "*** %s", trailing);
        chat_add_line(m, COLOR_DKGRAY);
    }
}

static void chat_poll_net(void) {
    if (s_irc_sock == INVALID_SOCKET) return;

    while (winsock_ce_has_data(s_irc_sock)) {
        char chunk[256];
        int r = winsock_ce_recv(s_irc_sock, chunk, sizeof(chunk) - 1, 0);
        if (r <= 0) {
            chat_disconnect();
            break;
        }

        chunk[r] = '\0';
        for (int i = 0; i < r; i++) {
            char c = chunk[i];
            if (c == '\n') {
                s_rx_buf[s_rx_len] = '\0';
                chat_process_line(s_rx_buf);
                s_rx_len = 0;
            } else if (c != '\r') {
                if (s_rx_len < sizeof(s_rx_buf) - 1) {
                    s_rx_buf[s_rx_len++] = c;
                }
            }
        }
    }
}

void chat_cli_run(const char *server_arg, const char *nick_arg) {
    /* Set defaults or parse args */
    if (server_arg && server_arg[0] != '\0') {
        const char *p = server_arg;
        while (*p == ' ' || *p == '\t') p++;
        char host_part[128] = {0};
        size_t hi = 0;
        while (*p && *p != ':' && *p != ' ' && hi < sizeof(host_part) - 1) {
            host_part[hi++] = *p++;
        }
        host_part[hi] = '\0';
        if (host_part[0] != '\0') {
            strncpy(s_irc_host, host_part, sizeof(s_irc_host) - 1);
        }
        if (*p == ':') {
            p++;
            int port = atoi(p);
            if (port > 0 && port <= 65535) s_irc_port = (uint16_t)port;
        }
    }

    if (nick_arg && nick_arg[0] != '\0') {
        while (*nick_arg == ' ' || *nick_arg == '\t') nick_arg++;
        strncpy(s_irc_nick, nick_arg, sizeof(s_irc_nick) - 1);
        s_irc_nick[sizeof(s_irc_nick) - 1] = '\0';
    }

    /* Switch to raw terminal rendering without standard prompt watermark */
    term_set_watermark(false);
    term_clear();

    chat_add_line("================================================================================", COLOR_GRAY);
    chat_add_line("  CEssh IRC Chat Client for HP Jornada 720", COLOR_BLACK);
    chat_add_line("  Commands: /server <host> [port], /nick <name>, /join <#chan>, /msg, /quit", COLOR_DKGRAY);
    chat_add_line("================================================================================", COLOR_GRAY);

    chat_connect(s_irc_host, s_irc_port);

    char input[256] = {0};
    size_t input_len = 0;
    size_t input_pos = 0;

    bool running = true;

    while (running) {
        /* 1. Poll network for incoming IRC packets */
        chat_poll_net();

        /* 2. Render Header Bar (Row 0) */
        char hdr[84];
        snprintf(hdr, sizeof(hdr), "  CEssh Chat: %s:%u  Nick: %s  Room: %s%s",
                 s_irc_host, s_irc_port, s_irc_nick, s_irc_chan,
                 s_irc_connected ? " [Connected]" : " [Offline]");
        size_t hlen = strlen(hdr);
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = (c < (int)hlen) ? hdr[c] : ' ';
            term_render_cell(c, 0, ch, COLOR_BLACK, COLOR_LTGRAY, 1);
        }

        /* 3. Render Divider (Row 1) */
        for (int c = 0; c < TERM_COLS; c++) {
            term_render_cell(c, 1, '-', COLOR_GRAY, COLOR_WHITE, 0);
        }

        /* 4. Render Message Area (Rows 2..19, 18 rows) */
        const int msg_rows = 18;
        int start_idx = 0;
        if (s_chat_line_count > msg_rows) {
            start_idx = s_chat_line_count - msg_rows;
        }

        for (int r = 0; r < msg_rows; r++) {
            int line_idx = start_idx + r;
            int screen_r = 2 + r;
            if (line_idx < s_chat_line_count) {
                const chat_line_t *cl = &s_chat_lines[line_idx];
                size_t txt_len = strlen(cl->text);
                for (int c = 0; c < TERM_COLS; c++) {
                    char ch = (c < (int)txt_len) ? cl->text[c] : ' ';
                    term_render_cell(c, screen_r, ch, cl->fg, cl->bg, cl->attr);
                }
            } else {
                for (int c = 0; c < TERM_COLS; c++) {
                    term_render_cell(c, screen_r, ' ', COLOR_BLACK, COLOR_WHITE, 0);
                }
            }
        }

        /* 5. Render Input Prompt Bar (Row 20) */
        char pfx[32];
        snprintf(pfx, sizeof(pfx), "[%s]> ", s_irc_chan);
        size_t pfx_len = strlen(pfx);

        for (int c = 0; c < TERM_COLS; c++) {
            char ch = ' ';
            uint32_t fg = COLOR_BLACK;
            uint32_t bg = COLOR_WHITE;
            if (c < (int)pfx_len) {
                ch = pfx[c];
                fg = COLOR_DKGRAY;
            } else if (c - (int)pfx_len < (int)input_len) {
                ch = input[c - (int)pfx_len];
            }
            term_render_cell(c, 20, ch, fg, bg, 0);
        }

        /* Set cursor */
        int cur_x = (int)pfx_len + (int)input_pos;
        if (cur_x >= TERM_COLS) cur_x = TERM_COLS - 1;
        term_set_cursor(cur_x, 20);

        win_main_flip();

        /* 6. Process Windows Messages (PeekMessage for non-blocking I/O) */
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_CHAR) {
                char c = (char)msg.wParam;
                if (c == 4) { /* Ctrl+D: Kill whole app */
                    chat_disconnect();
                    ExitProcess(0);
                } else if (c == 11) { /* Ctrl+K: Exit chat back to prompt */
                    running = false;
                    break;
                } else if (c == '\r' || c == '\n') {
                    input[input_len] = '\0';
                    if (input_len > 0) {
                        if (input[0] == '/') {
                            /* IRC command */
                            if (strncmp(input, "/server ", 8) == 0) {
                                char *h = input + 8;
                                while (*h == ' ') h++;
                                char *p_str = strchr(h, ' ');
                                uint16_t port = 6667;
                                if (p_str) {
                                    *p_str = '\0';
                                    port = (uint16_t)atoi(p_str + 1);
                                }
                                chat_connect(h, port);
                            } else if (strncmp(input, "/nick ", 6) == 0) {
                                char *n = input + 6;
                                while (*n == ' ') n++;
                                strncpy(s_irc_nick, n, sizeof(s_irc_nick) - 1);
                                char cmd_buf[64];
                                snprintf(cmd_buf, sizeof(cmd_buf), "NICK %s", s_irc_nick);
                                chat_send_raw(cmd_buf);
                            } else if (strncmp(input, "/join ", 6) == 0) {
                                char *ch = input + 6;
                                while (*ch == ' ') ch++;
                                strncpy(s_irc_chan, ch, sizeof(s_irc_chan) - 1);
                                char cmd_buf[64];
                                snprintf(cmd_buf, sizeof(cmd_buf), "JOIN %s", s_irc_chan);
                                chat_send_raw(cmd_buf);
                            } else if (strncmp(input, "/part", 5) == 0) {
                                char cmd_buf[64];
                                snprintf(cmd_buf, sizeof(cmd_buf), "PART %s", s_irc_chan);
                                chat_send_raw(cmd_buf);
                            } else if (strncmp(input, "/msg ", 5) == 0) {
                                char *tgt = input + 5;
                                while (*tgt == ' ') tgt++;
                                char *sp = strchr(tgt, ' ');
                                if (sp) {
                                    *sp = '\0';
                                    char *txt = sp + 1;
                                    while (*txt == ' ') txt++;
                                    char cmd_buf[300];
                                    snprintf(cmd_buf, sizeof(cmd_buf), "PRIVMSG %s :%s", tgt, txt);
                                    chat_send_raw(cmd_buf);
                                    char echo[CHAT_MSG_LEN];
                                    snprintf(echo, sizeof(echo), "-> *%s* %s", tgt, txt);
                                    chat_add_line(echo, 0x008B008B);
                                }
                            } else if (strcmp(input, "/names") == 0) {
                                char cmd_buf[64];
                                snprintf(cmd_buf, sizeof(cmd_buf), "NAMES %s", s_irc_chan);
                                chat_send_raw(cmd_buf);
                            } else if (strncmp(input, "/topic", 6) == 0) {
                                char *t = input + 6;
                                while (*t == ' ') t++;
                                char cmd_buf[128];
                                if (*t) {
                                    snprintf(cmd_buf, sizeof(cmd_buf), "TOPIC %s :%s", s_irc_chan, t);
                                } else {
                                    snprintf(cmd_buf, sizeof(cmd_buf), "TOPIC %s", s_irc_chan);
                                }
                                chat_send_raw(cmd_buf);
                            } else if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
                                running = false;
                                break;
                            } else {
                                chat_add_line("Unknown command. Supported: /server, /nick, /join, /part, /msg, /names, /topic, /quit", COLOR_DKGRAY);
                            }
                        } else {
                            /* Normal channel message */
                            if (s_irc_connected && s_irc_chan[0] == '#') {
                                char cmd_buf[300];
                                snprintf(cmd_buf, sizeof(cmd_buf), "PRIVMSG %s :%s", s_irc_chan, input);
                                chat_send_raw(cmd_buf);
                                char echo[CHAT_MSG_LEN];
                                snprintf(echo, sizeof(echo), "<%s> %s", s_irc_nick, input);
                                chat_add_line(echo, 0x000000CD);
                            } else {
                                chat_add_line("Not in a channel. Use /join #channel", COLOR_DKGRAY);
                            }
                        }
                        input_len = 0;
                        input_pos = 0;
                        input[0] = '\0';
                    }
                } else if (c == '\b' || c == 127) {
                    if (input_pos > 0) {
                        memmove(&input[input_pos - 1], &input[input_pos], input_len - input_pos);
                        input_pos--;
                        input_len--;
                        input[input_len] = '\0';
                    }
                } else if ((unsigned char)c >= 32 && input_len < sizeof(input) - 2) {
                    memmove(&input[input_pos + 1], &input[input_pos], input_len - input_pos);
                    input[input_pos] = c;
                    input_pos++;
                    input_len++;
                    input[input_len] = '\0';
                }
            } else if (msg.message == WM_KEYDOWN) {
                int vk = (int)msg.wParam;
                if (vk == VK_LEFT) {
                    if (input_pos > 0) input_pos--;
                } else if (vk == VK_RIGHT) {
                    if (input_pos < input_len) input_pos++;
                } else if (vk == VK_HOME) {
                    input_pos = 0;
                } else if (vk == VK_END) {
                    input_pos = input_len;
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        Sleep(20);
    }

    chat_disconnect();

    /* Restore watermark terminal mode */
    term_set_watermark(true);
    term_clear();
}
