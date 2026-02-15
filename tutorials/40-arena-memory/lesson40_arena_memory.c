#include <ttak/mem/arena_helper.h>
#include <ttak/mem_tree/mem_tree.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    uint32_t generation_id;
} fill_context_t;

static bool fill_chunk(void *chunk, size_t chunk_bytes, size_t chunk_index, void *ctx) {
    fill_context_t *info = (fill_context_t *)ctx;
    size_t pattern = (info->generation_id + 1u) * 13u + chunk_index;
    memset(chunk, (int)(pattern & 0xFF), chunk_bytes);

    if (chunk_index % 8 == 0) {
        printf("  chunk[%zu] => %p pattern=0x%02zx\n", chunk_index, chunk, pattern & 0xFF);
    }

    return true;
}

int main(void) {
    ttak_arena_env_config_t config;
    ttak_arena_env_config_init(&config);
    config.generation_bytes = 4096;
    config.chunk_bytes = 128;

    ttak_arena_env_t env;
    if (!ttak_arena_env_init(&env, &config)) {
        fprintf(stderr, "[arena] failed to initialize env\n");
        return 1;
    }

    const uint32_t generations = 3;
    for (uint32_t gen = 0; gen < generations; ++gen) {
        ttak_arena_generation_t generation = {0};
        if (!ttak_arena_generation_begin(&env, &generation, gen)) {
            fprintf(stderr, "[arena] failed to allocate generation %u\n", gen);
            break;
        }

        printf("[arena] generation %u started (capacity=%zu, chunk=%zu)\n",
               gen, generation.capacity, env.config.chunk_bytes);

        fill_context_t context = {.generation_id = gen};
        size_t processed = ttak_arena_generation_for_each(&env, &generation, 0, fill_chunk, &context);
        if (processed == 0) {
            fprintf(stderr, "  unable to carve chunks for generation %u\n", gen);
        }

        printf("  used=%zu / %zu bytes (%zu chunks)\n",
               generation.used, generation.capacity, processed);
        void *retired_ptr = generation.base;
        bool released = ttak_arena_generation_retire(&env, &generation);
        (void)released;
        ttak_arena_env_rotate(&env);

        const char *status = "pending (still tracked)";
        if (retired_ptr) {
            ttak_mem_node_t *still_tracked = ttak_mem_tree_find_node(&env.gc->tree, retired_ptr);
            if (!still_tracked) {
                status = "flushed";
            }
        }

        printf("  cleanup status: %s\n", status);
    }

    ttak_arena_env_destroy(&env);
    return 0;
}
