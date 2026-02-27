#include <ttak/mem/detachable.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif

#ifdef __linux__
    #include <sys/mman.h>
#endif

#ifndef _WIN32
#ifndef NSIG
#ifdef _NSIG
#define NSIG _NSIG
#else
/* 64 real-time signals + 1; covers Linux and most BSD defaults */
#define NSIG 65
#endif
#endif
#endif

#ifndef _WIN32
typedef struct {
    atomic_bool triggered;
    _Bool graceful;
    int exit_code;
} ttak_signal_guard_t;
#endif

static pthread_once_t g_default_ctx_once = PTHREAD_ONCE_INIT;
static ttak_detachable_context_t g_default_ctx;

static pthread_mutex_t g_ctx_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static ttak_detachable_context_t **g_ctx_registry = NULL;
static size_t g_ctx_registry_len = 0;
static size_t g_ctx_registry_cap = 0;

#ifndef _WIN32
static ttak_signal_guard_t g_signal_guard = {
    .triggered = false,
    .graceful = true,
    .exit_code = -1
};
#endif

static void ttak_detachable_register_context(ttak_detachable_context_t *ctx);
static void ttak_detachable_unregister_context(ttak_detachable_context_t *ctx);
static void ttak_detachable_context_once(void);
static void ttak_detachable_row_flush(ttak_detachable_context_t *ctx, ttak_detachable_generation_row_t *row);
static void ttak_detachable_track_pointer(ttak_detachable_context_t *ctx, void *ptr);
static bool ttak_detachable_untrack_pointer(ttak_detachable_context_t *ctx, void *ptr);
static bool ttak_detachable_cache_store(ttak_detachable_context_t *ctx, ttak_detachable_cache_t *cache, void *ptr, size_t size);
static void *ttak_detachable_cache_take(ttak_detachable_cache_t *cache, size_t requested);
static void ttak_detachable_cache_drain(ttak_detachable_context_t *ctx, ttak_detachable_cache_t *cache, bool release_storage);
static void ttak_detachable_global_shutdown(_Bool flush_rows);
#ifndef _WIN32
static void ttak_detachable_signal_handler(int signo);
static int ttak_hard_kill_configure(sigset_t signals, int *ret, _Bool graceful);
#endif

static inline _Bool ttak_detachable_need_lock(const ttak_detachable_context_t *ctx) {
    return (ctx->flags & TTAK_ARENA_USE_LOCKED_ACCESS) && !(ctx->flags & TTAK_ARENA_IS_SINGLE_THREAD);
}

static inline void ttak_detachable_wrlock(ttak_detachable_context_t *ctx) {
    if (ttak_detachable_need_lock(ctx)) {
        pthread_rwlock_wrlock(&ctx->arena_lock);
    }
}

static inline void ttak_detachable_unlock(ttak_detachable_context_t *ctx) {
    if (ttak_detachable_need_lock(ctx)) {
        pthread_rwlock_unlock(&ctx->arena_lock);
    }
}

static void ttak_detachable_context_once(void) {
    ttak_detachable_context_init(&g_default_ctx, TTAK_ARENA_HAS_EPOCH_RECLAMATION | TTAK_ARENA_HAS_DEFAULT_EPOCH_GC);
}

ttak_detachable_context_t *ttak_detachable_context_default(void) {
    pthread_once(&g_default_ctx_once, ttak_detachable_context_once);
    return &g_default_ctx;
}

void ttak_detachable_context_init(ttak_detachable_context_t *ctx, uint32_t flags) {
    if (!ctx) return;

    memset(ctx, 0, sizeof(*ctx));
    ctx->flags = flags;
    ctx->matrix_rows = TTAK_DETACHABLE_MATRIX_ROWS;
    ctx->active_row = 0;
    ctx->epoch_delay = (flags & TTAK_ARENA_HAS_DEFAULT_EPOCH_GC) ? 2 : 1;
    ttak_detach_status_reset(&ctx->base_status);
    atomic_init(&ctx->global_epoch_hint, 0);

    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
#ifdef PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
    pthread_rwlock_init(&ctx->arena_lock, &attr);
    pthread_rwlockattr_destroy(&attr);

    ttak_detachable_cache_init(&ctx->small_cache, TTAK_DETACHABLE_CACHE_MAX_BYTES, TTAK_DETACHABLE_CACHE_SLOTS);

    for (size_t i = 0; i < ctx->matrix_rows; ++i) {
        ctx->rows[i].columns = NULL;
        ctx->rows[i].len = 0;
        ctx->rows[i].cap = 0;
    }

    ttak_detachable_register_context(ctx);
}

