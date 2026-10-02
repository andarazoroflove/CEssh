#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssh2.h"
#include "ssh_buf.h"
#include "ssh_crypto.h"
#include "../net/winsock_ce.h"

/* SSH-2 Protocol Constants */
#define SSH_MSG_DISCONNECT                1
#define SSH_MSG_IGNORE                    2
#define SSH_MSG_UNIMPLEMENTED             3
#define SSH_MSG_DEBUG                     4
#define SSH_MSG_SERVICE_REQUEST           5
#define SSH_MSG_SERVICE_ACCEPT            6
#define SSH_MSG_KEXINIT                   20
#define SSH_MSG_NEWKEYS                   21
#define SSH_MSG_KEX_ECDH_INIT             30
#define SSH_MSG_KEX_ECDH_REPLY            31
#define SSH_MSG_USERAUTH_REQUEST          50
#define SSH_MSG_USERAUTH_FAILURE          51
#define SSH_MSG_USERAUTH_SUCCESS          52
#define SSH_MSG_USERAUTH_BANNER           53
#define SSH_MSG_GLOBAL_REQUEST            80
#define SSH_MSG_REQUEST_SUCCESS           81
#define SSH_MSG_REQUEST_FAILURE           82
#define SSH_MSG_CHANNEL_OPEN              90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_OPEN_FAILURE      92
#define SSH_MSG_CHANNEL_WINDOW_ADJUST     93
#define SSH_MSG_CHANNEL_DATA              94
#define SSH_MSG_CHANNEL_EXTENDED_DATA     95
#define SSH_MSG_CHANNEL_EOF               96
#define SSH_MSG_CHANNEL_CLOSE             97
#define SSH_MSG_CHANNEL_REQUEST           98
#define SSH_MSG_CHANNEL_SUCCESS           99
#define SSH_MSG_CHANNEL_FAILURE           100

#define SSH_CLIENT_BANNER "SSH-2.0-CEssh_1.0.0_Jornada720\r\n"

static SOCKET           s_sock = INVALID_SOCKET;
static ssh2_status_t    s_status = SSH2_STATUS_DISCONNECTED;
static ssh2_output_fn   s_output_cb = NULL;

static uint32_t         s_send_seq = 0;
static uint32_t         s_recv_seq = 0;
static bool             s_encrypted = false;

static ssh_cipher_ctx_t s_cipher_send;
static ssh_cipher_ctx_t s_cipher_recv;
static ssh_mac_ctx_t    s_mac_send;
static ssh_mac_ctx_t    s_mac_recv;

static uint32_t         s_server_chan = 0;
static uint32_t         s_client_chan = 0;
static int              s_term_cols = 80;
static int              s_term_rows = 24;

static void notify_out(const char *msg) {
    if (s_output_cb && msg) {
        s_output_cb(msg, strlen(msg));
    }
}

void ssh2_init(ssh2_output_fn output_cb) {
    s_output_cb = output_cb;
    s_sock = INVALID_SOCKET;
    s_status = SSH2_STATUS_DISCONNECTED;
    s_encrypted = false;
    s_send_seq = 0;
    s_recv_seq = 0;
}

ssh2_status_t ssh2_get_status(void) {
    return s_status;
}

bool ssh2_is_connected(void) {
    return (s_status == SSH2_STATUS_CONNECTED && s_sock != INVALID_SOCKET);
}

void ssh2_disconnect(void) {
    if (s_sock != INVALID_SOCKET) {
        if (s_status == SSH2_STATUS_CONNECTED) {
            /* Try to send SSH_MSG_DISCONNECT */
            uint8_t disc[32];
            ssh_buf_t db;
            ssh_buf_init(&db, disc, sizeof(disc));
            ssh_put_u8(&db, SSH_MSG_DISCONNECT);
            ssh_put_u32(&db, 11); /* SSH_DISCONNECT_BY_APPLICATION */
            ssh_put_str(&db, "User closed session");
            ssh_put_str(&db, "");
            /* Best-effort unencrypted/encrypted send */
        }
        winsock_ce_close(s_sock);
        s_sock = INVALID_SOCKET;
    }
    s_status = SSH2_STATUS_DISCONNECTED;
    s_encrypted = false;
    s_send_seq = 0;
    s_recv_seq = 0;
}

