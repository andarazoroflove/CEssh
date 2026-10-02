#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "winsock_ce.h"

#undef htons
#define htons(x) ((uint16_t)((((uint16_t)(x) & 0xFF) << 8) | (((uint16_t)(x) >> 8) & 0xFF)))

typedef int (WSAAPI *pfn_WSAStartup)(WORD wVersionRequired, LPWSADATA lpWSAData);
typedef int (WSAAPI *pfn_WSACleanup)(void);
typedef SOCKET (WSAAPI *pfn_socket)(int af, int type, int protocol);
typedef int (WSAAPI *pfn_closesocket)(SOCKET s);
typedef int (WSAAPI *pfn_connect)(SOCKET s, const struct sockaddr *name, int namelen);
typedef int (WSAAPI *pfn_send)(SOCKET s, const char *buf, int len, int flags);
typedef int (WSAAPI *pfn_recv)(SOCKET s, char *buf, int len, int flags);
typedef int (WSAAPI *pfn_select)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout);
typedef int (WSAAPI *pfn_ioctlsocket)(SOCKET s, long cmd, u_long *argp);
typedef struct hostent *(WSAAPI *pfn_gethostbyname)(const char *name);
typedef int (WSAAPI *pfn_gethostname)(char *name, int namelen);
typedef unsigned long (WSAAPI *pfn_inet_addr)(const char *cp);
typedef int (WSAAPI *pfn_WSAGetLastError)(void);

static pfn_WSAStartup      fn_WSAStartup      = NULL;
static pfn_WSACleanup      fn_WSACleanup      = NULL;
static pfn_socket          fn_socket          = NULL;
static pfn_closesocket     fn_closesocket     = NULL;
static pfn_connect         fn_connect         = NULL;
static pfn_send            fn_send            = NULL;
static pfn_recv            fn_recv            = NULL;
static pfn_select          fn_select          = NULL;
static pfn_ioctlsocket     fn_ioctlsocket     = NULL;
static pfn_gethostbyname   fn_gethostbyname   = NULL;
static pfn_gethostname     fn_gethostname     = NULL;
static pfn_inet_addr       fn_inet_addr       = NULL;
static pfn_WSAGetLastError fn_WSAGetLastError = NULL;

static HMODULE s_hWinsock = NULL;
static bool s_initialized = false;

static void *resolve_sym(HMODULE h, LPCWSTR nameW, int ord) {
    FARPROC p = NULL;
    if (nameW) p = GetProcAddressW(h, nameW);
    if (!p && ord > 0) p = GetProcAddressW(h, (LPCWSTR)(uintptr_t)ord);
    return (void *)p;
}

bool winsock_ce_init(void) {
    if (s_initialized) return true;

    if (!s_hWinsock) {
        s_hWinsock = LoadLibraryW(L"ws2.dll");
        if (!s_hWinsock) s_hWinsock = LoadLibraryW(L"winsock.dll");
        if (!s_hWinsock) s_hWinsock = LoadLibraryW(L"wsock32.dll");
    }

    if (!s_hWinsock) {
        return false;
    }

    fn_WSAStartup      = (pfn_WSAStartup)resolve_sym(s_hWinsock, L"WSAStartup", 115);
    fn_WSACleanup      = (pfn_WSACleanup)resolve_sym(s_hWinsock, L"WSACleanup", 116);
    fn_socket          = (pfn_socket)resolve_sym(s_hWinsock, L"socket", 23);
    fn_closesocket     = (pfn_closesocket)resolve_sym(s_hWinsock, L"closesocket", 3);
    fn_connect         = (pfn_connect)resolve_sym(s_hWinsock, L"connect", 4);
    fn_send            = (pfn_send)resolve_sym(s_hWinsock, L"send", 19);
    fn_recv            = (pfn_recv)resolve_sym(s_hWinsock, L"recv", 16);
    fn_select          = (pfn_select)resolve_sym(s_hWinsock, L"select", 18);
    fn_ioctlsocket     = (pfn_ioctlsocket)resolve_sym(s_hWinsock, L"ioctlsocket", 10);
    fn_gethostbyname   = (pfn_gethostbyname)resolve_sym(s_hWinsock, L"gethostbyname", 52);
    fn_gethostname     = (pfn_gethostname)resolve_sym(s_hWinsock, L"gethostname", 57);
    fn_inet_addr       = (pfn_inet_addr)resolve_sym(s_hWinsock, L"inet_addr", 11);
    fn_WSAGetLastError = (pfn_WSAGetLastError)resolve_sym(s_hWinsock, L"WSAGetLastError", 111);

    if (fn_WSAStartup) {
        WSADATA wsa;
        if (fn_WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
            if (fn_WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                return false;
            }
        }
    }

    s_initialized = true;
    return true;
}

