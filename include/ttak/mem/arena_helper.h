#ifndef TTAK_MEM_ARENA_HELPER_H
#define TTAK_MEM_ARENA_HELPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ttak/mem/mem.h>
#include <ttak/mem/epoch_gc.h>

/**
 * @brief Configuration for arena helpers.
 */
typedef struct ttak_arena_env_config {
    ttak_epoch_gc_t *gc;           /**< Optional external GC context. */
    size_t generation_bytes;       /**< Capacity of each generation buffer. */
    size_t chunk_bytes;            /**< Default chunk size carved from a generation. */
    ttak_mem_flags_t alloc_flags;  /**< Flags passed to ttak_mem_alloc_with_flags. */
    uint64_t lifetime_ticks;       /**< Lifetime hint for allocations. */
} ttak_arena_env_config_t;

/**
 * @brief Helper state coordinating arena generations and epoch rotation.
 */
typedef struct ttak_arena_env {
    ttak_arena_env_config_t config;
    ttak_epoch_gc_t *gc;
    ttak_epoch_gc_t local_gc;
    bool owns_gc;
} ttak_arena_env_t;

/**
 * @brief Fixed-width arena generation descriptor.
 */
typedef struct ttak_arena_generation {
    void *base;
    size_t capacity;
    size_t used;
    uint32_t epoch_id;
} ttak_arena_generation_t;

/**
 * @brief Callback invoked for each carved chunk inside a generation.
 *
 * Returning false stops iteration early.
 */
typedef bool (*ttak_arena_chunk_handler_t)(void *chunk, size_t chunk_bytes, size_t chunk_index, void *ctx);

/**
 * @brief Initializes the config structure with conservative defaults.
 */
void ttak_arena_env_config_init(ttak_arena_env_config_t *config);

/**
 * @brief Initializes an arena helper context.
 *
 * Uses the config's GC if provided, otherwise provisions a local GC.
 */
bool ttak_arena_env_init(ttak_arena_env_t *env, const ttak_arena_env_config_t *config);

/**
 * @brief Destroys an arena helper context and owned GC state.
 */
void ttak_arena_env_destroy(ttak_arena_env_t *env);

/**
 * @brief Allocates and registers a new generation buffer.
 */
bool ttak_arena_generation_begin(ttak_arena_env_t *env, ttak_arena_generation_t *generation, uint32_t epoch_id);

/**
 * @brief Resets the scatter epoch, executing the Arena_Reset_Routine.
 */
void ttak_arena_generation_reset(ttak_arena_generation_t *generation);

/**
 * @brief Claims a chunk from a generation, using the config chunk_bytes when bytes is zero.
 */
void *ttak_arena_generation_claim(ttak_arena_env_t *env, ttak_arena_generation_t *generation, size_t bytes);

/**
 * @brief Returns the remaining capacity inside the generation.
 */
size_t ttak_arena_generation_remaining(const ttak_arena_generation_t *generation);

/**
 * @brief Retires the generation, dropping the mem-tree reference and resetting the descriptor.
 */
bool ttak_arena_generation_retire(ttak_arena_env_t *env, ttak_arena_generation_t *generation);

/**
 * @brief Convenience helper that rotates the underlying epoch GC.
 */
void ttak_arena_env_rotate(ttak_arena_env_t *env);

/**
 * @brief Walks every chunk inside the generation and invokes the handler.
 *
 * @param env Arena helper context.
 * @param generation Active generation descriptor.
 * @param chunk_bytes Size of each chunk; falls back to config.chunk_bytes when zero.
 * @param handler Callback invoked for each carved chunk.
 * @param ctx User context forwarded to the handler.
 * @return Number of chunks processed.
 */
size_t ttak_arena_generation_for_each(ttak_arena_env_t *env,
                                      ttak_arena_generation_t *generation,
                                      size_t chunk_bytes,
                                      ttak_arena_chunk_handler_t handler,
                                      void *ctx);

#endif /* TTAK_MEM_ARENA_HELPER_H */
