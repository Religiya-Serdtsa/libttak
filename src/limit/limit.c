#include <ttak/limit/limit.h>
#include <ttak/timing/timing.h>
#include <ttak/sync/spinlock.h>
#include <math.h>

/**
 * @brief Initializes a token bucket with specific rate and burst parameters.
 * 
 * @param tb Pointer to the bucket structure.
 * @param rate Refill rate (tokens/sec).
 * @param burst Burst capacity.
 */
void ttak_token_bucket_init(ttak_token_bucket_t *tb, double rate, double burst) {
    tb->tokens = burst;
    tb->max_tokens = burst;
    tb->refill_rate = rate; 
    tb->last_refill_ts = ttak_get_tick_count();
    ttak_spin_init(&tb->lock);
}

/**
 * @brief Consumes tokens from the bucket if available.
 * 
 * Refills the bucket based on elapsed time before attempting consumption.
 * 
 * @param tb Pointer to the bucket.
 * @param tokens Amount to consume.
 * @return true if successful, false if insufficient tokens.
 */
bool ttak_token_bucket_consume(ttak_token_bucket_t *tb, double tokens) {
    ttak_spin_lock(&tb->lock);
    
    uint64_t now = ttak_get_tick_count();
    uint64_t elapsed = now - tb->last_refill_ts;
    
    // Refill logic: (Elapsed MS) * (Rate / 1000)
    if (elapsed > 0) {
        double new_tokens = (double)elapsed * (tb->refill_rate / 1000.0);
        tb->tokens = fmin(tb->max_tokens, tb->tokens + new_tokens);
        tb->last_refill_ts = now;
    }
    
    // Consume logic
    if (tb->tokens >= tokens) {
        tb->tokens -= tokens;
        ttak_spin_unlock(&tb->lock);
        return true;
    }
    
    ttak_spin_unlock(&tb->lock);
    return false;
}

/**
 * @brief Wrapper init for rate limiter.
 */
void ttak_ratelimit_init(ttak_ratelimit_t *rl, double rate, double burst) {
    ttak_token_bucket_init(&rl->bucket, rate, burst);
}

/**
 * @brief Wrapper allow for rate limiter (consumes 1 unit).
 */
bool ttak_ratelimit_allow(ttak_ratelimit_t *rl) {
    return ttak_token_bucket_consume(&rl->bucket, 1.0);
}