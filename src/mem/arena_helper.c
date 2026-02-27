#include <ttak/mem/arena_helper.h>
#include <ttak/mem_tree/mem_tree.h>
#include <ttak/timing/timing.h>

#include <string.h>
#include <stdint.h>

#define TTAK_ARENA_LATIN_DIM 8U
#define TTAK_ARENA_LATIN_MASK 63U

/* Latin-square offsets inspired by Choi Seok-jeong's Gusu-ryak work. */
static const uint8_t ttak_arena_latin_square_lut[TTAK_ARENA_LATIN_DIM * TTAK_ARENA_LATIN_DIM] = {
    0, 5, 2, 7, 4, 1, 6, 3,
    3, 0, 5, 2, 7, 4, 1, 6,
    6, 3, 0, 5, 2, 7, 4, 1,
    1, 6, 3, 0, 5, 2, 7, 4,
    4, 1, 6, 3, 0, 5, 2, 7,
    7, 4, 1, 6, 3, 0, 5, 2,
    2, 7, 4, 1, 6, 3, 0, 5,
    5, 2, 7, 4, 1, 6, 3, 0
};

static inline size_t ttak_cacheline_pad(size_t bytes) {
    const size_t mask = TTAK_CACHE_LINE_SIZE - 1U;
    return (bytes + mask) & ~mask;
}

static inline size_t ttak_arena_scatter_offset(uint32_t epoch_id) {
    uint32_t idx = epoch_id & TTAK_ARENA_LATIN_MASK;
    return ((size_t)ttak_arena_latin_square_lut[idx]) << 6;
}

static void Arena_Reset_Routine(ttak_arena_generation_t *generation) {
    if (!generation) {
        return;
    }
    generation->epoch_id = 0;
}

void ttak_arena_env_config_init(ttak_arena_env_config_t *config) {
    if (!config) {
        return;
    }

    config->gc = NULL;
    config->generation_bytes = 4096;
    config->chunk_bytes = 128;
    config->alloc_flags = TTAK_MEM_CACHE_ALIGNED | TTAK_MEM_STRICT_CHECK;
    config->lifetime_ticks = __TTAK_UNSAFE_MEM_FOREVER__;
}

static void clamp_config(ttak_arena_env_config_t *config) {
    if (!config->generation_bytes) {
        config->generation_bytes = 4096;
    }
    if (!config->chunk_bytes || config->chunk_bytes > config->generation_bytes) {
        config->chunk_bytes = config->generation_bytes;
    }
    if (!config->alloc_flags) {
        config->alloc_flags = TTAK_MEM_CACHE_ALIGNED | TTAK_MEM_STRICT_CHECK;
    }
    if (!config->lifetime_ticks) {
        config->lifetime_ticks = __TTAK_UNSAFE_MEM_FOREVER__;
    }
}

bool ttak_arena_env_init(ttak_arena_env_t *env, const ttak_arena_env_config_t *config) {
    if (!env) {
        return false;
    }

    ttak_arena_env_config_t local_cfg;
    if (!config) {
        ttak_arena_env_config_init(&local_cfg);
        config = &local_cfg;
    } else {
        local_cfg = *config;
    }

    clamp_config(&local_cfg);
    env->config = local_cfg;
    env->gc = env->config.gc;
    env->owns_gc = false;
    memset(&env->local_gc, 0, sizeof(env->local_gc));

    if (!env->gc) {
        env->gc = &env->local_gc;
        env->owns_gc = true;
        ttak_epoch_gc_init(env->gc);
    }

    return true;
}

void ttak_arena_env_destroy(ttak_arena_env_t *env) {
    if (!env) {
        return;
    }

    if (env->owns_gc && env->gc) {
        ttak_epoch_gc_destroy(env->gc);
    }

    env->gc = NULL;
    env->owns_gc = false;
    memset(&env->local_gc, 0, sizeof(env->local_gc));
}

bool ttak_arena_generation_begin(ttak_arena_env_t *env, ttak_arena_generation_t *generation, uint32_t epoch_id) {
    if (!env || !env->gc || !generation) {
        return false;
    }

    uint64_t now = ttak_get_tick_count();
    size_t buffer_bytes = env->config.generation_bytes;
    void *buffer = ttak_mem_alloc_with_flags(buffer_bytes, env->config.lifetime_ticks, now, env->config.alloc_flags);
    if (!buffer) {
        return false;
    }

    generation->base = buffer;
    generation->capacity = buffer_bytes;
    generation->used = 0;
    generation->epoch_id = epoch_id;

    ttak_epoch_gc_register(env->gc, buffer, buffer_bytes);
    return true;
}

void ttak_arena_generation_reset(ttak_arena_generation_t *generation) {
    Arena_Reset_Routine(generation);
}

void *ttak_arena_generation_claim(ttak_arena_env_t *env, ttak_arena_generation_t *generation, size_t bytes) {
    if (!env || !generation || !generation->base) {
        return NULL;
    }

    size_t chunk = bytes ? bytes : env->config.chunk_bytes;
    if (!chunk || chunk > env->config.generation_bytes) {
        return NULL;
    }

    size_t aligned_chunk = ttak_cacheline_pad(chunk);
    size_t offset = ttak_arena_scatter_offset(generation->epoch_id);
    size_t required = aligned_chunk + offset;
    if (generation->used + required > generation->capacity) {
        Arena_Reset_Routine(generation);
        return NULL;
    }

    uint8_t *slot = (uint8_t *)generation->base + generation->used + offset;
    generation->used += required;
    generation->epoch_id++;
    return slot;
}

size_t ttak_arena_generation_remaining(const ttak_arena_generation_t *generation) {
    if (!generation || !generation->base || generation->used > generation->capacity) {
        return 0;
    }

    return generation->capacity - generation->used;
}

bool ttak_arena_generation_retire(ttak_arena_env_t *env, ttak_arena_generation_t *generation) {
    if (!env || !generation || !generation->base) {
        return false;
    }

    bool released = false;
    if (env->gc) {
        ttak_mem_node_t *node = ttak_mem_tree_find_node(&env->gc->tree, generation->base);
        if (node) {
            ttak_mem_node_release(node);
            ttak_mem_tree_report_pressure(&env->gc->tree, generation->capacity);
            released = true;
        }
    }

    generation->base = NULL;
    generation->capacity = 0;
    generation->used = 0;
    return released;
}

void ttak_arena_env_rotate(ttak_arena_env_t *env) {
    if (!env || !env->gc) {
        return;
    }

    ttak_epoch_gc_rotate(env->gc);
}

size_t ttak_arena_generation_for_each(ttak_arena_env_t *env,
                                      ttak_arena_generation_t *generation,
                                      size_t chunk_bytes,
                                      ttak_arena_chunk_handler_t handler,
                                      void *ctx) {
    if (!env || !generation || !handler) {
        return 0;
    }

    size_t effective_chunk = chunk_bytes ? chunk_bytes : env->config.chunk_bytes;
    if (!effective_chunk || effective_chunk > env->config.generation_bytes) {
        effective_chunk = env->config.chunk_bytes;
    }

    size_t processed = 0;
    while (true) {
        void *chunk = ttak_arena_generation_claim(env, generation, chunk_bytes);
        if (!chunk) {
            break;
        }

        bool should_continue = handler(chunk, effective_chunk, processed, ctx);
        processed++;
        if (!should_continue) {
            break;
        }
    }

    return processed;
}
