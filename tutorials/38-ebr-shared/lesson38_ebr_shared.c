#define _DEFAULT_SOURCE
#include <ttak/shared/shared.h>
#include <ttak/mem/epoch.h>
#include <ttak/mem/epoch_gc.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

/* Define a custom payload */
typedef struct {
    int version;
    char data[64];
} my_config_t;

/* Define wrapper for type safety */
TTAK_SHARED_DEFINE_WRAPPER(config, my_config_t)

/* Shared resource */
ttak_shared_config_t *g_shared_config;
atomic_bool g_running = true;

/* 
 * Global Epoch GC instance. 
 * Advanced Recommendation: In high-performance systems, EBR should always be used 
 * alongside EpochGC. While EBR (ttak_epoch_reclaim) handles the safe retirement 
 * of pointers during concurrent swaps, EpochGC (ttak_epoch_gc_rotate) manages 
 * the macro-lifecycle and deterministic cleanup of larger memory structures.
 */
ttak_epoch_gc_t g_gc;

/* Reader thread function */
void *reader_func(void *arg) {
    ttak_owner_t *owner = (ttak_owner_t *)arg;

    /* Register thread for EBR */
    ttak_epoch_register_thread();

    printf("Reader %u started.\n", owner->id);

    while (atomic_load(&g_running)) {
        ttak_shared_result_t res;
        
        /* 
         * Use EBR-protected access.
         */
        const my_config_t *safe_cfg = (const my_config_t *)g_shared_config->base.access_ebr(
            &g_shared_config->base, 
            owner, 
            true, /* protected */
            &res
        );

        if (safe_cfg) {
            /* Accessing safe_cfg here is safe thanks to EBR */
            g_shared_config->base.release_ebr(&g_shared_config->base);
        }

        usleep(10);
    }

    ttak_epoch_deregister_thread();
    return NULL;
}

int main() {
    printf("Initializing Shared Object and EpochGC...\n");
    
    /* 1. Initialize Epoch GC for long-term lifecycle management */
    ttak_epoch_gc_init(&g_gc);

    /* 2. Initialize Shared Container */
    g_shared_config = ttak_mem_alloc(sizeof(ttak_shared_config_t), 0, ttak_get_tick_count());
    ttak_shared_config_init(g_shared_config);
    ttak_shared_config_allocate(g_shared_config, TTAK_SHARED_LEVEL_1);
    
    /* Set initial data */
    my_config_t *initial = (my_config_t *)g_shared_config->base.shared;
    initial->version = 1;
    snprintf(initial->data, sizeof(initial->data), "Initial Config");
    
    /* Create and register owners */
    ttak_owner_t *reader1 = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    ttak_owner_t *reader2 = ttak_owner_create(TTAK_OWNER_SAFE_DEFAULT);
    
    g_shared_config->base.add_owner(&g_shared_config->base, reader1);
    g_shared_config->base.add_owner(&g_shared_config->base, reader2);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, reader_func, reader1);
    pthread_create(&t2, NULL, reader_func, reader2);

    /* Writer Loop */
    for (int i = 0; i < 10; i++) {
        usleep(100000); /* 100ms */
        
        /* Prepare new data */
        my_config_t *new_data = ttak_mem_alloc(sizeof(my_config_t), 0, ttak_get_tick_count());
        new_data->version = i + 2;
        snprintf(new_data->data, sizeof(new_data->data), "Config Update %d", i + 2);

        printf("Writer: Swapping to version %d...\n", new_data->version);
        
        /* 
         * Atomic Swap with EBR.
         * The internal buffer is managed by EBR retirement.
         */
        ttak_shared_swap_ebr(&g_shared_config->base, new_data, sizeof(my_config_t));
        
        /* Free our local temporary buffer */
        ttak_mem_free(new_data);
        
        /* 
         * 3. Trigger EBR Reclamation.
         * Advanced Recommendation: Always pair EBR reclaim with EpochGC rotation.
         */
        ttak_epoch_reclaim();

        /*
         * 4. Rotate Epoch GC.
         * This ensures that any large-scale retired nodes or metadata in the 
         * memory tree are also processed.
         */
        ttak_epoch_gc_rotate(&g_gc);
    }

    g_running = false;
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("Writer: Retiring container...\n");
    /* Safe asynchronous retirement of internal contents */
    g_shared_config->base.retire(&g_shared_config->base);
    
    /* 
     * Final flush for both systems to ensure all retired memory is reclaimed.
     */
    for(int i = 0; i < 5; i++) {
        ttak_epoch_reclaim();
        ttak_epoch_gc_rotate(&g_gc);
    }

    /* Cleanup remaining structures */
    ttak_owner_destroy(reader1);
    ttak_owner_destroy(reader2);
    ttak_epoch_gc_destroy(&g_gc);
    /* Note: g_shared_config is freed by the EBR retirement process triggered by retire() */

    printf("Done.\n");
    return 0;
}
