#include "proxy.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define CFG_DIR  "sdmc:/switch/NXAnime"
#define CFG_PATH "sdmc:/switch/NXAnime/proxy.cfg"

static void set_status(char* out, size_t n, const char* s) {
    if (!out || n == 0) return;
    snprintf(out, n, "%s", s ? s : "");
}

static int send_all(int fd, const void* data, size_t size) {
    const unsigned char* p = (const unsigned char*)data;
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(fd, p + sent, size - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void* data, size_t size) {
    unsigned char* p = (unsigned char*)data;
    size_t got = 0;
    while (got < size) {
        ssize_t n = recv(fd, p + got, size - got, 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int connect_one_addr(const struct addrinfo* ai, int timeout_ms) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (rc < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (rc < 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        rc = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (rc <= 0) { close(fd); return -1; }
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) { close(fd); return -1; }
    }
    fcntl(fd, F_SETFL, flags);
    struct timeval io = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
    return fd;
}

static int tcp_connect(const char* host, int port, int timeout_ms) {
    char port_buf[16];
    snprintf(port_buf, sizeof(port_buf), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_buf, &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = connect_one_addr(ai, timeout_ms);
        if (fd >= 0) break;
    }
    freeaddrinfo(res);
    return fd;
}

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void base64_encode(const unsigned char* in, size_t len, char* out, size_t out_size) {
    size_t oi = 0;
    for (size_t i = 0; i < len && oi + 5 < out_size; i += 3) {
        unsigned int v = (unsigned int)in[i] << 16;
        int remain = (int)(len - i);
        if (remain > 1) v |= (unsigned int)in[i + 1] << 8;
        if (remain > 2) v |= in[i + 2];
        out[oi++] = b64_table[(v >> 18) & 63];
        out[oi++] = b64_table[(v >> 12) & 63];
        out[oi++] = remain > 1 ? b64_table[(v >> 6) & 63] : '=';
        out[oi++] = remain > 2 ? b64_table[v & 63] : '=';
    }
    if (out_size) out[oi < out_size ? oi : out_size - 1] = 0;
}

static int http_connect_tunnel(int fd, const ProxyConfig* cfg, const char* host, int port,
                               char* status, size_t status_size) {
    char auth_line[512] = {0};
    if (cfg->username[0]) {
        char raw[260], enc[384];
        snprintf(raw, sizeof(raw), "%s:%s", cfg->username, cfg->password);
        base64_encode((const unsigned char*)raw, strlen(raw), enc, sizeof(enc));
        snprintf(auth_line, sizeof(auth_line), "Proxy-Authorization: Basic %s\r\n", enc);
    }
    char req[1200];
    snprintf(req, sizeof(req),
             "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\nProxy-Connection: Keep-Alive\r\n%s\r\n",
             host, port, host, port, auth_line);
    if (send_all(fd, req, strlen(req)) < 0) { set_status(status, status_size, "HTTP PROXY SEND FAILED"); return -1; }
    char resp[1024];
    ssize_t n = recv(fd, resp, sizeof(resp) - 1, 0);
    if (n <= 0) { set_status(status, status_size, "HTTP PROXY NO RESPONSE"); return -1; }
    resp[n] = 0;
    if (!strstr(resp, " 200 ") && strncmp(resp, "HTTP/1.1 200", 12) != 0 && strncmp(resp, "HTTP/1.0 200", 12) != 0) {
        set_status(status, status_size, "HTTP PROXY CONNECT REJECTED"); return -1;
    }
    set_status(status, status_size, "HTTP TUNNEL OK");
    return 0;
}

static int socks5_auth(int fd, const ProxyConfig* cfg, char* status, size_t status_size) {
    unsigned char hello[4]; size_t n = 0;
    hello[n++] = 0x05;
    if (cfg->username[0]) { hello[n++] = 0x02; hello[n++] = 0x00; hello[n++] = 0x02; }
    else { hello[n++] = 0x01; hello[n++] = 0x00; }
    if (send_all(fd, hello, n) < 0) return -1;
    unsigned char reply[2];
    if (recv_all(fd, reply, 2) < 0 || reply[0] != 0x05 || reply[1] == 0xFF) { set_status(status, status_size, "SOCKS5 NEGOTIATION FAILED"); return -1; }
    if (reply[1] == 0x02) {
        size_t ul = strlen(cfg->username), pl = strlen(cfg->password);
        if (ul > 255 || pl > 255) return -1;
        unsigned char auth[520]; size_t p = 0;
        auth[p++] = 0x01; auth[p++] = (unsigned char)ul; memcpy(auth + p, cfg->username, ul); p += ul;
        auth[p++] = (unsigned char)pl; memcpy(auth + p, cfg->password, pl); p += pl;
        if (send_all(fd, auth, p) < 0) return -1;
        unsigned char ar[2];
        if (recv_all(fd, ar, 2) < 0 || ar[1] != 0x00) { set_status(status, status_size, "SOCKS5 AUTH FAILED"); return -1; }
    }
    return 0;
}

static int socks5_connect_tunnel(int fd, const ProxyConfig* cfg, const char* host, int port,
                                 char* status, size_t status_size) {
    if (socks5_auth(fd, cfg, status, status_size) < 0) return -1;
    size_t hl = strlen(host); if (hl == 0 || hl > 255) return -1;
    unsigned char req[300]; size_t p = 0;
    req[p++] = 0x05; req[p++] = 0x01; req[p++] = 0x00; req[p++] = 0x03; req[p++] = (unsigned char)hl;
    memcpy(req + p, host, hl); p += hl;
    req[p++] = (unsigned char)((port >> 8) & 0xFF); req[p++] = (unsigned char)(port & 0xFF);
    if (send_all(fd, req, p) < 0) return -1;
    unsigned char head[4];
    if (recv_all(fd, head, 4) < 0 || head[0] != 0x05 || head[1] != 0x00) { set_status(status, status_size, "SOCKS5 CONNECT REJECTED"); return -1; }
    size_t tail = 0;
    if (head[3] == 0x01) tail = 6;
    else if (head[3] == 0x04) tail = 18;
    else if (head[3] == 0x03) { unsigned char dl = 0; if (recv_all(fd, &dl, 1) < 0) return -1; tail = (size_t)dl + 2; }
    else return -1;
    unsigned char discard[260];
    if (tail > sizeof(discard) || recv_all(fd, discard, tail) < 0) return -1;
    set_status(status, status_size, "SOCKS5 TUNNEL OK");
    return 0;
}

void proxy_config_defaults(ProxyConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = PROXY_MODE_DIRECT;
    cfg->port = 1080;
}

const char* proxy_mode_name(ProxyMode mode) {
    switch (mode) {
        case PROXY_MODE_HTTP: return "HTTP CONNECT";
        case PROXY_MODE_SOCKS5: return "SOCKS5";
        default: return "DIRECT";
    }
}

bool proxy_config_load(ProxyConfig* cfg) {
    if (!cfg) return false;
    proxy_config_defaults(cfg);
    FILE* f = fopen(CFG_PATH, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq++ = 0; eq[strcspn(eq, "\r\n")] = 0;
        if (!strcmp(line, "mode")) cfg->mode = (ProxyMode)atoi(eq);
        else if (!strcmp(line, "host")) snprintf(cfg->host, sizeof(cfg->host), "%s", eq);
        else if (!strcmp(line, "port")) cfg->port = atoi(eq);
        else if (!strcmp(line, "username")) snprintf(cfg->username, sizeof(cfg->username), "%s", eq);
        else if (!strcmp(line, "password")) snprintf(cfg->password, sizeof(cfg->password), "%s", eq);
    }
    fclose(f);
    if (cfg->mode < PROXY_MODE_DIRECT || cfg->mode > PROXY_MODE_SOCKS5) cfg->mode = PROXY_MODE_DIRECT;
    if (cfg->port <= 0 || cfg->port > 65535) cfg->port = 1080;
    return true;
}

bool proxy_config_save(const ProxyConfig* cfg) {
    if (!cfg) return false;
    mkdir(CFG_DIR, 0777);
    FILE* f = fopen(CFG_PATH, "w"); if (!f) return false;
    fprintf(f, "mode=%d\nhost=%s\nport=%d\nusername=%s\npassword=%s\n",
            (int)cfg->mode, cfg->host, cfg->port, cfg->username, cfg->password);
    fclose(f); return true;
}

int proxy_connect_target(const ProxyConfig* cfg, const char* target_host, int target_port,
                         int timeout_ms, char* status, size_t status_size) {
    if (!cfg || !target_host || target_port <= 0) return -1;
    const char* connect_host = target_host; int connect_port = target_port;
    if (cfg->mode != PROXY_MODE_DIRECT) {
        if (!cfg->host[0] || cfg->port <= 0) { set_status(status, status_size, "PROXY HOST OR PORT MISSING"); return -1; }
        connect_host = cfg->host; connect_port = cfg->port;
    }
    int fd = tcp_connect(connect_host, connect_port, timeout_ms);
    if (fd < 0) { set_status(status, status_size, cfg->mode == PROXY_MODE_DIRECT ? "DIRECT CONNECT FAILED" : "PROXY SERVER UNREACHABLE"); return -1; }
    if (cfg->mode == PROXY_MODE_HTTP && http_connect_tunnel(fd, cfg, target_host, target_port, status, status_size) < 0) { close(fd); return -1; }
    if (cfg->mode == PROXY_MODE_SOCKS5 && socks5_connect_tunnel(fd, cfg, target_host, target_port, status, status_size) < 0) { close(fd); return -1; }
    if (cfg->mode == PROXY_MODE_DIRECT) set_status(status, status_size, "DIRECT TCP OK");
    return fd;
}

bool proxy_test(const ProxyConfig* cfg, char* status, size_t status_size) {
    int fd = proxy_connect_target(cfg, "ani.girigirilove.com", 443, 5000, status, status_size);
    if (fd < 0) return false;
    close(fd);
    return true;
}
