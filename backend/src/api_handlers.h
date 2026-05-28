#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include "mongoose.h"
#include "snapshot.h"
#include "config.h"
#include "auth_stub.h"

void api_handle_request(struct mg_connection *c, struct mg_http_message *hm,
                        snapshot_store_t *store, const config_t *cfg);

#endif
