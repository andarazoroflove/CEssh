#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftp.h"
#include "winsock_ce.h"
#include "../ui/terminal.h"

static SOCKET  s_ctrl_sock = INVALID_SOCKET;
static char    s_ctrl_host[128] = {0};
static uint16_t s_ctrl_port = 21;
static bool    s_connected = false;

static wchar_t s_app_dirW[MAX_PATH] = {0};
static char    s_app_dirA[MAX_PATH] = {0};
static bool    s_app_dir_inited = false;

void ftp_init(void) {
    if (s_app_dir_inited) return;

    /* Get directory where cessh.exe was launched from */
    wchar_t mod_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, mod_path, MAX_PATH)) {
        wchar_t *p = NULL;
        for (int i = 0; mod_path[i] != L'\0'; i++) {
            if (mod_path[i] == L'\\' || mod_path[i] == L'/') {
                p = &mod_path[i];
            }
        }
        if (p) {
            *(p + 1) = L'\0';
        }
        wcscpy(s_app_dirW, mod_path);

        /* Convert to ANSI for display */
        for (int i = 0; i < MAX_PATH; i++) {
            s_app_dirA[i] = (char)s_app_dirW[i];
            if (s_app_dirW[i] == L'\0') break;
        }
    } else {
        wcscpy(s_app_dirW, L"\\");
        strcpy(s_app_dirA, "\\");
    }

    s_app_dir_inited = true;
}

const char *ftp_get_app_dir(void) {
    ftp_init();
    return s_app_dirA;
}

static const char *get_safe_basename(const char *path) {
    if (!path || path[0] == '\0') return "download.dat";
    const char *p = NULL;
    for (const char *s = path; *s != '\0'; s++) {
        if (*s == '/' || *s == '\\') {
            p = s + 1;
        }
    }
    const char *base = p ? p : path;
    while (*base == ' ' || *base == '\t') base++;
    return (*base != '\0') ? base : "download.dat";
}

static void make_local_path(const char *filename, wchar_t *out_pathW, char *out_pathA, size_t max_len) {
    ftp_init();
    const char *safe = get_safe_basename(filename);

    /* Construct WCHAR path */
    wcscpy(out_pathW, s_app_dirW);
    int len = wcslen(out_pathW);
    for (int i = 0; safe[i] != '\0' && (len + i) < (int)(max_len - 1); i++) {
        out_pathW[len + i] = (wchar_t)safe[i];
        out_pathW[len + i + 1] = L'\0';
    }

    /* Construct ANSI path */
    strncpy(out_pathA, s_app_dirA, max_len - 1);
    out_pathA[max_len - 1] = '\0';
    strncat(out_pathA, safe, max_len - strlen(out_pathA) - 1);
}

