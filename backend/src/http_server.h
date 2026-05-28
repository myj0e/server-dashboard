#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "platform.h"
#include "mongoose.h"
#include "snapshot.h"
#include "config.h"

typedef struct {
    struct mg_mgr mgr;
    const config_t *config;
    snapshot_store_t *store;
    volatile bool running;
} http_server_t;

int  http_server_start(http_server_t *srv, const config_t *cfg, snapshot_store_t *store,
                       volatile sig_atomic_t *shutdown_flag);
void http_server_stop(http_server_t *srv);

#endif
