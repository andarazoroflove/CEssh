#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "prompt.h"
#include "../ui/terminal.h"
#include "../ssh/ssh2.h"
#include "../net/winsock_ce.h"
#include "../net/ping.h"
#include "../net/ftp.h"

typedef enum {
    MODE_PROMPT = 0,
    MODE_GET_USER,
    MODE_GET_PASS,
    MODE_SSH_ACTIVE,
    MODE_FTP_GET_USER,
    MODE_FTP_GET_PASS,
    MODE_FTP_PROMPT
} prompt_mode_t;

static prompt_mode_t s_mode = MODE_PROMPT;

static char s_cmd_line[256];
static size_t s_cmd_len = 0;
static size_t s_cmd_pos = 0;

/* Command History */
#define HIST_MAX 16
static char s_history[HIST_MAX][256];
static int  s_hist_count = 0;
static int  s_hist_idx = -1;

/* Pending SSH connection details */
static char     s_pending_host[128];
static uint16_t s_pending_port = 22;
static char     s_pending_user[64];
static char     s_pending_pass[64];

/* Pending FTP connection details */
static char     s_ftp_host[128];
static uint16_t s_ftp_port = 21;
static char     s_ftp_user[64];
static char     s_ftp_pass[64];

static void print_prompt(void) {
    term_set_fg(COLOR_DKGRAY);
    term_puts("?/");
    term_set_fg(COLOR_BLACK);
    term_puts(" ");
}

static void print_ftp_prompt(void) {
    term_set_fg(COLOR_DKGRAY);
    term_puts("ftp");
    term_set_fg(COLOR_BLACK);
    term_puts("> ");
}

static void hist_add(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;
    if (s_hist_count > 0 && strcmp(s_history[s_hist_count - 1], cmd) == 0) return;

    if (s_hist_count < HIST_MAX) {
        strncpy(s_history[s_hist_count], cmd, 255);
        s_history[s_hist_count][255] = '\0';
        s_hist_count++;
    } else {
        for (int i = 0; i < HIST_MAX - 1; i++) {
            strcpy(s_history[i], s_history[i + 1]);
        }
        strncpy(s_history[HIST_MAX - 1], cmd, 255);
        s_history[HIST_MAX - 1][255] = '\0';
    }
}

static void print_help(void) {
    term_puts("Available Commands:\r\n");
    term_puts("  ssh [user@]host[:port]  - Connect to remote SSH-2 server\r\n");
    term_puts("  ftp [user@]host[:port]  - Interactive FTP (transfers to CEssh folder)\r\n");
    term_puts("  ping <host> [count]     - Standard ICMP Ping (default 4 echo packets)\r\n");
    term_puts("  tcpping <host> [port]   - TCP port reachability test\r\n");
    term_puts("  ip / net                - Show Winsock & local IP address\r\n");
    term_puts("  clear / cls             - Clear screen buffer\r\n");
    term_puts("  theme                   - Toggle black-on-white / white-on-black\r\n");
    term_puts("  help / ?                - Show this command reference\r\n");
    term_puts("  exit / quit             - Exit CEssh\r\n\r\n");
    term_puts("Tips:\r\n");
    term_puts("  - During SSH session, press Ctrl+] to disconnect.\r\n");
    term_puts("  - FTP files are strictly stored in / read from the CEssh folder.\r\n\r\n");
}

static void print_ftp_help(void) {
    term_puts("FTP Commands (Transfers locked to CEssh application folder):\r\n");
    term_puts("  ls / dir [path]      - List remote directory contents\r\n");
    term_puts("  get <file> [local]   - Download remote file into CEssh directory\r\n");
    term_puts("  put <file> [remote]  - Upload local file from CEssh directory\r\n");
    term_puts("  cd <path>            - Change remote directory\r\n");
    term_puts("  pwd                  - Print remote working directory\r\n");
    term_puts("  binary / bin         - Set binary transfer mode (8-bit clean)\r\n");
    term_puts("  ascii                - Set ASCII transfer mode\r\n");
    term_puts("  quote <cmd>          - Send raw FTP protocol command\r\n");
    term_puts("  bye / quit / exit    - Close FTP connection and return to '?/' prompt\r\n\r\n");
}

