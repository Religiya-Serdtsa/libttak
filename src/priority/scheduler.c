#include <ttak/priority/scheduler.h>
#include <ttak/mem/mem.h>
#include <ttak/ht/map.h>
#include <ttak/timing/timing.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>

static tt_map_t *history_map = NULL;
static pthread_mutex_t history_lock = PTHREAD_MUTEX_INITIALIZER;
static _Bool history_initialized = 0;

void ttak_scheduler_init(void) {
    pthread_mutex_lock(&history_lock);
    if (!history_initialized) {
        history_map = ttak_create_map(128, ttak_get_tick_count());
        if (history_map) history_initialized = 1;
    }
    pthread_mutex_unlock(&history_lock);
}

void ttak_scheduler_record_execution(ttak_task_t *task, uint64_t duration_ms) {
    if (!task) return;
    uint64_t hash = ttak_task_get_hash(task);
    if (hash == 0) return;
    uint64_t now = ttak_get_tick_count();
    pthread_mutex_lock(&history_lock);
    if (history_map) {
        size_t old_avg = 0;
        if (ttak_map_get_key(history_map, (uintptr_t)hash, &old_avg, now)) {
            size_t new_avg = (size_t)((old_avg * 0.7) + (duration_ms * 0.3));
            ttak_insert_to_map(history_map, (uintptr_t)hash, new_avg, now);
        } else {
            ttak_insert_to_map(history_map, (uintptr_t)hash, (size_t)duration_ms, now);
        }
    }
    pthread_mutex_unlock(&history_lock);
}

int ttak_scheduler_get_adjusted_priority(ttak_task_t *task, int base_priority) {
    if (!task) return base_priority;
    uint64_t hash = ttak_task_get_hash(task);
    if (hash == 0) return base_priority;
    int adj_priority = base_priority;
    uint64_t now = ttak_get_tick_count();
    size_t avg_runtime = 0;
    _Bool found = 0;
    pthread_mutex_lock(&history_lock);
    if (history_map) found = ttak_map_get_key(history_map, (uintptr_t)hash, &avg_runtime, now);
    pthread_mutex_unlock(&history_lock);
    if (found) {
        if (avg_runtime < 10) adj_priority += 5;
        else if (avg_runtime < 50) adj_priority += 2;
        else if (avg_runtime > 2000) adj_priority -= 5;
        else if (avg_runtime > 500) adj_priority -= 2;
    } else adj_priority += 1;
    return adj_priority;
}

static int sched_get_current_priority(ttak_scheduler_t *sched) { (void)sched; return 0; }
static void sched_set_priority_override(ttak_scheduler_t *sched, ttak_task_t *task, int new_priority) { (void)sched; (void)task; (void)new_priority; }
static size_t sched_get_pending_count(ttak_scheduler_t *sched) { (void)sched; return 0; }
static size_t sched_get_running_count(ttak_scheduler_t *sched) { (void)sched; return 0; }
static double sched_get_load_average(ttak_scheduler_t *sched) { (void)sched; return 0.0; }

static ttak_scheduler_t global_scheduler = {
    .get_current_priority = sched_get_current_priority,
    .set_priority_override = sched_set_priority_override,
    .get_pending_count = sched_get_pending_count,
    .get_running_count = sched_get_running_count,
    .get_load_average = sched_get_load_average
};

ttak_scheduler_t *ttak_scheduler_get_instance(void) { return &global_scheduler; }