void winsock_ce_cleanup(void) {
    if (s_initialized && fn_WSACleanup) {
        fn_WSACleanup();
    }
    if (s_hWinsock) {
        FreeLibrary(s_hWinsock);
        s_hWinsock = NULL;
    }
    s_initialized = false;
}

bool winsock_ce_is_available(void) {
    return winsock_ce_init();
}

static inline int wce_fd_isset(SOCKET s, fd_set *set) {
    if (!set) return 0;
    for (u_int i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == s) return 1;
    }
    return 0;
}

SOCKET winsock_ce_connect(const char *host, uint16_t port, int timeout_ms) {
    if (!winsock_ce_init() || !fn_socket || !fn_connect) {
        return INVALID_SOCKET;
    }

    SOCKET s = fn_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);

    unsigned long ip = INADDR_NONE;
    if (fn_inet_addr) {
        ip = fn_inet_addr(host);
    }
    if (ip == INADDR_NONE && fn_gethostbyname) {
        struct hostent *he = fn_gethostbyname(host);
        if (he && he->h_addr_list && he->h_addr_list[0]) {
            memcpy(&sin.sin_addr, he->h_addr_list[0], sizeof(sin.sin_addr));
        } else {
            if (fn_closesocket) fn_closesocket(s);
            return INVALID_SOCKET;
        }
    } else {
        sin.sin_addr.s_addr = ip;
    }

    /* Set socket non-blocking for timeout connection */
    if (fn_ioctlsocket) {
        u_long mode = 1;
        fn_ioctlsocket(s, FIONBIO, &mode);
    }

    int res = fn_connect(s, (struct sockaddr *)&sin, sizeof(sin));
    if (res != 0) {
        /* Wait for connection via select */
        fd_set wfds, efds;
        wfds.fd_count = 1;
        wfds.fd_array[0] = s;
        efds.fd_count = 1;
        efds.fd_array[0] = s;

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = fn_select(0, NULL, &wfds, &efds, &tv);
        if (sel <= 0 || !wce_fd_isset(s, &wfds) || wce_fd_isset(s, &efds)) {
            if (fn_closesocket) fn_closesocket(s);
            return INVALID_SOCKET;
        }
    }

    /* Return to non-blocking mode */
    if (fn_ioctlsocket) {
        u_long mode = 1;
        fn_ioctlsocket(s, FIONBIO, &mode);
    }

    return s;
}

int winsock_ce_send(SOCKET s, const void *buf, int len) {
    if (!fn_send || s == INVALID_SOCKET || len <= 0) return -1;
    return fn_send(s, (const char *)buf, len, 0);
}

int winsock_ce_recv(SOCKET s, void *buf, int len, int timeout_ms) {
    if (!fn_recv || s == INVALID_SOCKET || len <= 0) return -1;

    if (timeout_ms >= 0 && fn_select) {
        fd_set rfds;
        rfds.fd_count = 1;
        rfds.fd_array[0] = s;

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = fn_select(0, &rfds, NULL, NULL, &tv);
        if (sel <= 0 || !wce_fd_isset(s, &rfds)) {
            return 0; /* Timeout / no data ready */
        }
    }

    return fn_recv(s, (char *)buf, len, 0);
}

bool winsock_ce_has_data(SOCKET s) {
    if (!fn_select || s == INVALID_SOCKET) return false;
    fd_set rfds;
    rfds.fd_count = 1;
    rfds.fd_array[0] = s;
    struct timeval tv = { 0, 0 };
    return (fn_select(0, &rfds, NULL, NULL, &tv) > 0 && wce_fd_isset(s, &rfds));
}

void winsock_ce_close(SOCKET s) {
    if (s != INVALID_SOCKET && fn_closesocket) {
        fn_closesocket(s);
    }
}

bool winsock_ce_get_local_info(char *name, size_t name_len, char *ip_str, size_t ip_len) {
    if (!winsock_ce_init()) return false;
    if (name && name_len > 0) name[0] = '\0';
    if (ip_str && ip_len > 0) ip_str[0] = '\0';

    char local_name[128] = {0};
    if (fn_gethostname && fn_gethostname(local_name, sizeof(local_name) - 1) == 0) {
        if (name && name_len > 0) {
            strncpy(name, local_name, name_len - 1);
            name[name_len - 1] = '\0';
        }
        if (fn_gethostbyname) {
            struct hostent *he = fn_gethostbyname(local_name);
            if (he && he->h_addr_list && he->h_addr_list[0]) {
                unsigned char *b = (unsigned char *)he->h_addr_list[0];
                if (ip_str && ip_len > 0) {
                    snprintf(ip_str, ip_len, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                }
            }
        }
        return true;
    }
    return false;
}
