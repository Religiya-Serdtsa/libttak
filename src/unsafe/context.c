#include <ttak/unsafe/context.h>
#include <string.h>

static ttak_owner_t *ttak_context_pick_owner(const ttak_context_t *ctx, ttak_context_inherit_t side) {
    if (!ctx) return NULL;
    return (side == __TTAK_CTX_USE_SECOND__) ? ctx->second : ctx->first;
}

bool ttak_context_init(ttak_context_t *ctx,
                       ttak_owner_t *first,
                       ttak_owner_t *second,
                       void *shared_mem,
                       size_t shared_size,
                       ttak_context_inherit_t inherit_side) {
    if (!ctx || !first || !second) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->first = first;
    ctx->second = second;
    ctx->shared_mem = shared_mem;
    ctx->shared_size = shared_size;
    ctx->ownership_side = (inherit_side == __TTAK_CTX_USE_SECOND__) ? __TTAK_CTX_USE_SECOND__ : __TTAK_CTX_USE_FIRST__;
    ctx->last_request = ctx->ownership_side;
    if (ttak_mutex_init(&ctx->bridge_lock) != 0) return false;
    ctx->initialized = true;
    return true;
}

void ttak_context_destroy(ttak_context_t *ctx) {
    if (!ctx) return;
    if (ctx->initialized) {
        ttak_mutex_destroy(&ctx->bridge_lock);
    }
    memset(ctx, 0, sizeof(*ctx));
}

bool ttak_context_run(ttak_context_t *ctx,
                      ttak_context_inherit_t side,
                      ttak_context_callback_t cb,
                      void *arg) {
    if (!ctx || !cb || !ctx->initialized) return false;
    if (ttak_mutex_lock(&ctx->bridge_lock) != 0) return false;
    ctx->last_request = (side == __TTAK_CTX_USE_SECOND__) ? __TTAK_CTX_USE_SECOND__ : __TTAK_CTX_USE_FIRST__;
    ttak_owner_t *owner = ttak_context_pick_owner(ctx, ctx->ownership_side);
    if (owner) {
        ttak_rwlock_wrlock(&owner->lock);
    }
    cb(ctx->shared_mem, ctx->shared_size, arg);
    if (owner) {
        ttak_rwlock_unlock(&owner->lock);
    }
    ttak_mutex_unlock(&ctx->bridge_lock);
    return true;
}

ttak_owner_t *ttak_context_owner(const ttak_context_t *ctx, ttak_context_inherit_t side) {
    return ttak_context_pick_owner(ctx, side);
}

bool ttak_context_reassign(ttak_context_t *ctx, ttak_context_inherit_t side) {
    if (!ctx || !ctx->initialized) return false;
    if (ttak_mutex_lock(&ctx->bridge_lock) != 0) return false;
    ctx->ownership_side = (side == __TTAK_CTX_USE_SECOND__) ? __TTAK_CTX_USE_SECOND__ : __TTAK_CTX_USE_FIRST__;
    ttak_mutex_unlock(&ctx->bridge_lock);
    return true;
}

ttak_context_inherit_t ttak_context_active(const ttak_context_t *ctx) {
    if (!ctx) return __TTAK_CTX_USE_FIRST__;
    return ctx->ownership_side;
}

void *ttak_context_shared(const ttak_context_t *ctx, size_t *size_out) {
    if (!ctx) return NULL;
    if (size_out) *size_out = ctx->shared_size;
    return ctx->shared_mem;
}
