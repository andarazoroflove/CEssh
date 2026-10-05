#ifndef CESSH_PALMOS_NET_H
#define CESSH_PALMOS_NET_H

#include <PalmOS.h>
#include <NetMgr.h>

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize NetLib network library */
Boolean palmos_net_init(void);

/* Clean up and close NetLib */
void palmos_net_cleanup(void);
void palmos_net_close_all(void);

/* Check if network is available */
Boolean palmos_net_is_available(void);

/* Resolve hostname to 32-bit IPv4 address */
NetIPAddr palmos_resolve(const char *host);

/* Open TCP connection to host:port with timeout */
NetSocketRef palmos_net_connect(const char *host, UInt16 port, UInt32 timeout_ms);

/* Send data to socket */
Int32 palmos_net_send(NetSocketRef sock, const void *buf, Int32 len);

/* Receive data from socket with timeout */
Int32 palmos_net_recv(NetSocketRef sock, void *buf, Int32 max_len, UInt32 timeout_ms);

/* Check if socket has pending data to read (non-blocking) */
Boolean palmos_net_has_data(NetSocketRef sock);

/* Close socket */
void palmos_net_close(NetSocketRef sock);

/* Milliseconds tick counter */
UInt32 palmos_get_tick_ms(void);

/* Resolve host IP address as string */
Boolean palmos_net_get_local_info(char *ip_buf, UInt16 ip_buf_len);

#ifdef __cplusplus
}
#endif

#endif /* CESSH_PALMOS_NET_H */
