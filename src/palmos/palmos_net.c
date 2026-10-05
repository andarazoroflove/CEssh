#include "palmos_net.h"

static UInt16  s_net_lib_ref = sysInvalidRefNum;
static Boolean s_net_inited = false;

Boolean palmos_net_init(void) {
    Err err;
    UInt16 if_err;

    if (s_net_inited) return true;

    err = SysLibFind("Net.lib", &s_net_lib_ref);
    if (err != errNone) {
        return false;
    }

    if_err = 0;
    err = NetLibOpen(s_net_lib_ref, &if_err);
    if (err != errNone && err != netErrAlreadyOpen) {
        s_net_lib_ref = sysInvalidRefNum;
        return false;
    }

    s_net_inited = true;
    return true;
}

void palmos_net_cleanup(void) {
    if (s_net_inited && s_net_lib_ref != sysInvalidRefNum) {
        NetLibClose(s_net_lib_ref, false);
        s_net_lib_ref = sysInvalidRefNum;
        s_net_inited = false;
    }
}

Boolean palmos_net_is_available(void) {
    return (s_net_inited && s_net_lib_ref != sysInvalidRefNum);
}

NetSocketRef palmos_net_connect(const char *host, UInt16 port, UInt32 timeout_ms) {
    Int32 timeout_ticks;
    Err err;
    NetIPAddr ip;
    NetSocketRef sock;
    NetSocketAddrINType addr_in;
    Int16 res;

    if (!palmos_net_init()) return -1;

    timeout_ticks = (Int32)(((unsigned long long)timeout_ms * SysTicksPerSecond()) / 1000);
    if (timeout_ticks < 1) timeout_ticks = 1;

    err = errNone;
    ip = NetLibAddrAToIN(s_net_lib_ref, host);
    if (ip == (NetIPAddr)-1 || ip == 0) {
        NetHostInfoBufType host_buf;
        NetHostInfoType *host_info = NetLibGetHostByName(s_net_lib_ref, host, &host_buf, timeout_ticks, &err);
        if (!host_info || !host_info->addrListP || !host_info->addrListP[0]) {
            return -1;
        }
        ip = *(NetIPAddr *)(host_info->addrListP[0]);
    }

    sock = NetLibSocketOpen(s_net_lib_ref, netSocketAddrINET, netSocketTypeStream, 0, timeout_ticks, &err);
    if (sock < 0) return -1;

    MemSet(&addr_in, sizeof(addr_in), 0);
    addr_in.family = netSocketAddrINET;
    addr_in.port = NetHToNS(port);
    addr_in.addr = ip;

    res = NetLibSocketConnect(s_net_lib_ref, sock, (NetSocketAddrType *)&addr_in, sizeof(addr_in), timeout_ticks, &err);
    if (res < 0) {
        NetLibSocketClose(s_net_lib_ref, sock, -1, &err);
        return -1;
    }

    return sock;
}

Int32 palmos_net_send(NetSocketRef sock, const void *buf, Int32 len) {
    Err err;
    Int32 sent;

    if (sock < 0 || !s_net_inited) return -1;
    err = errNone;
    sent = NetLibSend(s_net_lib_ref, sock, (void *)buf, (UInt16)len, 0, NULL, 0, SysTicksPerSecond() * 5, &err);
    if (sent < 0) {
        if (err == netErrWouldBlock || err == netErrTimeout) return 0;
        return -1;
    }
    return sent;
}

Int32 palmos_net_recv(NetSocketRef sock, void *buf, Int32 max_len, UInt32 timeout_ms) {
    Int32 timeout_ticks;
    Err err;
    Int32 r;

    if (sock < 0 || !s_net_inited) return -1;
    timeout_ticks = (Int32)(((unsigned long long)timeout_ms * SysTicksPerSecond()) / 1000);
    err = errNone;
    r = NetLibReceive(s_net_lib_ref, sock, buf, (UInt16)max_len, 0, NULL, 0, timeout_ticks, &err);
    if (r < 0) {
        if (err == netErrWouldBlock || err == netErrTimeout) return 0;
        return -1;
    }
    return r;
}

Boolean palmos_net_has_data(NetSocketRef sock) {
    NetFDSetType read_fds;
    Err err;
    Int16 n;

    if (sock < 0 || !s_net_inited) return false;
    netFDZero(&read_fds);
    netFDSet(sock, &read_fds);
    err = errNone;
    n = NetLibSelect(s_net_lib_ref, sock + 1, &read_fds, NULL, NULL, 0, &err);
    return (n > 0 && netFDIsSet(sock, &read_fds));
}

void palmos_net_close(NetSocketRef sock) {
    if (sock >= 0 && s_net_inited) {
        Err err = errNone;
        NetLibSocketClose(s_net_lib_ref, sock, -1, &err);
    }
}

UInt32 palmos_get_tick_ms(void) {
    UInt32 ticks = TimGetTicks();
    UInt32 tps = SysTicksPerSecond();
    if (tps == 0) tps = 100;
    return (UInt32)(((unsigned long long)ticks * 1000) / tps);
}

Boolean palmos_net_get_local_info(char *ip_buf, UInt16 ip_buf_len) {
    UInt32 if_creator;
    UInt16 if_instance;
    Err err;
    NetIPAddr ip;
    UInt16 setting_size;

    if (!palmos_net_init() || !ip_buf || ip_buf_len < 16) return false;

    if_creator = 0;
    if_instance = 0;
    err = NetLibIFGet(s_net_lib_ref, 0, &if_creator, &if_instance);
    if (err == errNone) {
        ip = 0;
        setting_size = sizeof(ip);
        err = NetLibIFSettingGet(s_net_lib_ref, if_creator, if_instance, netIFSettingActualIPAddr, &ip, &setting_size);
        if (err == errNone && ip != 0) {
            NetLibAddrINToA(s_net_lib_ref, ip, ip_buf);
            return true;
        }
    }
    return false;
}

NetIPAddr palmos_resolve(const char *host) {
    NetIPAddr ip;
    Err err = errNone;
    NetHostInfoBufType host_buf;
    NetHostInfoType *host_info;

    if (!palmos_net_init() || !host) return 0;

    ip = NetLibAddrAToIN(s_net_lib_ref, host);
    if (ip != (NetIPAddr)-1 && ip != 0) {
        return ip;
    }

    host_info = NetLibGetHostByName(s_net_lib_ref, host, &host_buf, SysTicksPerSecond() * 5, &err);
    if (!host_info || !host_info->addrListP || !host_info->addrListP[0]) {
        return 0;
    }
    return *(NetIPAddr *)(host_info->addrListP[0]);
}

void palmos_net_close_all(void) {
    palmos_net_cleanup();
}
