#ifndef CESSH_SSH2_H
#define CESSH_SSH2_H

#include <stdint.h>
#include <stddef.h>

#ifdef PALMOS
#include <PalmOS.h>
#ifndef bool
#define bool Boolean
#endif
#else
#include <stdbool.h>
#endif

typedef enum {
    SSH2_STATUS_DISCONNECTED = 0,
    SSH2_STATUS_CONNECTING,
    SSH2_STATUS_AUTHENTICATING,
    SSH2_STATUS_CONNECTED,
    SSH2_STATUS_ERROR
} ssh2_status_t;

typedef void (*ssh2_output_fn)(const char *data, size_t len);

void          ssh2_init(ssh2_output_fn output_cb);
bool          ssh2_connect(const char *host, uint16_t port, const char *user, const char *pass);
void          ssh2_disconnect(void);
bool          ssh2_is_connected(void);
ssh2_status_t ssh2_get_status(void);

/* Process incoming network data and events (call in main loop/timer) */
bool          ssh2_poll(void);

/* Send keystrokes / data to remote SSH shell */
bool          ssh2_send_data(const void *data, size_t len);

/* Window size update */
void          ssh2_set_terminal_size(int cols, int rows);

#endif /* CESSH_SSH2_H */
