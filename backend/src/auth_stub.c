#include "auth_stub.h"
#include <string.h>

void auth_stub_get_context(request_context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->authenticated = false;
    ctx->permissions = PERM_READ_METRICS;
}