static void do_ip_info(void) {
    term_puts("Winsock Subsystem Status:\r\n");
    if (!winsock_ce_is_available()) {
        term_puts("  Winsock: Not loaded (ws2.dll / winsock.dll unavailable)\r\n\r\n");
        return;
    }
    term_puts("  Winsock: Active (Orinoco Gold Wi-Fi / CF-NIC ready)\r\n");

    char name[128] = {0};
    char ip_str[64] = {0};
    if (winsock_ce_get_local_info(name, sizeof(name), ip_str, sizeof(ip_str))) {
        if (name[0]) term_printf("  Host Name: %s\r\n", name);
        if (ip_str[0]) term_printf("  Local IP: %s\r\n", ip_str);
    } else {
        term_puts("  (Local network address pending DHCP or static assignment)\r\n");
    }

    term_printf("  CEssh Directory: %s\r\n", ftp_get_app_dir());
    term_puts("\r\n");
}

/* ------------------------------------------------------------------ */
/* SSH Connection Handlers                                            */
/* ------------------------------------------------------------------ */
static void start_ssh_connection(void) {
    term_printf("Starting SSH session to %s@%s:%u...\r\n",
                s_pending_user[0] ? s_pending_user : "root",
                s_pending_host, s_pending_port);

    bool ok = ssh2_connect(s_pending_host, s_pending_port, s_pending_user, s_pending_pass);
    /* Securely clear password from memory */
    memset(s_pending_pass, 0, sizeof(s_pending_pass));

    if (ok) {
        s_mode = MODE_SSH_ACTIVE;
    } else {
        term_puts("\r\n");
        s_mode = MODE_PROMPT;
        print_prompt();
    }
}

static void parse_ssh_args(char *args) {
    s_pending_host[0] = '\0';
    s_pending_user[0] = '\0';
    s_pending_pass[0] = '\0';
    s_pending_port = 22;

    while (*args == ' ') args++;
    if (*args == '\0') {
        term_puts("Usage: ssh [user@]host[:port] or ssh host [port]\r\n");
        print_prompt();
        return;
    }

    char target[128] = {0};
    char extra_port[32] = {0};

    char *space = strchr(args, ' ');
    if (space) {
        *space = '\0';
        strncpy(target, args, sizeof(target) - 1);
        strncpy(extra_port, space + 1, sizeof(extra_port) - 1);
    } else {
        strncpy(target, args, sizeof(target) - 1);
    }

    char *at = strchr(target, '@');
    char *host_part = target;
    if (at) {
        *at = '\0';
        strncpy(s_pending_user, target, sizeof(s_pending_user) - 1);
        host_part = at + 1;
    }

    char *colon = strchr(host_part, ':');
    if (colon) {
        *colon = '\0';
        s_pending_port = (uint16_t)atoi(colon + 1);
    } else if (extra_port[0] != '\0') {
        s_pending_port = (uint16_t)atoi(extra_port);
    }

    strncpy(s_pending_host, host_part, sizeof(s_pending_host) - 1);

    if (s_pending_port == 0) s_pending_port = 22;

    if (s_pending_user[0] == '\0') {
        s_mode = MODE_GET_USER;
        term_puts("Username: ");
        return;
    }

    s_mode = MODE_GET_PASS;
    term_puts("Password: ");
}

/* ------------------------------------------------------------------ */
/* FTP Connection & Interactive Handlers                              */
/* ------------------------------------------------------------------ */
static void start_ftp_connection(void) {
    bool ok = ftp_connect(s_ftp_host, s_ftp_port, s_ftp_user, s_ftp_pass);
    memset(s_ftp_pass, 0, sizeof(s_ftp_pass));

    if (ok) {
        s_mode = MODE_FTP_PROMPT;
        print_ftp_prompt();
    } else {
        s_mode = MODE_PROMPT;
        print_prompt();
    }
}

