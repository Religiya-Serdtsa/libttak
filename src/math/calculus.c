#include <ttak/math/calculus.h>
#include <ttak/thread/pool.h>
#include <ttak/async/future.h>
#include <ttak/mem/mem.h>
#include <stdio.h>

_Bool ttak_calculus_diff(ttak_bigreal_t *res, ttak_math_func_t f, const ttak_bigreal_t *x, void *ctx, uint64_t now) {
    if (!res || !f || !x) return false;
    
    // f'(x) approx (f(x+h) - f(x-h)) / 2h
    ttak_bigreal_t h, x_plus, x_minus, f_plus, f_minus, num, den;
    ttak_bigreal_init(&h, now);
    ttak_bigreal_init(&x_plus, now);
    ttak_bigreal_init(&x_minus, now);
    ttak_bigreal_init(&f_plus, now);
    ttak_bigreal_init(&f_minus, now);
    ttak_bigreal_init(&num, now);
    ttak_bigreal_init(&den, now);
    
    // Set small h
    ttak_bigreal_init_u64(&h, 1, now);
    h.exponent = -10; // h = 2^-10
    
    ttak_bigreal_add(&x_plus, x, &h, now);
    ttak_bigreal_sub(&x_minus, x, &h, now);
    
    if (!f(&f_plus, &x_plus, ctx, now) || !f(&f_minus, &x_minus, ctx, now)) {
        goto cleanup;
    }
    
    ttak_bigreal_sub(&num, &f_plus, &f_minus, now);
    ttak_bigreal_init_u64(&den, 2, now);
    ttak_bigreal_mul(&den, &den, &h, now);
    
    ttak_bigreal_div(res, &num, &den, now);
    
cleanup:
    ttak_bigreal_free(&h, now);
    ttak_bigreal_free(&x_plus, now);
    ttak_bigreal_free(&x_minus, now);
    ttak_bigreal_free(&f_plus, now);
    ttak_bigreal_free(&f_minus, now);
    ttak_bigreal_free(&num, now);
    ttak_bigreal_free(&den, now);
    
    return true;
}

_Bool ttak_calculus_partial_diff(ttak_bigreal_t *res, ttak_math_func_t f, const ttak_bigreal_t *x_vec, uint8_t dim, void *ctx, uint64_t now) {
    (void)res; (void)f; (void)x_vec; (void)dim; (void)ctx; (void)now;
    // Similar to diff, but x_vec is a pointer to an array of bigreals.
    // We only modify x_vec[dim].
    // Since ttak_math_func_t takes a single bigreal pointer, we might need a different signature or ctx.
    return false; // Not implemented for now.
}

typedef struct {
    ttak_math_func_t f;
    ttak_bigreal_t a, b;
    void *ctx;
    uint64_t now;
    ttak_bigreal_t result;
} integrate_interval_t;

static void* integrate_worker(void *arg) {
    integrate_interval_t *task = (integrate_interval_t *)arg;
    
    // Simple Simpson's rule for the interval [a, b]
    ttak_bigreal_t h, mid, fa, fb, fmid, t1, t2;
    ttak_bigreal_init(&h, task->now);
    ttak_bigreal_init(&mid, task->now);
    ttak_bigreal_init(&fa, task->now);
    ttak_bigreal_init(&fb, task->now);
    ttak_bigreal_init(&fmid, task->now);
    ttak_bigreal_init(&t1, task->now);
    ttak_bigreal_init(&t2, task->now);
    
    task->f(&fa, &task->a, task->ctx, task->now);
    task->f(&fb, &task->b, task->ctx, task->now);
    
    ttak_bigreal_add(&mid, &task->a, &task->b, task->now);
    ttak_bigreal_init_u64(&t1, 2, task->now);
    ttak_bigreal_div(&mid, &mid, &t1, task->now);
    
    task->f(&fmid, &mid, task->ctx, task->now);
    
    // Result = (b-a)/6 * (fa + 4*fmid + fb)
    ttak_bigreal_sub(&h, &task->b, &task->a, task->now);
    ttak_bigreal_init_u64(&t1, 6, task->now);
    ttak_bigreal_div(&h, &h, &t1, task->now);
    
    ttak_bigreal_init_u64(&t1, 4, task->now);
    ttak_bigreal_mul(&t1, &t1, &fmid, task->now);
    ttak_bigreal_add(&t1, &t1, &fa, task->now);
    ttak_bigreal_add(&t1, &t1, &fb, task->now);
    
    ttak_bigreal_mul(&task->result, &h, &t1, task->now);
    
    ttak_bigreal_free(&h, task->now);
    ttak_bigreal_free(&mid, task->now);
    ttak_bigreal_free(&fa, task->now);
    ttak_bigreal_free(&fb, task->now);
    ttak_bigreal_free(&fmid, task->now);
    ttak_bigreal_free(&t1, task->now);
    ttak_bigreal_free(&t2, task->now);
    
    return NULL;
}

_Bool ttak_calculus_integrate(ttak_bigreal_t *res, ttak_math_func_t f, const ttak_bigreal_t *a, const ttak_bigreal_t *b, void *ctx, uint64_t now) {
    if (!res || !f || !a || !b) return false;
    
    int num_intervals = 4;
    integrate_interval_t intervals[4];
    ttak_future_t *futures[4];
    
    ttak_bigreal_t step, current;
    ttak_bigreal_init(&step, now);
    ttak_bigreal_init(&current, now);
    
    ttak_bigreal_sub(&step, b, a, now);
    ttak_bigreal_init_u64(&current, num_intervals, now);
    ttak_bigreal_div(&step, &step, &current, now);
    
    ttak_bigreal_copy(&current, a, now);
    
    for (int i = 0; i < num_intervals; i++) {
        intervals[i].f = f;
        intervals[i].ctx = ctx;
        intervals[i].now = now;
        ttak_bigreal_init(&intervals[i].result, now);
        ttak_bigreal_copy(&intervals[i].a, &current, now);
        ttak_bigreal_add(&current, &current, &step, now);
        ttak_bigreal_copy(&intervals[i].b, &current, now);
        
        futures[i] = ttak_thread_pool_submit_task(async_pool, integrate_worker, &intervals[i], __TT_SCHED_NORMAL__, now);
    }
    
    ttak_bigreal_init_u64(res, 0, now);
    for (int i = 0; i < num_intervals; i++) {
        if (futures[i]) {
            ttak_future_get(futures[i]);
            ttak_bigreal_add(res, res, &intervals[i].result, now);
            // ttak_future_destroy(futures[i]); // Assuming we need to destroy it
        }
    }
    
    for (int i = 0; i < num_intervals; i++) {
        ttak_bigreal_free(&intervals[i].a, now);
        ttak_bigreal_free(&intervals[i].b, now);
        ttak_bigreal_free(&intervals[i].result, now);
    }
    ttak_bigreal_free(&step, now);
    ttak_bigreal_free(&current, now);
    
    return true;
}