static int ssh_send_packet(const uint8_t *payload, size_t payload_len) {
    if (s_sock == INVALID_SOCKET) return -1;

    static uint8_t raw_buf[4096];
    size_t block_size = s_encrypted ? 16 : 8;

    size_t rem = (5 + payload_len) % block_size;
    size_t pad_len = (rem == 0) ? block_size : (block_size - rem);
    if (pad_len < 4) {
        pad_len += block_size;
    }

    uint32_t packet_len = (uint32_t)(1 + payload_len + pad_len);
    size_t total_unencrypted = 4 + packet_len;
    if (total_unencrypted + 32 > sizeof(raw_buf)) {
        return -1;
    }

    raw_buf[0] = (uint8_t)(packet_len >> 24);
    raw_buf[1] = (uint8_t)(packet_len >> 16);
    raw_buf[2] = (uint8_t)(packet_len >> 8);
    raw_buf[3] = (uint8_t)packet_len;
    raw_buf[4] = (uint8_t)pad_len;

    memcpy(raw_buf + 5, payload, payload_len);
    ssh_random_bytes(raw_buf + 5 + payload_len, pad_len);

    uint8_t mac_tag[32];
    if (s_encrypted) {
        ssh_mac_compute(&s_mac_send, s_send_seq, raw_buf, total_unencrypted, mac_tag);
        ssh_cipher_crypt(&s_cipher_send, raw_buf, total_unencrypted);
    }

    s_send_seq++;

    int ret = winsock_ce_send(s_sock, raw_buf, (int)total_unencrypted);
    if (ret <= 0) return ret;

    if (s_encrypted) {
        ret = winsock_ce_send(s_sock, mac_tag, 32);
        if (ret <= 0) return ret;
    }

    return 1;
}

static int recv_all(uint8_t *dst, size_t needed, uint32_t timeout_ms) {
    size_t received = 0;
    DWORD start_time = GetTickCount();

    while (received < needed) {
        int r = winsock_ce_recv(s_sock, dst + received, (int)(needed - received), 100);
        if (r > 0) {
            received += r;
        } else if (r < 0) {
            return -1;
        } else {
            DWORD now = GetTickCount();
            if (now - start_time >= timeout_ms) {
                return (received > 0) ? -1 : 0;
            }
        }
    }
    return 1;
}

static int ssh_recv_packet(uint8_t *out_payload, size_t max_payload, size_t *out_payload_len, uint32_t timeout_ms) {
    if (s_sock == INVALID_SOCKET) return -1;

    static uint8_t raw_buf[4096];

    /* 1. Read packet length (first 4 bytes) */
    uint8_t hdr[4];
    int r = recv_all(hdr, 4, timeout_ms);
    if (r <= 0) return r;

    if (s_encrypted) {
        ssh_cipher_crypt(&s_cipher_recv, hdr, 4);
    }

    uint32_t packet_len = ((uint32_t)hdr[0] << 24) |
                          ((uint32_t)hdr[1] << 16) |
                          ((uint32_t)hdr[2] << 8) |
                          ((uint32_t)hdr[3]);

    if (packet_len < 2 || packet_len > 35000) {
        return -1;
    }

    /* 2. Read remaining payload + padding + MAC */
    size_t to_read = packet_len + (s_encrypted ? 32 : 0);
    if (to_read > sizeof(raw_buf)) {
        return -1;
    }

    r = recv_all(raw_buf, to_read, timeout_ms);
    if (r <= 0) return -1;

    uint8_t *mac_tag = raw_buf + packet_len;

    if (s_encrypted) {
        ssh_cipher_crypt(&s_cipher_recv, raw_buf, packet_len);

        uint8_t check_buf[4096];
        memcpy(check_buf, hdr, 4);
        memcpy(check_buf + 4, raw_buf, packet_len);

        uint8_t expected_mac[32];
        ssh_mac_compute(&s_mac_recv, s_recv_seq, check_buf, 4 + packet_len, expected_mac);

        if (memcmp(expected_mac, mac_tag, 32) != 0) {
            return -1; /* MAC failure */
        }
    }

    s_recv_seq++;

    uint8_t pad_len = raw_buf[0];
    if (1 + pad_len > packet_len) {
        return -1;
    }

    size_t plen = packet_len - 1 - pad_len;
    if (plen > max_payload) plen = max_payload;

    memcpy(out_payload, raw_buf + 1, plen);
    if (out_payload_len) *out_payload_len = plen;
    return 1;
}

