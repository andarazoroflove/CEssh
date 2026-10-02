#ifndef CESSH_PING_H
#define CESSH_PING_H

#include <stdbool.h>
#include <stdint.h>

/* Standard ICMP Ping (sends 4 echo requests, measures RTT and packet loss) */
bool ping_run(const char *host, int count);

/* TCP Port Probe */
bool ping_tcp(const char *host, uint16_t port, uint32_t timeout_ms);

#endif /* CESSH_PING_H */