void ttak_detachable_context_destroy(ttak_detachable_context_t *ctx) {
    if (!ctx) return;

    ttak_detachable_unregister_context(ctx);

    for (size_t i = 0; i < ctx->matrix_rows; ++i) {
        ttak_detachable_row_flush(ctx, &ctx->rows[i]);
        free(ctx->rows[i].columns);
        ctx->rows[i].columns = NULL;
        ctx->rows[i].cap = 0;
        ctx->rows[i].len = 0;
    }

    ttak_detachable_cache_destroy(ctx, &ctx->small_cache);
    pthread_rwlock_destroy(&ctx->arena_lock);
}

void ttak_detachable_cache_init(ttak_detachable_cache_t *cache, size_t chunk_size, size_t capacity) {
    if (!cache) return;

    memset(cache, 0, sizeof(*cache));
    cache->chunk_size = (chunk_size == 0 || chunk_size > TTAK_DETACHABLE_CACHE_MAX_BYTES)
                            ? TTAK_DETACHABLE_CACHE_MAX_BYTES
                            : chunk_size;
    cache->capacity = (capacity == 0) ? TTAK_DETACHABLE_CACHE_SLOTS : capacity;
    cache->slots = calloc(cache->capacity, sizeof(void *));
    pthread_mutex_init(&cache->lock, NULL);
}

void ttak_detachable_cache_destroy(ttak_detachable_context_t *ctx, ttak_detachable_cache_t *cache) {
    if (!cache) return;
    ttak_detachable_cache_drain(ctx, cache, true);
    pthread_mutex_destroy(&cache->lock);
}

ttak_detachable_allocation_t ttak_detachable_mem_alloc(ttak_detachable_context_t *ctx, size_t size, uint64_t epoch_hint) {
    ttak_detachable_allocation_t result = {0};
    if (!ctx) ctx = ttak_detachable_context_default();

    size_t actual_size = size == 0 ? 1 : size;
    ttak_detach_status_t status = ctx->base_status;
    ttak_detach_status_converge(&status);

    void *data = NULL;
    bool from_cache = false;

    if (actual_size <= ctx->small_cache.chunk_size) {
        data = ttak_detachable_cache_take(&ctx->small_cache, actual_size);
        if (data) {
            from_cache = true;
        }
    }

    if (!data) {
        if (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION) {
            ttak_epoch_enter();
        }

        data = calloc(1, actual_size);

        if (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION) {
            atomic_store_explicit(&ctx->global_epoch_hint, epoch_hint, memory_order_relaxed);
            ttak_epoch_exit();
        }

        if (!data) {
            return result;
        }

#ifdef __linux__
        if (ctx->flags & TTAK_ARENA_USE_ASYNC_OPT) {
            madvise(data, actual_size, MADV_DONTFORK);
        }
#endif
    }

    status.bits |= TTAK_DETACHABLE_ATTACH;
    if (from_cache) status.bits |= TTAK_DETACHABLE_PARTIAL_CACHE;
    ttak_detach_status_mark_known(&status);

    ttak_detachable_wrlock(ctx);
    ttak_detachable_track_pointer(ctx, data);
    ttak_detachable_unlock(ctx);

    result.data = data;
    result.size = actual_size;
    result.detach_status = status;
    result.cache = &ctx->small_cache;
    return result;
}

void ttak_detachable_mem_free(ttak_detachable_context_t *ctx, ttak_detachable_allocation_t *alloc) {
    if (!alloc || !alloc->data) return;
    if (!ctx) ctx = ttak_detachable_context_default();

    bool stored = false;
    if (alloc->size <= ctx->small_cache.chunk_size &&
        !(alloc->detach_status.bits & TTAK_DETACHABLE_DETACH_NOCHECK)) {
        stored = ttak_detachable_cache_store(ctx, &ctx->small_cache, alloc->data, alloc->size);
        if (stored) {
            alloc->detach_status.bits |= TTAK_DETACHABLE_PARTIAL_CACHE;
        }
    }

    ttak_detachable_wrlock(ctx);
    bool was_tracked = ttak_detachable_untrack_pointer(ctx, alloc->data);
    ttak_detachable_unlock(ctx);
    bool skip_retire = (!was_tracked) && (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION);

    if (!stored && !skip_retire) {
        if (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION) {
            ttak_epoch_enter();
            ttak_epoch_retire(alloc->data, free);
            ttak_epoch_exit();
        } else {
            if ((alloc->detach_status.bits & TTAK_DETACHABLE_DETACH_NOCHECK) ||
                (ctx->base_status.bits & TTAK_DETACHABLE_DETACH_NOCHECK)) {
                ttak_epoch_retire(alloc->data, free);
            } else {
                free(alloc->data);
            }
        }
    }

    alloc->data = NULL;
    alloc->size = 0;
    alloc->cache = NULL;
    ttak_detach_status_reset(&alloc->detach_status);
}

