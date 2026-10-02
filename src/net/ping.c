#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ping.h"
#include "winsock_ce.h"
#include "../ui/terminal.h"

typedef struct {
    unsigned char Ttl;
    unsigned char Tos;
    unsigned char Flags;
    unsigned char OptionsSize;
    unsigned char *OptionsData;
} IP_OPTION_INFORMATION;

typedef struct {
    uint32_t Address;
    uint32_t Status;
    uint32_t RoundTripTime;
    uint16_t DataSize;
    uint16_t Reserved;
    void *Data;
    IP_OPTION_INFORMATION Options;
} ICMP_ECHO_REPLY;

typedef HANDLE (WINAPI *pfn_IcmpCreateFile)(void);
typedef DWORD (WINAPI *pfn_IcmpSendEcho)(HANDLE IcmpHandle, uint32_t DestinationAddress,
                                         LPVOID RequestData, WORD RequestSize,
                                         IP_OPTION_INFORMATION *RequestOptions,
                                         LPVOID ReplyBuffer, DWORD ReplySize, DWORD Timeout);
typedef BOOL (WINAPI *pfn_IcmpCloseHandle)(HANDLE IcmpHandle);

bool ping_tcp(const char *host, uint16_t port, uint32_t timeout_ms) {
    if (!host || host[0] == '\0') return false;
    if (port == 0) port = 22;
    if (timeout_ms == 0) timeout_ms = 4000;

    term_printf("Testing TCP connection to %s:%u (timeout %lu ms)...\r\n", host, port, (unsigned long)timeout_ms);
    DWORD t0 = GetTickCount();
    SOCKET s = winsock_ce_connect(host, port, (int)timeout_ms);
    DWORD t1 = GetTickCount();

    if (s != INVALID_SOCKET) {
        winsock_ce_close(s);
        term_printf("Connected to %s:%u in %lu ms! (Host reachable)\r\n\r\n", host, port, (unsigned long)(t1 - t0));
        return true;
    } else {
        term_printf("Error: Connection to %s:%u failed or timed out.\r\n\r\n", host, port);
        return false;
    }
}

bool ping_run(const char *host, int count) {
    if (!host || host[0] == '\0') {
        term_puts("Usage: ping <host> [count]\r\n");
        return false;
    }
    if (count <= 0) count = 4;
    if (count > 20) count = 20;

    char ip_str[64] = {0};
    uint32_t ip = winsock_ce_resolve(host, ip_str, sizeof(ip_str));
    if (ip == INADDR_NONE) {
        term_printf("Ping request could not find host '%s'. Please verify the name or IP address.\r\n\r\n", host);
        return false;
    }

    term_printf("\r\nPinging %s [%s] with 32 bytes of data:\r\n", host, ip_str);

    /* Dynamically load icmp.dll (or iphlpapi.dll) */
    HMODULE hIcmp = LoadLibraryW(L"icmp.dll");
    if (!hIcmp) hIcmp = LoadLibraryW(L"iphlpapi.dll");

    pfn_IcmpCreateFile  fn_IcmpCreateFile  = NULL;
    pfn_IcmpSendEcho    fn_IcmpSendEcho    = NULL;
    pfn_IcmpCloseHandle fn_IcmpCloseHandle = NULL;

    if (hIcmp) {
        fn_IcmpCreateFile  = (pfn_IcmpCreateFile)GetProcAddressW(hIcmp, L"IcmpCreateFile");
        fn_IcmpSendEcho    = (pfn_IcmpSendEcho)GetProcAddressW(hIcmp, L"IcmpSendEcho");
        fn_IcmpCloseHandle = (pfn_IcmpCloseHandle)GetProcAddressW(hIcmp, L"IcmpCloseHandle");
    }

    if (!hIcmp || !fn_IcmpCreateFile || !fn_IcmpSendEcho || !fn_IcmpCloseHandle) {
        if (hIcmp) FreeLibrary(hIcmp);
        term_puts("(Notice: icmp.dll not present on this Windows CE build; falling back to TCP reachability)\r\n");
        return ping_tcp(host, 80, 3000);
    }

    HANDLE hFile = fn_IcmpCreateFile();
    if (hFile == INVALID_HANDLE_VALUE) {
        FreeLibrary(hIcmp);
        term_puts("Error: Could not initialize ICMP handle. Falling back to TCP probe.\r\n");
        return ping_tcp(host, 80, 3000);
    }

    char send_data[32] = "abcdefghijklmnopqrstuvwabcdefghi";
    uint8_t reply_buf[1024];

    int sent = 0;
    int received = 0;
    DWORD min_rtt = 0xFFFFFFFF;
    DWORD max_rtt = 0;
    DWORD sum_rtt = 0;

    for (int i = 0; i < count; i++) {
        memset(reply_buf, 0, sizeof(reply_buf));
        sent++;

        DWORD replies = fn_IcmpSendEcho(hFile, ip, send_data, sizeof(send_data),
                                        NULL, reply_buf, sizeof(reply_buf), 2000);
        if (replies > 0) {
            ICMP_ECHO_REPLY *rep = (ICMP_ECHO_REPLY *)reply_buf;
            if (rep->Status == 0) {
                received++;
                DWORD rtt = rep->RoundTripTime;
                if (rtt < min_rtt) min_rtt = rtt;
                if (rtt > max_rtt) max_rtt = rtt;
                sum_rtt += rtt;
                term_printf("Reply from %s: bytes=%u time=%lums TTL=%u\r\n",
                            ip_str, (unsigned)rep->DataSize, (unsigned long)rtt, (unsigned)rep->Options.Ttl);
            } else {
                term_printf("Reply from %s: Destination host unreachable (status=%lu)\r\n", ip_str, (unsigned long)rep->Status);
            }
        } else {
            term_puts("Request timed out.\r\n");
        }

        if (i < count - 1) {
            Sleep(500);
        }
    }

    fn_IcmpCloseHandle(hFile);
    FreeLibrary(hIcmp);

    int lost = sent - received;
    int loss_pct = (sent > 0) ? (lost * 100 / sent) : 0;

    term_printf("\r\nPing statistics for %s:\r\n", ip_str);
    term_printf("    Packets: Sent = %d, Received = %d, Lost = %d (%d%% loss),\r\n",
                sent, received, lost, loss_pct);

    if (received > 0) {
        DWORD avg_rtt = sum_rtt / received;
        term_puts("Approximate round trip times in milli-seconds:\r\n");
        term_printf("    Minimum = %lums, Maximum = %lums, Average = %lums\r\n\r\n",
                    (unsigned long)min_rtt, (unsigned long)max_rtt, (unsigned long)avg_rtt);
    } else {
        term_puts("\r\n");
    }

    return (received > 0);
}
