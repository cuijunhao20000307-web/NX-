#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROXY_MODE_DIRECT = 0,
    PROXY_MODE_HTTP = 1,
    PROXY_MODE_SOCKS5 = 2,
} ProxyMode;

typedef struct {
    ProxyMode mode;
    char host[256];
    int port;
    char username[128];
    char password[128];
} ProxyConfig;

void proxy_config_defaults(ProxyConfig* cfg);
bool proxy_config_load(ProxyConfig* cfg);
bool proxy_config_save(const ProxyConfig* cfg);
const char* proxy_mode_name(ProxyMode mode);
int proxy_connect_target(const ProxyConfig* cfg,
                         const char* target_host,
                         int target_port,
                         int timeout_ms,
                         char* status,
                         size_t status_size);
bool proxy_test(const ProxyConfig* cfg, char* status, size_t status_size);

#ifdef __cplusplus
}
#endif
