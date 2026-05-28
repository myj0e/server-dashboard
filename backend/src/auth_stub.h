#ifndef AUTH_STUB_H
#define AUTH_STUB_H

#include "platform.h"

#define PERM_READ_METRICS   (1u << 0)
#define PERM_MANAGE_PROCESS (1u << 1)
#define PERM_MANAGE_CONFIG  (1u << 2)
#define PERM_READ_AUDIT     (1u << 3)

typedef struct {
    bool authenticated;
    char user_id[128];
    uint32_t permissions;
} request_context_t;

void auth_stub_get_context(request_context_t *ctx);

#endif
