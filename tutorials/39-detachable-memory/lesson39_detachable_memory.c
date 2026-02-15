#include <ttak/mem/detachable.h>
#include <ttak/mem/epoch.h>
#include <pthread.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define WORKER_COUNT 3
#define ITERATIONS_PER_WORKER 64

static atomic_ulong g_cache_reuse = 0;
static atomic_ulong g_fresh_allocs = 0;

typedef struct worker_args {
    size_t id;
    ttak_detachable_context_t *ctx;
} worker_args_t;

static void describe_allocation(size_t worker_id, size_t iter, const ttak_detachable_allocation_t *alloc) {
    const char *source = (alloc->detach_status.bits & TTAK_DETACHABLE_PARTIAL_CACHE)
                             ? "cache"
                             : "arena";
    printf("worker[%zu] iter=%zu -> %zu bytes via %s\n", worker_id, iter, alloc->size, source);
}

static void *worker_main(void *arg) {
    worker_args_t *args = (worker_args_t *)arg;
    ttak_detachable_context_t *ctx = args->ctx;

    for (size_t iter = 0; iter < ITERATIONS_PER_WORKER; ++iter) {
        size_t payload = (iter % 4 == 0) ? 24 : 8; /* mix cached vs. arena-tracked blocks */
        ttak_detachable_allocation_t alloc = ttak_detachable_mem_alloc(ctx, payload, ((uint64_t)args->id << 32) | iter);
        if (!alloc.data) {
            fprintf(stderr, "worker[%zu] failed to allocate %zu bytes\n", args->id, payload);
            break;
        }

        describe_allocation(args->id, iter, &alloc);

        if (alloc.detach_status.bits & TTAK_DETACHABLE_PARTIAL_CACHE) {
            atomic_fetch_add_explicit(&g_cache_reuse, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&g_fresh_allocs, 1, memory_order_relaxed);
        }

        if ((iter % 6) == 0) {
            alloc.detach_status.bits |= TTAK_DETACHABLE_DETACH_NOCHECK;
        }

        memset(alloc.data, (int)(args->id + iter), alloc.size);
        usleep(1000);
        ttak_detachable_mem_free(ctx, &alloc);
    }

    return NULL;
}

static void run_signal_demo(void) {
    sigset_t watch_list;
    sigemptyset(&watch_list);
    sigaddset(&watch_list, SIGUSR1);

    int exit_code = 12;
    pid_t child = fork();
    if (child == 0) {
        if (ttak_hard_kill_graceful_exit(watch_list, &exit_code) != 0) {
            perror("ttak_hard_kill_graceful_exit");
            _exit(1);
        }

        ttak_detachable_context_t *ctx = ttak_detachable_context_default();
        ttak_detachable_allocation_t leak = ttak_detachable_mem_alloc(ctx, 8, 0);
        if (leak.data) {
            memset(leak.data, 0xAB, leak.size);
        }

        /* Trigger the watched signal so the handler Flushes and exits */
        raise(SIGUSR1);
        _exit(0);
    } else if (child > 0) {
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status)) {
            printf("[signal-demo] child exited via handler with code %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[signal-demo] child terminated by signal %d\n", WTERMSIG(status));
        }
    } else {
        perror("fork");
    }
}

int main(void) {
    printf("[main] detachable context warm-up\n");

    uint32_t flags = TTAK_ARENA_HAS_EPOCH_RECLAMATION |
                     TTAK_ARENA_HAS_DEFAULT_EPOCH_GC |
                     TTAK_ARENA_IS_URGENT_TASK |
                     TTAK_ARENA_USE_LOCKED_ACCESS;

    ttak_detachable_context_t ctx;
    ttak_detachable_context_init(&ctx, flags);

    pthread_t threads[WORKER_COUNT];
    worker_args_t args[WORKER_COUNT];

    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        args[i].id = i;
        args[i].ctx = &ctx;
        pthread_create(&threads[i], NULL, worker_main, &args[i]);
    }

    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("[stats] cache hits=%" PRIu64 " misses=%" PRIu64 "\n",
           ctx.small_cache.hits,
           ctx.small_cache.misses);
    printf("[stats] allocations reused=%lu fresh=%lu\n",
           atomic_load_explicit(&g_cache_reuse, memory_order_relaxed),
           atomic_load_explicit(&g_fresh_allocs, memory_order_relaxed));

    run_signal_demo();

    ttak_detachable_context_destroy(&ctx);
    return 0;
}