static void parse_ftp_args(char *args) {
    s_ftp_host[0] = '\0';
    s_ftp_user[0] = '\0';
    s_ftp_pass[0] = '\0';
    s_ftp_port = 21;

    while (*args == ' ') args++;
    if (*args == '\0') {
        term_puts("Usage: ftp [user@]host[:port] or ftp host [port]\r\n");
        print_prompt();
        return;
    }

    char target[128] = {0};
    char extra_port[32] = {0};

    char *space = strchr(args, ' ');
    if (space) {
        *space = '\0';
        strncpy(target, args, sizeof(target) - 1);
        strncpy(extra_port, space + 1, sizeof(extra_port) - 1);
    } else {
        strncpy(target, args, sizeof(target) - 1);
    }

    char *at = strchr(target, '@');
    char *host_part = target;
    if (at) {
        *at = '\0';
        strncpy(s_ftp_user, target, sizeof(s_ftp_user) - 1);
        host_part = at + 1;
    }

    char *colon = strchr(host_part, ':');
    if (colon) {
        *colon = '\0';
        s_ftp_port = (uint16_t)atoi(colon + 1);
    } else if (extra_port[0] != '\0') {
        s_ftp_port = (uint16_t)atoi(extra_port);
    }

    strncpy(s_ftp_host, host_part, sizeof(s_ftp_host) - 1);
    if (s_ftp_port == 0) s_ftp_port = 21;

    if (s_ftp_user[0] == '\0') {
        s_mode = MODE_FTP_GET_USER;
        term_puts("FTP Username [anonymous]: ");
        return;
    }

    s_mode = MODE_FTP_GET_PASS;
    term_puts("FTP Password: ");
}

static void execute_ftp_command(char *cmd) {
    while (*cmd == ' ') cmd++;
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t' || cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        cmd[--len] = '\0';
    }

    if (len == 0) {
        print_ftp_prompt();
        return;
    }

    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        ftp_cmd_ls(NULL);
    } else if (strncmp(cmd, "ls ", 3) == 0) {
        ftp_cmd_ls(cmd + 3);
    } else if (strncmp(cmd, "dir ", 4) == 0) {
        ftp_cmd_ls(cmd + 4);
    } else if (strncmp(cmd, "get ", 4) == 0) {
        char *rem = cmd + 4;
        while (*rem == ' ') rem++;
        char *loc = strchr(rem, ' ');
        if (loc) {
            *loc = '\0';
            loc++;
            while (*loc == ' ') loc++;
        }
        ftp_cmd_get(rem, loc);
    } else if (strncmp(cmd, "put ", 4) == 0) {
        char *loc = cmd + 4;
        while (*loc == ' ') loc++;
        char *rem = strchr(loc, ' ');
        if (rem) {
            *rem = '\0';
            rem++;
            while (*rem == ' ') rem++;
        }
        ftp_cmd_put(loc, rem);
    } else if (strncmp(cmd, "cd ", 3) == 0) {
        ftp_cmd_cd(cmd + 3);
    } else if (strcmp(cmd, "pwd") == 0) {
        ftp_cmd_pwd();
    } else if (strcmp(cmd, "binary") == 0 || strcmp(cmd, "bin") == 0) {
        ftp_cmd_binary();
    } else if (strcmp(cmd, "ascii") == 0) {
        ftp_cmd_ascii();
    } else if (strncmp(cmd, "quote ", 6) == 0) {
        ftp_cmd_raw(cmd + 6);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_ftp_help();
    } else if (strcmp(cmd, "bye") == 0 || strcmp(cmd, "quit") == 0 ||
               strcmp(cmd, "exit") == 0 || strcmp(cmd, "close") == 0) {
        ftp_disconnect();
        s_mode = MODE_PROMPT;
        term_puts("FTP session closed.\r\n\r\n");
        print_prompt();
        return;
    } else {
        term_printf("Unknown FTP command '%s'. Type 'help' for available commands.\r\n", cmd);
    }

    print_ftp_prompt();
}

