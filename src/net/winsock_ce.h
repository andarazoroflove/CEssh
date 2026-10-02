#ifndef CESSH_WINSOCK_CE_H
#define CESSH_WINSOCK_CE_H

#include <windows.h>
#include <winsock2.h>
#include <stdbool.h>
#include <stdint.h>

bool   winsock_ce_init(void);
void   winsock_ce_cleanup(void);
bool   winsock_ce_is_available(void);

SOCKET winsock_ce_connect(const char *host, uint16_t port, int timeout_ms);
int    winsock_ce_send(SOCKET s, const void *buf, int len);
int    winsock_ce_recv(SOCKET s, void *buf, int len, int timeout_ms);
bool   winsock_ce_has_data(SOCKET s);
void   winsock_ce_close(SOCKET s);
bool   winsock_ce_get_local_info(char *name, size_t name_len, char *ip_str, size_t ip_len);

#endif /* CESSH_WINSOCK_CE_H */