static int ftp_read_line(SOCKET s, char *buf, size_t max_len, int timeout_ms) {
    size_t pos = 0;
    DWORD start = GetTickCount();

    while (pos < max_len - 1) {
        char ch;
        int r = winsock_ce_recv(s, &ch, 1, 100);
        if (r > 0) {
            buf[pos++] = ch;
            if (ch == '\n') break;
        } else if (r < 0) {
            return -1;
        } else {
            if (GetTickCount() - start >= (DWORD)timeout_ms) {
                return (pos > 0) ? (int)pos : 0;
            }
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

static int ftp_read_reply(char *out_buf, size_t max_len, int *out_code) {
    if (s_ctrl_sock == INVALID_SOCKET) return -1;

    char line[512];
    int code = 0;
    bool multiline = false;

    if (out_buf && max_len > 0) out_buf[0] = '\0';

    while (1) {
        int r = ftp_read_line(s_ctrl_sock, line, sizeof(line), 10000);
        if (r <= 0) return -1;

        if (out_buf && strlen(out_buf) + strlen(line) < max_len - 1) {
            strcat(out_buf, line);
        }

        if (line[0] >= '1' && line[0] <= '5' &&
            line[1] >= '0' && line[1] <= '9' &&
            line[2] >= '0' && line[2] <= '9') {
            int line_code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
            if (!multiline) {
                code = line_code;
                if (line[3] == '-') {
                    multiline = true;
                } else if (line[3] == ' ' || line[3] == '\r' || line[3] == '\n') {
                    break;
                }
            } else {
                if (line_code == code && line[3] == ' ') {
                    break;
                }
            }
        }
    }

    if (out_code) *out_code = code;
    return code;
}

static int ftp_send_cmd(const char *fmt, ...) {
    if (s_ctrl_sock == INVALID_SOCKET) return -1;

    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    int len = strlen(buf);
    return winsock_ce_send(s_ctrl_sock, buf, len);
}

bool ftp_is_connected(void) {
    return (s_connected && s_ctrl_sock != INVALID_SOCKET);
}

void ftp_disconnect(void) {
    if (s_ctrl_sock != INVALID_SOCKET) {
        ftp_send_cmd("QUIT\r\n");
        winsock_ce_close(s_ctrl_sock);
        s_ctrl_sock = INVALID_SOCKET;
    }
    s_connected = false;
}

static SOCKET ftp_open_pasv_data(void) {
    if (!ftp_is_connected()) return INVALID_SOCKET;

    if (ftp_send_cmd("PASV\r\n") <= 0) {
        term_puts("Error: Failed to send PASV command.\r\n");
        return INVALID_SOCKET;
    }

    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) <= 0 || code != 227) {
        term_printf("Error: PASV mode failed: %s", reply);
        return INVALID_SOCKET;
    }

    /* Format: 227 Entering Passive Mode (192,168,1,50,195,84) */
    char *p = strchr(reply, '(');
    if (!p) {
        term_puts("Error: Malformed PASV response.\r\n");
        return INVALID_SOCKET;
    }
    p++;

    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        term_puts("Error: Could not parse PASV address.\r\n");
        return INVALID_SOCKET;
    }

    char data_ip[64];
    /* If server replied with local 127.0.0.1 or 0.0.0.0, use the control connection host */
    if (h1 == 127 || h1 == 0) {
        strncpy(data_ip, s_ctrl_host, sizeof(data_ip) - 1);
        data_ip[sizeof(data_ip) - 1] = '\0';
    } else {
        snprintf(data_ip, sizeof(data_ip), "%d.%d.%d.%d", h1, h2, h3, h4);
    }

    uint16_t data_port = (uint16_t)((p1 << 8) | p2);

    SOCKET s_data = winsock_ce_connect(data_ip, data_port, 5000);
    if (s_data == INVALID_SOCKET) {
        term_printf("Error: Could not connect to data port %s:%u\r\n", data_ip, data_port);
    }
    return s_data;
}

