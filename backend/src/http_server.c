#include "http_server.h"
#include "api_handlers.h"
#include <stdio.h>

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        /* fn_data from mg_http_listen is stored in c->fn_data */
        http_server_t *srv = (http_server_t *)c->fn_data;
        api_handle_request(c, hm, srv->store, srv->config);
    }
}

int http_server_start(http_server_t *srv, const config_t *cfg, snapshot_store_t *store,
                      volatile sig_atomic_t *shutdown_flag) {
    memset(srv, 0, sizeof(*srv));
    srv->config = cfg;
    srv->store = store;

    mg_mgr_init(&srv->mgr);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d", cfg->listen_host, cfg->listen_port);
    mg_http_listen(&srv->mgr, url, ev_handler, srv);
    printf("HTTP server listening on %s\n", url);

    srv->running = true;

    while (srv->running && !*shutdown_flag) {
        mg_mgr_poll(&srv->mgr, 1000);
    }

    mg_mgr_free(&srv->mgr);
    return 0;
}

void http_server_stop(http_server_t *srv) {
    srv->running = false;
}