bool ssh2_connect(const char *host, uint16_t port, const char *user, const char *pass) {
    if (!host || host[0] == '\0') {
        notify_out("Error: Hostname cannot be empty.\r\n");
        return false;
    }
    if (port == 0) port = 22;

    ssh2_disconnect();

    char status_buf[128];
    sprintf(status_buf, "Connecting to %s:%u...\r\n", host, port);
    notify_out(status_buf);

    s_status = SSH2_STATUS_CONNECTING;
    s_sock = winsock_ce_connect(host, port, 5000);
    if (s_sock == INVALID_SOCKET) {
        notify_out("Error: TCP connection failed (host unreachable or connection refused).\r\n");
        s_status = SSH2_STATUS_ERROR;
        return false;
    }

    /* 1. Receive Server Banner */
    char srv_banner[256];
    size_t sb_len = 0;
    DWORD start_time = GetTickCount();
    while (sb_len < sizeof(srv_banner) - 1) {
        char ch;
        int r = winsock_ce_recv(s_sock, &ch, 1, 100);
        if (r > 0) {
            srv_banner[sb_len++] = ch;
            if (ch == '\n') break;
        } else if (r < 0) {
            notify_out("Error: Disconnected while receiving banner.\r\n");
            ssh2_disconnect();
            return false;
        } else {
            if (GetTickCount() - start_time > 5000) {
                notify_out("Error: Server banner timed out.\r\n");
                ssh2_disconnect();
                return false;
            }
        }
    }
    srv_banner[sb_len] = '\0';
    while (sb_len > 0 && (srv_banner[sb_len - 1] == '\r' || srv_banner[sb_len - 1] == '\n')) {
        srv_banner[--sb_len] = '\0';
    }

    sprintf(status_buf, "Remote: %s\r\n", srv_banner);
    notify_out(status_buf);

    /* 2. Send Client Banner */
    const char *cli_banner = SSH_CLIENT_BANNER;
    size_t cb_full_len = strlen(cli_banner);
    if (winsock_ce_send(s_sock, cli_banner, (int)cb_full_len) <= 0) {
        notify_out("Error: Failed to send client banner.\r\n");
        ssh2_disconnect();
        return false;
    }

    char clean_cli_banner[64];
    strncpy(clean_cli_banner, cli_banner, sizeof(clean_cli_banner) - 1);
    clean_cli_banner[sizeof(clean_cli_banner) - 1] = '\0';
    size_t cb_len = strlen(clean_cli_banner);
    while (cb_len > 0 && (clean_cli_banner[cb_len - 1] == '\r' || clean_cli_banner[cb_len - 1] == '\n')) {
        clean_cli_banner[--cb_len] = '\0';
    }

    /* 3. Send Client KEXINIT */
    notify_out("Initiating key exchange (Curve25519 ECDH + AES128-CTR)...\r\n");
    uint8_t cli_kexinit[512];
    ssh_buf_t ckb;
    ssh_buf_init(&ckb, cli_kexinit, sizeof(cli_kexinit));
    ssh_put_u8(&ckb, SSH_MSG_KEXINIT);
    uint8_t cookie[16];
    ssh_random_bytes(cookie, sizeof(cookie));
    ssh_put_raw(&ckb, cookie, sizeof(cookie));
    ssh_put_str(&ckb, "curve25519-sha256,curve25519-sha256@libssh.org");
    ssh_put_str(&ckb, "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-256,ssh-rsa");
    ssh_put_str(&ckb, "aes128-ctr");
    ssh_put_str(&ckb, "aes128-ctr");
    ssh_put_str(&ckb, "hmac-sha2-256");
    ssh_put_str(&ckb, "hmac-sha2-256");
    ssh_put_str(&ckb, "none");
    ssh_put_str(&ckb, "none");
    ssh_put_str(&ckb, "");
    ssh_put_str(&ckb, "");
    ssh_put_u8(&ckb, 0);
    ssh_put_u32(&ckb, 0);

    if (ssh_send_packet(ckb.data, ckb.len) <= 0) {
        notify_out("Error: Failed to send KEXINIT.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 4. Receive Server KEXINIT */
    uint8_t srv_kexinit[2048];
    size_t srv_kexinit_len = 0;
    if (ssh_recv_packet(srv_kexinit, sizeof(srv_kexinit), &srv_kexinit_len, 5000) <= 0) {
        notify_out("Error: Failed to receive server KEXINIT.\r\n");
        ssh2_disconnect();
        return false;
    }
    if (srv_kexinit[0] != SSH_MSG_KEXINIT) {
        notify_out("Error: Expected KEXINIT message from server.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 5. Generate Client Ephemeral Key & Send SSH_MSG_KEX_ECDH_INIT (30) */
    uint8_t client_priv[32];
    ssh_random_bytes(client_priv, sizeof(client_priv));
    uint8_t q_c[32];
    ssh_curve25519_mulgen(q_c, client_priv);

    uint8_t ecdh_init[64];
    ssh_buf_t eib;
    ssh_buf_init(&eib, ecdh_init, sizeof(ecdh_init));
    ssh_put_u8(&eib, SSH_MSG_KEX_ECDH_INIT);
    ssh_put_string(&eib, q_c, 32);

    if (ssh_send_packet(eib.data, eib.len) <= 0) {
        notify_out("Error: Failed to send ECDH_INIT.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 6. Receive SSH_MSG_KEX_ECDH_REPLY (31) */
    uint8_t reply[2048];
    size_t reply_len = 0;
    if (ssh_recv_packet(reply, sizeof(reply), &reply_len, 5000) <= 0) {
        notify_out("Error: Failed to receive ECDH_REPLY.\r\n");
        ssh2_disconnect();
        return false;
    }
    if (reply[0] != SSH_MSG_KEX_ECDH_REPLY) {
        notify_out("Error: Expected ECDH_REPLY message.\r\n");
        ssh2_disconnect();
        return false;
    }

    ssh_buf_t rb;
    ssh_buf_init_read(&rb, reply, reply_len);
    uint8_t rmtype;
    ssh_get_u8(&rb, &rmtype);
    const uint8_t *k_s, *q_s, *sig_blob;
    size_t k_s_len, q_s_len, sig_len;
    if (!ssh_get_string(&rb, &k_s, &k_s_len) ||
        !ssh_get_string(&rb, &q_s, &q_s_len) ||
        !ssh_get_string(&rb, &sig_blob, &sig_len) ||
        q_s_len != 32) {
        notify_out("Error: Malformed ECDH_REPLY.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 7. Compute Shared Secret K (Curve25519) */
    uint8_t k_raw[32];
    ssh_curve25519_mul(k_raw, q_s, client_priv);

    uint8_t k_mpint[64];
    ssh_buf_t k_buf;
    ssh_buf_init(&k_buf, k_mpint, sizeof(k_mpint));
    ssh_put_mpint(&k_buf, k_raw, 32);

    /* 8. Compute Exchange Hash H */
    br_sha256_context h_ctx;
    br_sha256_init(&h_ctx);

    uint8_t v_c_hdr[4];
    v_c_hdr[0] = (uint8_t)(cb_len >> 24); v_c_hdr[1] = (uint8_t)(cb_len >> 16);
    v_c_hdr[2] = (uint8_t)(cb_len >> 8);  v_c_hdr[3] = (uint8_t)cb_len;
    br_sha256_update(&h_ctx, v_c_hdr, 4);
    br_sha256_update(&h_ctx, clean_cli_banner, cb_len);

    uint8_t v_s_hdr[4];
    v_s_hdr[0] = (uint8_t)(sb_len >> 24); v_s_hdr[1] = (uint8_t)(sb_len >> 16);
    v_s_hdr[2] = (uint8_t)(sb_len >> 8);  v_s_hdr[3] = (uint8_t)sb_len;
    br_sha256_update(&h_ctx, v_s_hdr, 4);
    br_sha256_update(&h_ctx, srv_banner, sb_len);

    uint8_t i_c_hdr[4];
    i_c_hdr[0] = (uint8_t)(ckb.len >> 24); i_c_hdr[1] = (uint8_t)(ckb.len >> 16);
    i_c_hdr[2] = (uint8_t)(ckb.len >> 8);  i_c_hdr[3] = (uint8_t)ckb.len;
    br_sha256_update(&h_ctx, i_c_hdr, 4);
    br_sha256_update(&h_ctx, ckb.data, ckb.len);

    uint8_t i_s_hdr[4];
    i_s_hdr[0] = (uint8_t)(srv_kexinit_len >> 24); i_s_hdr[1] = (uint8_t)(srv_kexinit_len >> 16);
    i_s_hdr[2] = (uint8_t)(srv_kexinit_len >> 8);  i_s_hdr[3] = (uint8_t)srv_kexinit_len;
    br_sha256_update(&h_ctx, i_s_hdr, 4);
    br_sha256_update(&h_ctx, srv_kexinit, srv_kexinit_len);

    uint8_t k_s_hdr[4];
    k_s_hdr[0] = (uint8_t)(k_s_len >> 24); k_s_hdr[1] = (uint8_t)(k_s_len >> 16);
    k_s_hdr[2] = (uint8_t)(k_s_len >> 8);  k_s_hdr[3] = (uint8_t)k_s_len;
    br_sha256_update(&h_ctx, k_s_hdr, 4);
    br_sha256_update(&h_ctx, k_s, k_s_len);

    uint8_t q_c_hdr[4] = {0, 0, 0, 32};
    br_sha256_update(&h_ctx, q_c_hdr, 4);
    br_sha256_update(&h_ctx, q_c, 32);

    uint8_t q_s_hdr[4] = {0, 0, 0, 32};
    br_sha256_update(&h_ctx, q_s_hdr, 4);
    br_sha256_update(&h_ctx, q_s, 32);

    br_sha256_update(&h_ctx, k_buf.data, k_buf.len);

    uint8_t exchange_h[32];
    br_sha256_out(&h_ctx, exchange_h);

    /* 9. Send SSH_MSG_NEWKEYS (21) */
    uint8_t nk = SSH_MSG_NEWKEYS;
    if (ssh_send_packet(&nk, 1) <= 0) {
        notify_out("Error: Failed to send NEWKEYS.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 10. Receive SSH_MSG_NEWKEYS (21) */
    uint8_t r_nk[64];
    size_t r_nk_len = 0;
    if (ssh_recv_packet(r_nk, sizeof(r_nk), &r_nk_len, 5000) <= 0) {
        notify_out("Error: Failed to receive NEWKEYS.\r\n");
        ssh2_disconnect();
        return false;
    }
    if (r_nk[0] != SSH_MSG_NEWKEYS) {
        notify_out("Error: Expected NEWKEYS from server.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 11. Key Derivation (RFC 4253 §7.2) */
    uint8_t iv_c2s[16], iv_s2c[16];
    uint8_t key_c2s[16], key_s2c[16];
    uint8_t mac_c2s[32], mac_s2c[32];

    ssh_kdf(k_buf.data, k_buf.len, exchange_h, 32, 'A', exchange_h, 32, iv_c2s, 16);
    ssh_kdf(k_buf.data, k_buf.len, exchange_h, 32, 'B', exchange_h, 32, iv_s2c, 16);
    ssh_kdf(k_buf.data, k_buf.len, exchange_h, 32, 'C', exchange_h, 32, key_c2s, 16);
    ssh_kdf(k_buf.data, k_buf.len, exchange_h, 32, 'D', exchange_h, 32, key_s2c, 16);
    ssh_kdf(k_buf.data, k_buf.len, exchange_h, 32, 'E', exchange_h, 32, mac_c2s, 32);
    ssh_kdf(k_buf.data, k_buf.len, exchange_h, 32, 'F', exchange_h, 32, mac_s2c, 32);

    ssh_cipher_init(&s_cipher_send, key_c2s, iv_c2s);
    ssh_cipher_init(&s_cipher_recv, key_s2c, iv_s2c);
    ssh_mac_init(&s_mac_send, mac_c2s, 32);
    ssh_mac_init(&s_mac_recv, mac_s2c, 32);
    s_encrypted = true;

    /* 12. Request Service: ssh-userauth */
    notify_out("Requesting authentication service...\r\n");
    uint8_t sreq[64];
    ssh_buf_t srb;
    ssh_buf_init(&srb, sreq, sizeof(sreq));
    ssh_put_u8(&srb, SSH_MSG_SERVICE_REQUEST);
    ssh_put_str(&srb, "ssh-userauth");
    if (ssh_send_packet(srb.data, srb.len) <= 0) {
        notify_out("Error: Failed to send SERVICE_REQUEST.\r\n");
        ssh2_disconnect();
        return false;
    }

    uint8_t saccept[256];
    size_t saccept_len = 0;
    if (ssh_recv_packet(saccept, sizeof(saccept), &saccept_len, 5000) <= 0) {
        notify_out("Error: Failed to receive SERVICE_ACCEPT.\r\n");
        ssh2_disconnect();
        return false;
    }
    if (saccept[0] != SSH_MSG_SERVICE_ACCEPT) {
        notify_out("Error: Service request rejected by server.\r\n");
        ssh2_disconnect();
        return false;
    }

    /* 13. Send Password Authentication Request */
    s_status = SSH2_STATUS_AUTHENTICATING;
    sprintf(status_buf, "Authenticating user '%s'...\r\n", user ? user : "root");
    notify_out(status_buf);

    uint8_t ureq[512];
    ssh_buf_t ub;
    ssh_buf_init(&ub, ureq, sizeof(ureq));
    ssh_put_u8(&ub, SSH_MSG_USERAUTH_REQUEST);
    ssh_put_str(&ub, user ? user : "root");
    ssh_put_str(&ub, "ssh-connection");
    ssh_put_str(&ub, "password");
    ssh_put_u8(&ub, 0); /* FALSE */
    ssh_put_str(&ub, pass ? pass : "");

    if (ssh_send_packet(ub.data, ub.len) <= 0) {
        notify_out("Error: Failed to send userauth request.\r\n");
        ssh2_disconnect();
        return false;
    }

    uint8_t urep[256];
    size_t urep_len = 0;
    if (ssh_recv_packet(urep, sizeof(urep), &urep_len, 8000) <= 0) {
        notify_out("Error: Authentication timed out.\r\n");
        ssh2_disconnect();
        return false;
    }

    if (urep[0] != SSH_MSG_USERAUTH_SUCCESS) {
        notify_out("Error: Access denied (authentication failed).\r\n");
        ssh2_disconnect();
        return false;
    }
    notify_out("Authentication succeeded.\r\n");

    /* 14. Open Session Channel */
    notify_out("Opening terminal session channel...\r\n");
    uint8_t copen[128];
    ssh_buf_t ob;
    ssh_buf_init(&ob, copen, sizeof(copen));
    ssh_put_u8(&ob, SSH_MSG_CHANNEL_OPEN);
    ssh_put_str(&ob, "session");
    ssh_put_u32(&ob, 0);     /* sender channel = 0 */
    ssh_put_u32(&ob, 65536); /* initial window */
    ssh_put_u32(&ob, 16384); /* max packet */

    if (ssh_send_packet(ob.data, ob.len) <= 0) {
        notify_out("Error: Failed to send channel open request.\r\n");
        ssh2_disconnect();
        return false;
    }

    uint8_t cconf[256];
    size_t cconf_len = 0;
    if (ssh_recv_packet(cconf, sizeof(cconf), &cconf_len, 5000) <= 0) {
        notify_out("Error: Failed to receive channel open confirmation.\r\n");
        ssh2_disconnect();
        return false;
    }
    if (cconf[0] != SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
        notify_out("Error: Session channel rejected by server.\r\n");
        ssh2_disconnect();
        return false;
    }

    ssh_buf_t cb;
    ssh_buf_init_read(&cb, cconf, cconf_len);
    uint8_t cmsg;
    ssh_get_u8(&cb, &cmsg);
    uint32_t my_chan, server_chan, s_win, s_max;
    ssh_get_u32(&cb, &my_chan);
    ssh_get_u32(&cb, &server_chan);
    ssh_get_u32(&cb, &s_win);
    ssh_get_u32(&cb, &s_max);

    s_client_chan = my_chan;
    s_server_chan = server_chan;

    /* 15. Request PTY (vt100, 80x24) */
    uint8_t pty_req[128];
    ssh_buf_init(&ob, pty_req, sizeof(pty_req));
    ssh_put_u8(&ob, SSH_MSG_CHANNEL_REQUEST);
    ssh_put_u32(&ob, s_server_chan);
    ssh_put_str(&ob, "pty-req");
    ssh_put_u8(&ob, 1); /* want_reply */
    ssh_put_str(&ob, "vt100");
    ssh_put_u32(&ob, s_term_cols);
    ssh_put_u32(&ob, s_term_rows);
    ssh_put_u32(&ob, 640);
    ssh_put_u32(&ob, 240);
    ssh_put_str(&ob, "");
    ssh_send_packet(ob.data, ob.len);

    uint8_t r_pty[128];
    size_t r_pty_len = 0;
    ssh_recv_packet(r_pty, sizeof(r_pty), &r_pty_len, 3000);

    /* 16. Request Shell */
    uint8_t shell_req[64];
    ssh_buf_init(&ob, shell_req, sizeof(shell_req));
    ssh_put_u8(&ob, SSH_MSG_CHANNEL_REQUEST);
    ssh_put_u32(&ob, s_server_chan);
    ssh_put_str(&ob, "shell");
    ssh_put_u8(&ob, 1);
    ssh_send_packet(ob.data, ob.len);

    uint8_t r_sh[128];
    size_t r_sh_len = 0;
    ssh_recv_packet(r_sh, sizeof(r_sh), &r_sh_len, 3000);

    s_status = SSH2_STATUS_CONNECTED;
    notify_out("\r\n[SSH-2 Session Established. Press Ctrl+] to disconnect]\r\n\r\n");
    return true;
}

bool ssh2_poll(void) {
    if (s_status != SSH2_STATUS_CONNECTED || s_sock == INVALID_SOCKET) {
        return false;
    }

    if (!winsock_ce_has_data(s_sock)) {
        return false;
    }

    uint8_t in_buf[4096];
    size_t in_len = 0;
    int pr = ssh_recv_packet(in_buf, sizeof(in_buf), &in_len, 20);
    if (pr <= 0 || in_len == 0) {
        if (pr < 0) {
            notify_out("\r\n[Connection lost or host disconnected]\r\n");
            ssh2_disconnect();
            return true;
        }
        return false;
    }

    ssh_buf_t in_b;
    ssh_buf_init_read(&in_b, in_buf, in_len);
    uint8_t itype;
    ssh_get_u8(&in_b, &itype);

    if (itype == SSH_MSG_CHANNEL_DATA || itype == SSH_MSG_CHANNEL_EXTENDED_DATA) {
        uint32_t chan;
        ssh_get_u32(&in_b, &chan);
        if (itype == SSH_MSG_CHANNEL_EXTENDED_DATA) {
            uint32_t data_type_code;
            ssh_get_u32(&in_b, &data_type_code);
        }
        const uint8_t *text;
        size_t text_len;
        if (ssh_get_string(&in_b, &text, &text_len) && text_len > 0) {
            if (s_output_cb) {
                s_output_cb((const char *)text, text_len);
            }

            /* Replenish channel window */
            uint8_t wadj[16];
            ssh_buf_t wb;
            ssh_buf_init(&wb, wadj, sizeof(wb));
            ssh_put_u8(&wb, SSH_MSG_CHANNEL_WINDOW_ADJUST);
            ssh_put_u32(&wb, s_server_chan);
            ssh_put_u32(&wb, (uint32_t)text_len);
            ssh_send_packet(wb.data, wb.len);
            return true;
        }
    } else if (itype == SSH_MSG_CHANNEL_CLOSE || itype == SSH_MSG_DISCONNECT) {
        notify_out("\r\n[Remote host closed session]\r\n");
        ssh2_disconnect();
        return true;
    } else if (itype == SSH_MSG_GLOBAL_REQUEST) {
        const uint8_t *req_name;
        size_t req_len;
        uint8_t want_reply;
        if (ssh_get_string(&in_b, &req_name, &req_len) && ssh_get_u8(&in_b, &want_reply)) {
            if (want_reply) {
                uint8_t fail = SSH_MSG_REQUEST_FAILURE;
                ssh_send_packet(&fail, 1);
            }
        }
    } else if (itype == SSH_MSG_CHANNEL_REQUEST) {
        uint32_t chan;
        const uint8_t *req_name;
        size_t req_len;
        uint8_t want_reply;
        if (ssh_get_u32(&in_b, &chan) && ssh_get_string(&in_b, &req_name, &req_len) && ssh_get_u8(&in_b, &want_reply)) {
            if (want_reply) {
                uint8_t succ[8];
                ssh_buf_t sb;
                ssh_buf_init(&sb, succ, sizeof(succ));
                ssh_put_u8(&sb, SSH_MSG_CHANNEL_SUCCESS);
                ssh_put_u32(&sb, s_server_chan);
                ssh_send_packet(sb.data, sb.len);
            }
        }
    }

    return false;
}

bool ssh2_send_data(const void *data, size_t len) {
    if (s_status != SSH2_STATUS_CONNECTED || s_sock == INVALID_SOCKET) {
        return false;
    }

    uint8_t pkt[2048];
    ssh_buf_t db;
    ssh_buf_init(&db, pkt, sizeof(pkt));
    ssh_put_u8(&db, SSH_MSG_CHANNEL_DATA);
    ssh_put_u32(&db, s_server_chan);
    ssh_put_string(&db, data, len);

    return (ssh_send_packet(db.data, db.len) > 0);
}

void ssh2_set_terminal_size(int cols, int rows) {
    s_term_cols = cols;
    s_term_rows = rows;
    if (s_status == SSH2_STATUS_CONNECTED && s_sock != INVALID_SOCKET) {
        /* Send window-change request */
        uint8_t wchg[64];
        ssh_buf_t wb;
        ssh_buf_init(&wb, wchg, sizeof(wchg));
        ssh_put_u8(&wb, SSH_MSG_CHANNEL_REQUEST);
        ssh_put_u32(&wb, s_server_chan);
        ssh_put_str(&wb, "window-change");
        ssh_put_u8(&wb, 0); /* no reply */
        ssh_put_u32(&wb, (uint32_t)cols);
        ssh_put_u32(&wb, (uint32_t)rows);
        ssh_put_u32(&wb, (uint32_t)(cols * 8));
        ssh_put_u32(&wb, (uint32_t)(rows * 10));
        ssh_send_packet(wb.data, wb.len);
    }
}
