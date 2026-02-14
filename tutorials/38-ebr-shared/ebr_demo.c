#include <ttak/shared/shared.h>
#include <ttak/mem/epoch.h>
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
ttak_shared_config_t g_shared_config;
atomic_bool g_running = true;

/* Reader thread function */
void *reader_func(void *arg) {
    long id = (long)arg;
    ttak_owner_t owner = { .id = (uint32_t)id, .name = "reader", .prio = 1, .is_dirty = false };

    /* Register thread for EBR */
    ttak_epoch_enter(); 
    ttak_epoch_exit();
    /* Note: enter/exit registers the thread if not already registered. */

    printf("Reader %ld started.\n", id);

    while (atomic_load(&g_running)) {
        ttak_shared_result_t res;
        
        /* 
         * Use EBR-protected access (Hybrid mode).
         * protected=true means we enter the epoch, preventing reclamation of the data
         * we are about to read, even if a swap happens concurrently.
         */
        
        /* Manual EBR access */
        const my_config_t *safe_cfg = (const my_config_t *)g_shared_config.base.access_ebr(
            &g_shared_config.base, 
            &owner, 
            true, /* protected */
            &res
        );

        if (safe_cfg) {
            // printf("Reader %ld: Read version %d: %s\n", id, safe_cfg->version, safe_cfg->data);
            /* Simulate work */
            // usleep(100);
            
            /* Must release if protected=true was used */
            g_shared_config.base.release_ebr(&g_shared_config.base);
        } else {
            // printf("Reader %ld: Access denied or invalid.\n", id);
        }

        /* Occasional Rough Share (Fast, unsafe if we dereference old pointer later) */
        if (id == 1 && (rand() % 100 == 0)) {
            const my_config_t *fast_cfg = (const my_config_t *)g_shared_config.base.access_ebr(
                &g_shared_config.base, 
                &owner, 
                false, /* Not protected */
                &res
            );
            /* If we are fast enough, this is fine. But risky if swap happens now. */
            if (fast_cfg) {
                (void)fast_cfg;
            }
        }
        
        /* 
         * Simulate local work so we are not always holding the epoch.
         * Important: You must release_ebr (exit epoch) to allow global epoch to advance!
         */
        usleep(10);
    }

    ttak_epoch_deregister_thread();
    return NULL;
}

int main() {
    printf("Initializing Shared Object with EBR support...\n");
    
    ttak_shared_config_init(&g_shared_config);
    ttak_shared_config_allocate(&g_shared_config, TTAK_SHARED_LEVEL_1);
    
    /* Set initial data */
    my_config_t *initial = (my_config_t *)g_shared_config.base.shared;
    initial->version = 1;
    snprintf(initial->data, sizeof(initial->data), "Initial Config");
    
    /* Register owners */
    ttak_owner_t reader1 = { .id = 1, .name = "reader1", .prio = 1, .is_dirty = false };
    ttak_owner_t reader2 = { .id = 2, .name = "reader2", .prio = 1, .is_dirty = false };
    g_shared_config.base.add_owner(&g_shared_config.base, &reader1);
    g_shared_config.base.add_owner(&g_shared_config.base, &reader2);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, reader_func, (void *)1);
    pthread_create(&t2, NULL, reader_func, (void *)2);

    /* Writer Loop */
    for (int i = 0; i < 10; i++) {
        usleep(100000); /* 100ms */
        
        /* Prepare new data */
        my_config_t *new_data = malloc(sizeof(my_config_t));
        new_data->version = i + 2;
        snprintf(new_data->data, sizeof(new_data->data), "Config Update %d", i + 2);

        printf("Writer: Swapping to version %d...\n", new_data->version);
        
        /* Atomic Swap with EBR */
        ttak_shared_swap_ebr(&g_shared_config.base, new_data, sizeof(my_config_t));
        
        /* 
         * Trigger Reclamation.
         * In a real system, this runs on a background worker or scheduler tick.
         */
        ttak_epoch_reclaim();
    }

    g_running = false;
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("Writer: Retiring container...\n");
    /* Safe asynchronous destruction */
    g_shared_config.base.retire(&g_shared_config.base);
    
    /* 
     * Need one last reclaim cycle to actually free the container 
     * after threads have exited (or advanced).
     */
    ttak_epoch_reclaim();
    
    for(int i=0; i<3; i++) {
        ttak_epoch_reclaim();
    }

    printf("Done.\n");
    return 0;
}