/* ------------------------------------------------------------------ */
/* Main Shell Command Execution                                       */
/* ------------------------------------------------------------------ */
static void execute_command(char *cmd) {
    while (*cmd == ' ') cmd++;
    size_t len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t' || cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        cmd[--len] = '\0';
    }

    if (len == 0) {
        print_prompt();
        return;
    }

    hist_add(cmd);

    if (strncmp(cmd, "ssh ", 4) == 0) {
        parse_ssh_args(cmd + 4);
    } else if (strcmp(cmd, "ssh") == 0) {
        term_puts("Usage: ssh [user@]host[:port]\r\n");
        print_prompt();
    } else if (strncmp(cmd, "connect ", 8) == 0) {
        parse_ssh_args(cmd + 8);
    } else if (strcmp(cmd, "connect") == 0) {
        term_puts("Usage: connect [user@]host[:port]\r\n");
        print_prompt();
    } else if (strncmp(cmd, "ftp ", 4) == 0) {
        parse_ftp_args(cmd + 4);
    } else if (strcmp(cmd, "ftp") == 0) {
        term_puts("Usage: ftp [user@]host[:port]\r\n");
        print_prompt();
    } else if (strncmp(cmd, "ping ", 5) == 0) {
        char *h = cmd + 5;
        while (*h == ' ') h++;
        char *c_str = strchr(h, ' ');
        int count = 4;
        if (c_str) {
            *c_str = '\0';
            count = atoi(c_str + 1);
        }
        ping_run(h, count);
        print_prompt();
    } else if (strcmp(cmd, "ping") == 0) {
        term_puts("Usage: ping <host> [count]\r\n");
        print_prompt();
    } else if (strncmp(cmd, "tcpping ", 8) == 0) {
        char *h = cmd + 8;
        while (*h == ' ') h++;
        char *p_str = strchr(h, ' ');
        uint16_t p = 80;
        if (p_str) {
            *p_str = '\0';
            p = (uint16_t)atoi(p_str + 1);
        }
        ping_tcp(h, p, 4000);
        print_prompt();
    } else if (strcmp(cmd, "tcpping") == 0) {
        term_puts("Usage: tcpping <host> [port]\r\n");
        print_prompt();
    } else if (strcmp(cmd, "ip") == 0 || strcmp(cmd, "net") == 0) {
        do_ip_info();
        print_prompt();
    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        term_clear();
        print_prompt();
    } else if (strcmp(cmd, "theme") == 0 || strcmp(cmd, "invert") == 0) {
        term_toggle_invert();
        term_clear();
        print_prompt();
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_help();
        print_prompt();
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        term_puts("Exiting CEssh...\r\n");
        PostQuitMessage(0);
    } else {
        term_printf("Unknown command '%s'. Type 'help' for command list.\r\n", cmd);
        print_prompt();
    }
}

void prompt_init(void) {
    s_mode = MODE_PROMPT;
    s_cmd_len = 0;
    s_cmd_pos = 0;
    s_cmd_line[0] = '\0';
    s_hist_idx = -1;

    ftp_init();
    ssh2_init(term_write);

    term_clear();
    term_set_fg(COLOR_BLACK);
    term_set_bg(COLOR_WHITE);

    term_puts("================================================================================\r\n");
    term_puts("  CEssh v1.1.0 (Windows CE 3.0 / HPC 2000)\r\n");
    term_puts("  HP Jornada 720 StrongARM SA-1110 SSH-2, FTP & ICMP Ping Terminal\r\n");
    term_puts("================================================================================\r\n");
    term_puts("Type 'help' for commands, 'ssh user@host', 'ftp host', or 'ping host'.\r\n\r\n");

    print_prompt();
}

bool prompt_is_ssh_active(void) {
    return (s_mode == MODE_SSH_ACTIVE && ssh2_is_connected());
}

void prompt_end_ssh(void) {
    if (s_mode == MODE_SSH_ACTIVE) {
        ssh2_disconnect();
        s_mode = MODE_PROMPT;
        term_puts("\r\n[SSH session closed]\r\n\r\n");
        print_prompt();
    }
}