#ifndef _WIN32
int ttak_hard_kill_graceful_exit(sigset_t signals, int *ret) {
    return ttak_hard_kill_configure(signals, ret, true);
}

int ttak_hard_kill_exit(sigset_t signals, int *ret) {
    return ttak_hard_kill_configure(signals, ret, false);
}
#endif

static void ttak_detachable_register_context(ttak_detachable_context_t *ctx) {
    pthread_mutex_lock(&g_ctx_registry_lock);
    if (g_ctx_registry_len == g_ctx_registry_cap) {
        size_t new_cap = g_ctx_registry_cap ? g_ctx_registry_cap * 2 : 8;
        ttak_detachable_context_t **tmp = realloc(g_ctx_registry, new_cap * sizeof(*tmp));
        if (!tmp) {
            pthread_mutex_unlock(&g_ctx_registry_lock);
            return;
        }
        g_ctx_registry = tmp;
        g_ctx_registry_cap = new_cap;
    }
    g_ctx_registry[g_ctx_registry_len++] = ctx;
    pthread_mutex_unlock(&g_ctx_registry_lock);
}

static void ttak_detachable_unregister_context(ttak_detachable_context_t *ctx) {
    pthread_mutex_lock(&g_ctx_registry_lock);
    for (size_t i = 0; i < g_ctx_registry_len; ++i) {
        if (g_ctx_registry[i] == ctx) {
            g_ctx_registry[i] = g_ctx_registry[g_ctx_registry_len - 1];
            g_ctx_registry_len--;
            break;
        }
    }
    pthread_mutex_unlock(&g_ctx_registry_lock);
}

static void ttak_detachable_row_alloc(ttak_detachable_generation_row_t *row) {
    if (!row->columns) {
        row->cap = TTAK_DETACHABLE_GENERATIONS;
        row->columns = calloc(row->cap, sizeof(void *));
        row->len = 0;
    }
}

static void ttak_detachable_row_flush(ttak_detachable_context_t *ctx, ttak_detachable_generation_row_t *row) {
    if (!row->columns) return;
    for (size_t i = 0; i < row->len; ++i) {
        void *ptr = row->columns[i];
        if (!ptr) continue;
        if (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION) {
            ttak_epoch_retire(ptr, free);
        } else {
            free(ptr);
        }
        row->columns[i] = NULL;
    }
    row->len = 0;
}

static void ttak_detachable_track_pointer(ttak_detachable_context_t *ctx, void *ptr) {
    if (!ptr) return;
    ttak_detachable_generation_row_t *row = &ctx->rows[ctx->active_row];
    ttak_detachable_row_alloc(row);

    if (row->len >= row->cap) {
        ttak_detachable_row_flush(ctx, row);
        size_t advance = ctx->epoch_delay ? ctx->epoch_delay : 1;
        ctx->active_row = (ctx->active_row + advance) % ctx->matrix_rows;
        row = &ctx->rows[ctx->active_row];
        ttak_detachable_row_alloc(row);
    }

    row->columns[row->len++] = ptr;
}

static bool ttak_detachable_untrack_pointer(ttak_detachable_context_t *ctx, void *ptr) {
    if (!ptr) return false;
    for (size_t r = 0; r < ctx->matrix_rows; ++r) {
        ttak_detachable_generation_row_t *row = &ctx->rows[r];
        if (!row->columns) continue;
        for (size_t c = 0; c < row->len; ++c) {
            if (row->columns[c] == ptr) {
                row->columns[c] = NULL;
                return true;
            }
        }
    }
    return false;
}

