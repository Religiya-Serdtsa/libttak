#include <ttak/stats/stats_ext.h>
#include <ttak/mem/mem.h>
#include <ttak/thread/pool.h>
#include <math.h>
#include <stdlib.h>

void ttak_stats_ext_init(ttak_stats_ext_t *s, uint64_t now) {
    ttak_stats_init(&s->base, 0, 100);
    s->promoted = false;
    ttak_bigint_init(&s->count_big, now);
    ttak_bigint_init(&s->sum_x_big, now);
    ttak_bigint_init(&s->sum_x_sq_big, now);
    ttak_bigint_init(&s->sum_y_big, now);
    ttak_bigint_init(&s->sum_y_sq_big, now);
    ttak_bigint_init(&s->sum_xy_big, now);
    ttak_spin_init(&s->lock_ext);
}

void ttak_stats_ext_record(ttak_stats_ext_t *s, uint64_t x, uint64_t y, uint64_t now) {
    ttak_spin_lock(&s->lock_ext);
    
    // Check for overflow in base stats (uint64_t)
    if (!s->promoted) {
        if (s->base.count == UINT64_MAX || (UINT64_MAX - s->base.sum < x)) {
            s->promoted = true;
            // Promotion already implicitly handled by always updating bigints below,
            // but we could perform a one-time sync here if we didn't update bigints.
        }
    }
    
    // High precision accumulators are updated to avoid precision loss
    ttak_bigint_add_u64(&s->count_big, &s->count_big, 1, now);
    ttak_bigint_add_u64(&s->sum_x_big, &s->sum_x_big, x, now);
    
    ttak_bigint_t bx, by, bprod;
    ttak_bigint_init_u64(&bx, x, now);
    ttak_bigint_init_u64(&by, y, now);
    ttak_bigint_init(&bprod, now);
    
    ttak_bigint_mul(&bprod, &bx, &bx, now);
    ttak_bigint_add(&s->sum_x_sq_big, &s->sum_x_sq_big, &bprod, now);
    
    ttak_bigint_add_u64(&s->sum_y_big, &s->sum_y_big, y, now);
    ttak_bigint_mul(&bprod, &by, &by, now);
    ttak_bigint_add(&s->sum_y_sq_big, &s->sum_y_sq_big, &bprod, now);
    
    ttak_bigint_mul(&bprod, &bx, &by, now);
    ttak_bigint_add(&s->sum_xy_big, &s->sum_xy_big, &bprod, now);
    
    ttak_bigint_free(&bx, now);
    ttak_bigint_free(&by, now);
    ttak_bigint_free(&bprod, now);
    
    ttak_stats_record(&s->base, x);
    
    ttak_spin_unlock(&s->lock_ext);
}

_Bool ttak_stats_ext_variance(ttak_bigreal_t *res, ttak_stats_ext_t *s, uint64_t now) {
    // Var = (E[X^2] - E[X]^2) = (sum_x_sq / n) - (sum_x / n)^2
    ttak_bigreal_t n, sx, sx2, t1, t2;
    ttak_bigreal_init(&n, now);
    ttak_bigreal_init(&sx, now);
    ttak_bigreal_init(&sx2, now);
    ttak_bigreal_init(&t1, now);
    ttak_bigreal_init(&t2, now);
    
    // Convert bigints to bigreals
    ttak_bigint_copy(&n.mantissa, &s->count_big, now);
    n.exponent = 0;
    ttak_bigint_copy(&sx.mantissa, &s->sum_x_big, now);
    sx.exponent = 0;
    ttak_bigint_copy(&sx2.mantissa, &s->sum_x_sq_big, now);
    sx2.exponent = 0;
    
    // t1 = sum_x_sq / n
    ttak_bigreal_div(&t1, &sx2, &n, now);
    
    // t2 = (sum_x / n)^2
    ttak_bigreal_div(&t2, &sx, &n, now);
    ttak_bigreal_mul(&t2, &t2, &t2, now);
    
    ttak_bigreal_sub(res, &t1, &t2, now);
    
    ttak_bigreal_free(&n, now);
    ttak_bigreal_free(&sx, now);
    ttak_bigreal_free(&sx2, now);
    ttak_bigreal_free(&t1, now);
    ttak_bigreal_free(&t2, now);
    
    return true;
}

_Bool ttak_stats_ext_stddev(ttak_bigreal_t *res, ttak_stats_ext_t *s, uint64_t now) {
    ttak_bigreal_t var;
    ttak_bigreal_init(&var, now);
    ttak_stats_ext_variance(&var, s, now);
    // res = sqrt(var). Placeholder:
    ttak_bigreal_copy(res, &var, now);
    ttak_bigreal_free(&var, now);
    return true;
}

