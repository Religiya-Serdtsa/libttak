#ifndef TTAK_UNSAFE_CONTEXT_H
#define TTAK_UNSAFE_CONTEXT_H

#include <ttak/mem/owner.h>
#include <ttak/sync/sync.h>
#include <stddef.h>
#include <stdbool.h>

#define __TTAK_CTX_USE_FIRST__  0
#define __TTAK_CTX_USE_SECOND__ 1

typedef int ttak_context_inherit_t;

typedef void (*ttak_context_callback_t)(void *shared_mem, size_t shared_size, void *arg);

typedef struct ttak_context {
    ttak_owner_t *first;
    ttak_owner_t *second;
    void         *shared_mem;
    size_t        shared_size;
    ttak_mutex_t  bridge_lock;
    ttak_context_inherit_t ownership_side;
    ttak_context_inherit_t last_request;
    bool initialized;
} ttak_context_t;

bool ttak_context_init(ttak_context_t *ctx,
                       ttak_owner_t *first,
                       ttak_owner_t *second,
                       void *shared_mem,
                       size_t shared_size,
                       ttak_context_inherit_t inherit_side);

void ttak_context_destroy(ttak_context_t *ctx);

bool ttak_context_run(ttak_context_t *ctx,
                      ttak_context_inherit_t side,
                      ttak_context_callback_t cb,
                      void *arg);

bool ttak_context_reassign(ttak_context_t *ctx, ttak_context_inherit_t side);

ttak_owner_t *ttak_context_owner(const ttak_context_t *ctx, ttak_context_inherit_t side);
ttak_context_inherit_t ttak_context_active(const ttak_context_t *ctx);
void *ttak_context_shared(const ttak_context_t *ctx, size_t *size_out);

#endif // TTAK_UNSAFE_CONTEXT_H