void prompt_handle_char(char c) {
    if (s_mode == MODE_SSH_ACTIVE) {
        if (c == 29) { /* Ctrl + ] */
            prompt_end_ssh();
            return;
        }
        ssh2_send_data(&c, 1);
        return;
    }

    if (s_mode == MODE_FTP_PROMPT) {
        if (c == '\r' || c == '\n') {
            term_puts("\r\n");
            s_cmd_line[s_cmd_len] = '\0';
            char buf[256];
            strcpy(buf, s_cmd_line);
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_cmd_line[0] = '\0';
            execute_ftp_command(buf);
        } else if (c == '\b' || c == 127) {
            if (s_cmd_pos > 0) {
                s_cmd_pos--;
                s_cmd_len--;
                term_putc('\b');
                term_putc(' ');
                term_putc('\b');
            }
        } else if (c == 3) { /* Ctrl+C */
            term_puts("^C\r\n");
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_cmd_line[0] = '\0';
            print_ftp_prompt();
        } else if ((unsigned char)c >= 32 && s_cmd_len < sizeof(s_cmd_line) - 1) {
            s_cmd_line[s_cmd_pos++] = c;
            s_cmd_len++;
            s_cmd_line[s_cmd_len] = '\0';
            term_putc(c);
        }
        return;
    }

    if (s_mode == MODE_PROMPT) {
        if (c == '\r' || c == '\n') {
            term_puts("\r\n");
            s_cmd_line[s_cmd_len] = '\0';
            char buf[256];
            strcpy(buf, s_cmd_line);
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_cmd_line[0] = '\0';
            s_hist_idx = -1;
            execute_command(buf);
        } else if (c == '\b' || c == 127) {
            if (s_cmd_pos > 0) {
                s_cmd_pos--;
                s_cmd_len--;
                term_putc('\b');
                term_putc(' ');
                term_putc('\b');
            }
        } else if (c == 3) { /* Ctrl+C */
            term_puts("^C\r\n");
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_cmd_line[0] = '\0';
            s_hist_idx = -1;
            print_prompt();
        } else if (c == 12) { /* Ctrl+L */
            term_clear();
            print_prompt();
            if (s_cmd_len > 0) {
                term_puts(s_cmd_line);
            }
        } else if ((unsigned char)c >= 32 && s_cmd_len < sizeof(s_cmd_line) - 1) {
            s_cmd_line[s_cmd_pos++] = c;
            s_cmd_len++;
            s_cmd_line[s_cmd_len] = '\0';
            term_putc(c);
        }
        return;
    }

    /* SSH credential prompts */
    if (s_mode == MODE_GET_USER) {
        if (c == '\r' || c == '\n') {
            term_puts("\r\n");
            s_cmd_line[s_cmd_len] = '\0';
            if (s_cmd_len > 0) {
                strncpy(s_pending_user, s_cmd_line, sizeof(s_pending_user) - 1);
            } else {
                strcpy(s_pending_user, "root");
            }
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_cmd_line[0] = '\0';

            s_mode = MODE_GET_PASS;
            term_puts("Password: ");
        } else if (c == '\b' || c == 127) {
            if (s_cmd_pos > 0) {
                s_cmd_pos--;
                s_cmd_len--;
                term_putc('\b');
                term_putc(' ');
                term_putc('\b');
            }
        } else if (c == 3) {
            term_puts("^C\r\n");
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_mode = MODE_PROMPT;
            print_prompt();
        } else if ((unsigned char)c >= 32 && s_cmd_len < sizeof(s_pending_user) - 1) {
            s_cmd_line[s_cmd_pos++] = c;
            s_cmd_len++;
            term_putc(c);
        }
        return;
    }

    if (s_mode == MODE_GET_PASS) {
        if (c == '\r' || c == '\n') {
            term_puts("\r\n");
            s_cmd_line[s_cmd_len] = '\0';
            strncpy(s_pending_pass, s_cmd_line, sizeof(s_pending_pass) - 1);
            s_cmd_len = 0;
            s_cmd_pos = 0;
            memset(s_cmd_line, 0, sizeof(s_cmd_line));

            start_ssh_connection();
        } else if (c == '\b' || c == 127) {
            if (s_cmd_pos > 0) {
                s_cmd_pos--;
                s_cmd_len--;
                term_putc('\b');
                term_putc(' ');
                term_putc('\b');
            }
        } else if (c == 3) {
            term_puts("^C\r\n");
            s_cmd_len = 0;
            s_cmd_pos = 0;
            memset(s_pending_pass, 0, sizeof(s_pending_pass));
            s_mode = MODE_PROMPT;
            print_prompt();
        } else if ((unsigned char)c >= 32 && s_cmd_len < sizeof(s_pending_pass) - 1) {
            s_cmd_line[s_cmd_pos++] = c;
            s_cmd_len++;
            term_putc('*');
        }
        return;
    }

    /* FTP credential prompts */
    if (s_mode == MODE_FTP_GET_USER) {
        if (c == '\r' || c == '\n') {
            term_puts("\r\n");
            s_cmd_line[s_cmd_len] = '\0';
            if (s_cmd_len > 0) {
                strncpy(s_ftp_user, s_cmd_line, sizeof(s_ftp_user) - 1);
            } else {
                strcpy(s_ftp_user, "anonymous");
            }
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_cmd_line[0] = '\0';

            s_mode = MODE_FTP_GET_PASS;
            term_puts("FTP Password [jornada@wince]: ");
        } else if (c == '\b' || c == 127) {
            if (s_cmd_pos > 0) {
                s_cmd_pos--;
                s_cmd_len--;
                term_putc('\b');
                term_putc(' ');
                term_putc('\b');
            }
        } else if (c == 3) {
            term_puts("^C\r\n");
            s_cmd_len = 0;
            s_cmd_pos = 0;
            s_mode = MODE_PROMPT;
            print_prompt();
        } else if ((unsigned char)c >= 32 && s_cmd_len < sizeof(s_ftp_user) - 1) {
            s_cmd_line[s_cmd_pos++] = c;
            s_cmd_len++;
            term_putc(c);
        }
        return;
    }

    if (s_mode == MODE_FTP_GET_PASS) {
        if (c == '\r' || c == '\n') {
            term_puts("\r\n");
            s_cmd_line[s_cmd_len] = '\0';
            if (s_cmd_len > 0) {
                strncpy(s_ftp_pass, s_cmd_line, sizeof(s_ftp_pass) - 1);
            } else {
                strcpy(s_ftp_pass, "jornada@wince");
            }
            s_cmd_len = 0;
            s_cmd_pos = 0;
            memset(s_cmd_line, 0, sizeof(s_cmd_line));

            start_ftp_connection();
        } else if (c == '\b' || c == 127) {
            if (s_cmd_pos > 0) {
                s_cmd_pos--;
                s_cmd_len--;
                term_putc('\b');
                term_putc(' ');
                term_putc('\b');
            }
        } else if (c == 3) {
            term_puts("^C\r\n");
            s_cmd_len = 0;
            s_cmd_pos = 0;
            memset(s_ftp_pass, 0, sizeof(s_ftp_pass));
            s_mode = MODE_PROMPT;
            print_prompt();
        } else if ((unsigned char)c >= 32 && s_cmd_len < sizeof(s_ftp_pass) - 1) {
            s_cmd_line[s_cmd_pos++] = c;
            s_cmd_len++;
            term_putc('*');
        }
        return;
    }
}

