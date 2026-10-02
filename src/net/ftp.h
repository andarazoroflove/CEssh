#ifndef CESSH_FTP_H
#define CESSH_FTP_H

#include <stdbool.h>
#include <stdint.h>

void        ftp_init(void);
const char *ftp_get_app_dir(void);

bool        ftp_connect(const char *host, uint16_t port, const char *user, const char *pass);
void        ftp_disconnect(void);
bool        ftp_is_connected(void);

bool        ftp_cmd_pwd(void);
bool        ftp_cmd_cd(const char *path);
bool        ftp_cmd_ls(const char *path);
bool        ftp_cmd_get(const char *remote_file, const char *local_file);
bool        ftp_cmd_put(const char *local_file, const char *remote_file);
bool        ftp_cmd_binary(void);
bool        ftp_cmd_ascii(void);
bool        ftp_cmd_raw(const char *cmd_line);

#endif /* CESSH_FTP_H */