static bool ttak_detachable_cache_store(ttak_detachable_context_t *ctx, ttak_detachable_cache_t *cache, void *ptr, size_t size) {
    if (!cache || !ptr) return false;
    pthread_mutex_lock(&cache->lock);
    if (!cache->slots || size > cache->chunk_size) {
        pthread_mutex_unlock(&cache->lock);
        return false;
    }

    if (cache->count == cache->capacity) {
        if (ctx->flags & TTAK_ARENA_IS_URGENT_TASK) {
            void *victim = cache->slots[cache->head];
            if (victim) {
                if (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION) {
                    ttak_epoch_retire(victim, free);
                } else {
                    free(victim);
                }
            }
            cache->head = (cache->head + 1) % cache->capacity;
            cache->count--;
        } else {
            cache->misses++;
            pthread_mutex_unlock(&cache->lock);
            return false;
        }
    } else {
        cache->hits++;
    }

    cache->slots[cache->tail] = ptr;
    cache->tail = (cache->tail + 1) % cache->capacity;
    cache->count++;
    pthread_mutex_unlock(&cache->lock);
    return true;
}

static void *ttak_detachable_cache_take(ttak_detachable_cache_t *cache, size_t requested) {
    if (!cache) return NULL;
    pthread_mutex_lock(&cache->lock);
    if (!cache->slots || cache->count == 0 || requested > cache->chunk_size) {
        cache->misses++;
        pthread_mutex_unlock(&cache->lock);
        return NULL;
    }

    void *ptr = cache->slots[cache->head];
    cache->slots[cache->head] = NULL;
    cache->head = (cache->head + 1) % cache->capacity;
    cache->count--;
    cache->hits++;
    pthread_mutex_unlock(&cache->lock);

    if (ptr) {
        memset(ptr, 0, cache->chunk_size);
    }
    return ptr;
}

static void ttak_detachable_cache_drain(ttak_detachable_context_t *ctx, ttak_detachable_cache_t *cache, bool release_storage) {
    if (!cache) return;
    pthread_mutex_lock(&cache->lock);
    void **slots = cache->slots;
    size_t cap = cache->capacity;
    if (slots) {
        for (size_t i = 0; i < cap; ++i) {
            void *ptr = slots[i];
            if (!ptr) continue;
            if (ctx && (ctx->flags & TTAK_ARENA_HAS_EPOCH_RECLAMATION)) {
                ttak_epoch_retire(ptr, free);
            } else {
                free(ptr);
            }
            slots[i] = NULL;
        }
    }
    cache->head = cache->tail = cache->count = 0;
    if (release_storage) {
        cache->slots = NULL;
        cache->capacity = 0;
    }
    pthread_mutex_unlock(&cache->lock);

    if (release_storage) {
        free(slots);
    }
}

static void ttak_detachable_global_shutdown(_Bool flush_rows) {
    pthread_mutex_lock(&g_ctx_registry_lock);
    for (size_t i = 0; i < g_ctx_registry_len; ++i) {
        ttak_detachable_context_t *ctx = g_ctx_registry[i];
        if (!ctx) continue;
        ttak_detachable_cache_drain(ctx, &ctx->small_cache, false);
        if (flush_rows) {
            for (size_t r = 0; r < ctx->matrix_rows; ++r) {
                ttak_detachable_row_flush(ctx, &ctx->rows[r]);
            }
        }
    }
    pthread_mutex_unlock(&g_ctx_registry_lock);
}

#ifndef _WIN32
static void ttak_detachable_signal_handler(int signo) {
    bool expected = false;
    if (!atomic_compare_exchange_weak(&g_signal_guard.triggered, &expected, true)) {
        _exit(g_signal_guard.exit_code >= 0 ? g_signal_guard.exit_code : signo);
    }

    if (g_signal_guard.graceful) {
        ttak_detachable_global_shutdown(true);
    } else {
        ttak_detachable_global_shutdown(false);
    }

    int code = (g_signal_guard.exit_code >= 0) ? g_signal_guard.exit_code : signo;
    _exit(code);
}

static int ttak_hard_kill_configure(sigset_t signals, int *ret, _Bool graceful) {
    g_signal_guard.graceful = graceful;
    g_signal_guard.exit_code = ret ? *ret : -1;
    atomic_store(&g_signal_guard.triggered, false);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ttak_detachable_signal_handler;
    sigemptyset(&sa.sa_mask);
#ifdef SA_RESTART
    sa.sa_flags = SA_RESTART;
#endif

    for (int sig = 1; sig < NSIG; ++sig) {
        if (sigismember(&signals, sig) == 1) {
            if (sigaction(sig, &sa, NULL) != 0) {
                return -1;
            }
        }
    }

    return 0;
}
#endif