void prompt_handle_keydown(int vk) {
    if (s_mode == MODE_SSH_ACTIVE) {
        switch (vk) {
            case VK_UP:     ssh2_send_data("\x1b[A", 3); break;
            case VK_DOWN:   ssh2_send_data("\x1b[B", 3); break;
            case VK_RIGHT:  ssh2_send_data("\x1b[C", 3); break;
            case VK_LEFT:   ssh2_send_data("\x1b[D", 3); break;
            case VK_HOME:   ssh2_send_data("\x1b[1~", 4); break;
            case VK_END:    ssh2_send_data("\x1b[4~", 4); break;
            case VK_PRIOR:  ssh2_send_data("\x1b[5~", 4); break;
            case VK_NEXT:   ssh2_send_data("\x1b[6~", 4); break;
            case VK_DELETE: ssh2_send_data("\x1b[3~", 4); break;
            case VK_TAB:    ssh2_send_data("\t", 1); break;
            case VK_ESCAPE: ssh2_send_data("\x1b", 1); break;
            default: break;
        }
        return;
    }

    if (s_mode == MODE_PROMPT) {
        if (vk == VK_UP) {
            if (s_hist_count > 0) {
                if (s_hist_idx == -1) s_hist_idx = s_hist_count - 1;
                else if (s_hist_idx > 0) s_hist_idx--;

                while (s_cmd_pos > 0) {
                    term_putc('\b');
                    term_putc(' ');
                    term_putc('\b');
                    s_cmd_pos--;
                }

                strcpy(s_cmd_line, s_history[s_hist_idx]);
                s_cmd_len = strlen(s_cmd_line);
                s_cmd_pos = s_cmd_len;
                term_puts(s_cmd_line);
            }
        } else if (vk == VK_DOWN) {
            if (s_hist_idx != -1) {
                if (s_hist_idx < s_hist_count - 1) {
                    s_hist_idx++;
                    while (s_cmd_pos > 0) {
                        term_putc('\b');
                        term_putc(' ');
                        term_putc('\b');
                        s_cmd_pos--;
                    }
                    strcpy(s_cmd_line, s_history[s_hist_idx]);
                    s_cmd_len = strlen(s_cmd_line);
                    s_cmd_pos = s_cmd_len;
                    term_puts(s_cmd_line);
                } else {
                    s_hist_idx = -1;
                    while (s_cmd_pos > 0) {
                        term_putc('\b');
                        term_putc(' ');
                        term_putc('\b');
                        s_cmd_pos--;
                    }
                    s_cmd_len = 0;
                    s_cmd_pos = 0;
                    s_cmd_line[0] = '\0';
                }
            }
        }
    }
}