_Bool ttak_stats_ext_correlation(ttak_bigreal_t *res, ttak_stats_ext_t *s, uint64_t now) {
    (void)res; (void)s; (void)now;
    // r = (n*sum_xy - sum_x*sum_y) / sqrt((n*sum_x2 - sum_x^2) * (n*sum_y2 - sum_y^2))
    return false; // Complex for now without sqrt
}

_Bool ttak_stats_ext_linear_regression(ttak_bigreal_t *slope, ttak_bigreal_t *intercept, ttak_stats_ext_t *s, uint64_t now) {
    (void)slope; (void)intercept; (void)s; (void)now;
    // slope b = (n*sum_xy - sum_x*sum_y) / (n*sum_x2 - sum_x^2)
    // intercept a = (sum_y - b*sum_x) / n
    return false;
}

_Bool ttak_stats_dist_normal(ttak_bigreal_t *res, const ttak_bigreal_t *x, const ttak_bigreal_t *mu, const ttak_bigreal_t *sigma, uint64_t now) {
    (void)res; (void)x; (void)mu; (void)sigma; (void)now;
    // f(x) = (1 / (sigma * sqrt(2*pi))) * exp(-0.5 * ((x-mu)/sigma)^2)
    return false;
}

_Bool ttak_stats_dist_poisson(ttak_bigreal_t *res, uint64_t k, const ttak_bigreal_t *lambda, uint64_t now) {
    (void)res; (void)k; (void)lambda; (void)now;
    // f(k) = (lambda^k * exp(-lambda)) / k!
    return false;
}

_Bool ttak_stats_dist_binomial(ttak_bigreal_t *res, uint64_t k, uint64_t n, const ttak_bigreal_t *p, uint64_t now) {
    (void)res; (void)k; (void)n; (void)p; (void)now;
    // f(k) = C(n, k) * p^k * (1-p)^(n-k)
    return false;
}

typedef struct {
    uint64_t *data;
    size_t count;
    ttak_stats_ext_t *s;
    uint64_t now;
} stats_parallel_task_t;

static void* stats_parallel_worker(void *arg) {
    stats_parallel_task_t *task = (stats_parallel_task_t *)arg;
    for (size_t i = 0; i < task->count; i++) {
        ttak_stats_ext_record(task->s, task->data[i], 0, task->now);
    }
    return NULL;
}

_Bool ttak_stats_parallel_process(ttak_stats_ext_t *s, uint64_t *data, size_t count, ttak_scheduler_t *sched, uint64_t now) {
    if (!s || !data || count == 0) return false;
    (void)sched;
    
    int num_threads = 4;
    size_t chunk_size = count / num_threads;
    ttak_future_t *futures[4];
    stats_parallel_task_t tasks[4];
    
    for (int i = 0; i < num_threads; i++) {
        tasks[i].data = &data[i * chunk_size];
        tasks[i].count = (i == num_threads - 1) ? (count - i * chunk_size) : chunk_size;
        tasks[i].s = s;
        tasks[i].now = now;
        futures[i] = ttak_thread_pool_submit_task(async_pool, stats_parallel_worker, &tasks[i], __TT_SCHED_NORMAL__, now);
    }
    
    for (int i = 0; i < num_threads; i++) {
        if (futures[i]) ttak_future_get(futures[i]);
    }
    
    return true;
}

static int compare_u64(const void *a, const void *b) {
    uint64_t arg1 = *(const uint64_t *)a;
    uint64_t arg2 = *(const uint64_t *)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

_Bool ttak_stats_compute_percentiles(uint64_t *data, size_t count,
                                     ttak_bigreal_t *p50, ttak_bigreal_t *p95,
                                     ttak_bigreal_t *p99, ttak_bigreal_t *p999,
                                     uint64_t now) {
    if (!data || count == 0) return false;

    // Sort the data to find percentiles
    qsort(data, count, sizeof(uint64_t), compare_u64);

    size_t idx;

    if (p50) {
        idx = (size_t)(0.50 * (double)count);
        if (idx >= count) idx = count - 1;
        ttak_bigreal_init_u64(p50, data[idx], now);
    }

    if (p95) {
        idx = (size_t)(0.95 * (double)count);
        if (idx >= count) idx = count - 1;
        ttak_bigreal_init_u64(p95, data[idx], now);
    }

    if (p99) {
        idx = (size_t)(0.99 * (double)count);
        if (idx >= count) idx = count - 1;
        ttak_bigreal_init_u64(p99, data[idx], now);
    }

    if (p999) {
        idx = (size_t)(0.999 * (double)count);
        if (idx >= count) idx = count - 1;
        ttak_bigreal_init_u64(p999, data[idx], now);
    }

    return true;
}