bool ftp_connect(const char *host, uint16_t port, const char *user, const char *pass) {
    ftp_init();
    ftp_disconnect();

    if (!host || host[0] == '\0') {
        term_puts("Usage: ftp [user@]host[:port]\r\n");
        return false;
    }
    if (port == 0) port = 21;

    strncpy(s_ctrl_host, host, sizeof(s_ctrl_host) - 1);
    s_ctrl_port = port;

    term_printf("Connecting to FTP server at %s:%u...\r\n", host, port);
    s_ctrl_sock = winsock_ce_connect(host, port, 5000);
    if (s_ctrl_sock == INVALID_SOCKET) {
        term_puts("Error: Connection to FTP server failed.\r\n\r\n");
        return false;
    }

    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) <= 0 || code != 220) {
        term_printf("Error: Unexpected FTP greeting: %s\r\n", reply);
        ftp_disconnect();
        return false;
    }

    term_puts(reply);

    /* Send USER */
    const char *u = (user && user[0]) ? user : "anonymous";
    term_printf("USER %s\r\n", u);
    ftp_send_cmd("USER %s\r\n", u);

    if (ftp_read_reply(reply, sizeof(reply), &code) <= 0) {
        term_puts("Error: No reply to USER command.\r\n");
        ftp_disconnect();
        return false;
    }

    if (code == 331) {
        /* Password required */
        const char *p = (pass && pass[0]) ? pass : "jornada@wince";
        ftp_send_cmd("PASS %s\r\n", p);
        if (ftp_read_reply(reply, sizeof(reply), &code) <= 0) {
            term_puts("Error: No reply to PASS command.\r\n");
            ftp_disconnect();
            return false;
        }
    }

    if (code != 230) {
        term_printf("Error: FTP login failed (%d): %s\r\n", code, reply);
        ftp_disconnect();
        return false;
    }

    term_puts(reply);

    /* Set Binary mode */
    ftp_send_cmd("TYPE I\r\n");
    ftp_read_reply(reply, sizeof(reply), &code);

    s_connected = true;

    term_puts("--------------------------------------------------------------------------------\r\n");
    term_printf("  FTP Session Active (Sandbox Directory: %s)\r\n", s_app_dirA);
    term_puts("  Commands: ls, get <file>, put <file>, cd <dir>, pwd, binary, bye\r\n");
    term_puts("--------------------------------------------------------------------------------\r\n\r\n");
    return true;
}

bool ftp_cmd_pwd(void) {
    if (!ftp_is_connected()) return false;
    ftp_send_cmd("PWD\r\n");
    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) > 0) {
        term_puts(reply);
        return (code == 257);
    }
    return false;
}

bool ftp_cmd_cd(const char *path) {
    if (!ftp_is_connected()) return false;
    ftp_send_cmd("CWD %s\r\n", path ? path : "/");
    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) > 0) {
        term_puts(reply);
        return (code == 250);
    }
    return false;
}

bool ftp_cmd_binary(void) {
    if (!ftp_is_connected()) return false;
    ftp_send_cmd("TYPE I\r\n");
    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) > 0) {
        term_puts(reply);
        return (code == 200);
    }
    return false;
}

bool ftp_cmd_ascii(void) {
    if (!ftp_is_connected()) return false;
    ftp_send_cmd("TYPE A\r\n");
    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) > 0) {
        term_puts(reply);
        return (code == 200);
    }
    return false;
}

bool ftp_cmd_raw(const char *cmd_line) {
    if (!ftp_is_connected() || !cmd_line) return false;
    ftp_send_cmd("%s\r\n", cmd_line);
    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) > 0) {
        term_puts(reply);
        return true;
    }
    return false;
}

bool ftp_cmd_ls(const char *path) {
    if (!ftp_is_connected()) return false;

    SOCKET s_data = ftp_open_pasv_data();
    if (s_data == INVALID_SOCKET) return false;

    if (path && path[0]) {
        ftp_send_cmd("LIST %s\r\n", path);
    } else {
        ftp_send_cmd("LIST\r\n");
    }

    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) <= 0 || (code != 150 && code != 125)) {
        term_printf("Error: LIST rejected: %s", reply);
        winsock_ce_close(s_data);
        return false;
    }

    term_puts(reply);

    /* Read directory listing from data connection */
    char buf[1024];
    while (1) {
        int r = winsock_ce_recv(s_data, buf, sizeof(buf) - 1, 3000);
        if (r > 0) {
            buf[r] = '\0';
            term_write(buf, r);
        } else {
            break;
        }
    }
    winsock_ce_close(s_data);

    /* Read transfer completion reply */
    ftp_read_reply(reply, sizeof(reply), &code);
    term_puts(reply);
    return true;
}

