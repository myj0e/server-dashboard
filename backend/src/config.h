#ifndef CONFIG_H
#define CONFIG_H

#include "platform.h"

typedef struct {
    char listen_host[64];
    int listen_port;
    int sample_interval_ms;
    bool enable_gpu;
    bool enable_process_ops;
    bool enable_auth;
    char config_path[512];
} config_t;

int config_load(config_t *cfg, int argc, char **argv);

#endif
