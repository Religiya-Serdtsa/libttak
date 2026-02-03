#ifndef TTAK_LIMIT_LIMIT_H
#define TTAK_LIMIT_LIMIT_H

#include <stdint.h>
#include <stdbool.h>
#include <ttak/sync/sync.h>
#include <ttak/sync/spinlock.h>

/**
 * @brief Token Bucket structure for rate limiting.
 * 
 * Implements the standard token bucket algorithm.
 * Thread-safe using a spinlock.
 */
typedef struct ttak_token_bucket {
    double tokens;              /**< Current number of tokens available. */
    double max_tokens;          /**< Maximum bucket capacity (burst size). */
    double refill_rate;         /**< Rate at which tokens are refilled (tokens per second). */
    uint64_t last_refill_ts;    /**< Timestamp of the last refill operation. */
    ttak_spin_t lock;           /**< Spinlock for thread safety. */
} ttak_token_bucket_t;

typedef ttak_token_bucket_t tt_token_bucket_t;

/**
 * @brief Initializes a token bucket.
 * 
 * @param tb Pointer to the bucket.
 * @param rate Refill rate in tokens per second.
 * @param burst Maximum burst size (capacity).
 */
void ttak_token_bucket_init(ttak_token_bucket_t *tb, double rate, double burst);

/**
 * @brief Attempts to consume tokens from the bucket.
 * 
 * @param tb Pointer to the bucket.
 * @param tokens Number of tokens to consume.
 * @return true if tokens were available and consumed.
 * @return false if there were insufficient tokens.
 */
bool ttak_token_bucket_consume(ttak_token_bucket_t *tb, double tokens);

/**
 * @brief Rate Limiter structure (wrapper around Token Bucket).
 * 
 * Simplifies rate limiting for binary allow/deny decisions.
 */
typedef struct ttak_ratelimit {
    ttak_token_bucket_t bucket; /**< Underlying token bucket. */
} ttak_ratelimit_t;

typedef ttak_ratelimit_t tt_ratelimit_t;

/**
 * @brief Initializes a rate limiter.
 * 
 * @param rl Pointer to the rate limiter.
 * @param rate Allowed rate (requests per second).
 * @param burst Maximum burst size.
 */
void ttak_ratelimit_init(ttak_ratelimit_t *rl, double rate, double burst);

/**
 * @brief Checks if an action is allowed by the rate limiter.
 * 
 * Consumes 1.0 token.
 * 
 * @param rl Pointer to the rate limiter.
 * @return true if allowed, false otherwise.
 */
bool ttak_ratelimit_allow(ttak_ratelimit_t *rl);

#endif // TTAK_LIMIT_LIMIT_H