bool ftp_cmd_get(const char *remote_file, const char *local_file) {
    if (!ftp_is_connected() || !remote_file || remote_file[0] == '\0') {
        term_puts("Usage: get <remote_filename> [local_filename]\r\n");
        return false;
    }

    const char *target_local = (local_file && local_file[0]) ? local_file : remote_file;
    wchar_t local_pathW[MAX_PATH];
    char local_pathA[MAX_PATH];
    make_local_path(target_local, local_pathW, local_pathA, MAX_PATH);

    term_printf("Saving to application sandbox: %s\r\n", local_pathA);

    HANDLE hFile = CreateFileW(local_pathW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        term_printf("Error: Could not open local file for writing: %s (err=%lu)\r\n", local_pathA, GetLastError());
        return false;
    }

    SOCKET s_data = ftp_open_pasv_data();
    if (s_data == INVALID_SOCKET) {
        CloseHandle(hFile);
        return false;
    }

    ftp_send_cmd("RETR %s\r\n", remote_file);

    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) <= 0 || (code != 150 && code != 125)) {
        term_printf("Error: Server refused RETR: %s", reply);
        winsock_ce_close(s_data);
        CloseHandle(hFile);
        return false;
    }

    term_puts(reply);

    char buf[2048];
    DWORD total_bytes = 0;
    DWORD last_report = GetTickCount();

    while (1) {
        int r = winsock_ce_recv(s_data, buf, sizeof(buf), 5000);
        if (r > 0) {
            DWORD written = 0;
            WriteFile(hFile, buf, (DWORD)r, &written, NULL);
            total_bytes += written;

            DWORD now = GetTickCount();
            if (now - last_report >= 1000) {
                term_printf("  Progress: %lu bytes received...\r\n", (unsigned long)total_bytes);
                last_report = now;
            }
        } else {
            break;
        }
    }

    winsock_ce_close(s_data);
    CloseHandle(hFile);

    ftp_read_reply(reply, sizeof(reply), &code);
    term_puts(reply);

    term_printf("Successfully downloaded %s (%lu bytes saved to %s)\r\n\r\n",
                get_safe_basename(target_local), (unsigned long)total_bytes, local_pathA);
    return true;
}

bool ftp_cmd_put(const char *local_file, const char *remote_file) {
    if (!ftp_is_connected() || !local_file || local_file[0] == '\0') {
        term_puts("Usage: put <local_filename> [remote_filename]\r\n");
        return false;
    }

    wchar_t local_pathW[MAX_PATH];
    char local_pathA[MAX_PATH];
    make_local_path(local_file, local_pathW, local_pathA, MAX_PATH);

    HANDLE hFile = CreateFileW(local_pathW, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        term_printf("Error: File not found in CEssh directory: %s\r\n", local_pathA);
        return false;
    }

    const char *remote_target = (remote_file && remote_file[0]) ? remote_file : get_safe_basename(local_file);

    SOCKET s_data = ftp_open_pasv_data();
    if (s_data == INVALID_SOCKET) {
        CloseHandle(hFile);
        return false;
    }

    ftp_send_cmd("STOR %s\r\n", remote_target);

    char reply[512];
    int code = 0;
    if (ftp_read_reply(reply, sizeof(reply), &code) <= 0 || (code != 150 && code != 125)) {
        term_printf("Error: Server refused STOR: %s", reply);
        winsock_ce_close(s_data);
        CloseHandle(hFile);
        return false;
    }

    term_puts(reply);

    char buf[2048];
    DWORD total_bytes = 0;
    DWORD bytes_read = 0;

    while (ReadFile(hFile, buf, sizeof(buf), &bytes_read, NULL) && bytes_read > 0) {
        int sent = winsock_ce_send(s_data, buf, (int)bytes_read);
        if (sent <= 0) break;
        total_bytes += sent;
    }

    winsock_ce_close(s_data);
    CloseHandle(hFile);

    ftp_read_reply(reply, sizeof(reply), &code);
    term_puts(reply);

    term_printf("Successfully uploaded %s (%lu bytes transferred)\r\n\r\n",
                remote_target, (unsigned long)total_bytes);
    return true;
}
