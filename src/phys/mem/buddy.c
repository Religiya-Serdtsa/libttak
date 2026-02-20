#include <ttak/phys/mem/buddy.h>

#include <ttak/mem/epoch.h>

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifndef EMBEDDED
#define EMBEDDED 0
#endif

#define TTAK_BUDDY_MIN_ORDER   6U   /* 64 bytes */
#define TTAK_BUDDY_MAX_ORDER   60U  /* 2^60 bytes (~1 EB) */

typedef struct ttak_buddy_block {
    _Alignas(64) struct ttak_buddy_block *next;
    uint32_t owner_tag;
    uint32_t call_safety;
    uint8_t order;
    uint8_t in_use;
} ttak_buddy_block_t;

typedef struct ttak_buddy_zone {
    ttak_buddy_block_t *free_lists[TTAK_BUDDY_MAX_ORDER + 1];
    uint8_t max_order;
    uint8_t embedded_mode;
    uint8_t anti_fragmentation;
    void *pool_start;
    size_t pool_len;
} ttak_buddy_zone_t;

static ttak_buddy_zone_t g_zone;
static atomic_flag g_zone_lock = ATOMIC_FLAG_INIT;

static inline size_t order_size(uint8_t order) {
    if (order > TTAK_BUDDY_MAX_ORDER) return 0;
    return (size_t)1 << order;
}

static inline uint8_t order_for_size(size_t bytes) {
    if (bytes == 0) return TTAK_BUDDY_MIN_ORDER;
    size_t need = bytes;
    if (need < order_size(TTAK_BUDDY_MIN_ORDER)) {
        need = order_size(TTAK_BUDDY_MIN_ORDER);
    }
    uint8_t order = TTAK_BUDDY_MIN_ORDER;
    while (order < TTAK_BUDDY_MAX_ORDER && order_size(order) < need) {
        order++;
    }
    return order;
}

static inline void buddy_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_zone_lock, memory_order_acquire)) {
    }
}

static inline void buddy_unlock(void) {
    atomic_flag_clear_explicit(&g_zone_lock, memory_order_release);
}

static inline size_t block_offset(ttak_buddy_block_t *block) {
    return (size_t)((unsigned char *)block - (unsigned char *)g_zone.pool_start);
}

static inline ttak_buddy_block_t *buddy_pair(ttak_buddy_block_t *block, uint8_t order) {
    size_t offset = block_offset(block);
    size_t pair_offset = offset ^ order_size(order);
    if (pair_offset >= g_zone.pool_len) {
        return NULL;
    }
    return (ttak_buddy_block_t *)((unsigned char *)g_zone.pool_start + pair_offset);
}

static void list_push(uint8_t order, ttak_buddy_block_t *block) {
    block->next = g_zone.free_lists[order];
    g_zone.free_lists[order] = block;
    block->order = order;
    block->in_use = 0;
}

static ttak_buddy_block_t *list_pop_head(uint8_t order) {
    ttak_buddy_block_t *block = g_zone.free_lists[order];
    if (block) {
        g_zone.free_lists[order] = block->next;
        block->next = NULL;
    }
    return block;
}

static void list_remove(uint8_t order, ttak_buddy_block_t *target) {
    ttak_buddy_block_t **cur = &g_zone.free_lists[order];
    while (*cur) {
        if (*cur == target) {
            *cur = target->next;
            target->next = NULL;
            return;
        }
        cur = &((*cur)->next);
    }
}

static void buddy_defragment(void) {
    for (uint8_t order = TTAK_BUDDY_MIN_ORDER; order < g_zone.max_order; ++order) {
        ttak_buddy_block_t *block = g_zone.free_lists[order];
        while (block) {
            ttak_buddy_block_t *next = block->next;
            ttak_buddy_block_t *pair = buddy_pair(block, order);
            if (pair && !pair->in_use && pair->order == order) {
                list_remove(order, block);
                list_remove(order, pair);
                ttak_buddy_block_t *merged = (block_offset(block) < block_offset(pair)) ? block : pair;
                list_push(order + 1, merged);
                block = g_zone.free_lists[order];
                continue;
            }
            block = next;
        }
    }
}

void ttak_mem_buddy_init(void *pool_start, size_t pool_len, int embedded_mode) {
    memset(&g_zone, 0, sizeof(g_zone));
    g_zone.pool_start = pool_start;
    g_zone.pool_len = pool_len;
    g_zone.embedded_mode = (uint8_t)(embedded_mode ? 1 : 0);
    g_zone.anti_fragmentation = (uint8_t)(embedded_mode ? 1 : 0);
    g_zone.max_order = TTAK_BUDDY_MAX_ORDER;

    /* Multi-Block Initializer: Divide non-power-of-two pool into largest possible blocks */
    unsigned char *ptr = (unsigned char *)pool_start;
    size_t remaining = pool_len;

    while (remaining >= order_size(TTAK_BUDDY_MIN_ORDER)) {
        uint8_t order = TTAK_BUDDY_MAX_ORDER;
        while (order > TTAK_BUDDY_MIN_ORDER) {
            size_t size = order_size(order);
            /* Alignment and size check */
            if (size <= remaining && ((size_t)(ptr - (unsigned char *)pool_start) % size == 0)) {
                break;
            }
            order--;
        }

        ttak_buddy_block_t *block = (ttak_buddy_block_t *)ptr;
        memset(block, 0, sizeof(ttak_buddy_block_t));
        block->order = order;
        block->in_use = 0;
        list_push(order, block);

        size_t used = order_size(order);
        ptr += used;
        remaining -= used;
    }
}

void ttak_mem_buddy_set_pool(void *pool_start, size_t pool_len) {
    buddy_lock();
    ttak_mem_buddy_init(pool_start, pool_len, g_zone.embedded_mode);
    buddy_unlock();
}

static ttak_buddy_block_t *select_block(uint8_t desired, ttak_priority_t priority) {
    if (priority == TTAK_PRIORITY_WORST_FIT) {
        for (int order = g_zone.max_order; order >= (int)desired; --order) {
            if (g_zone.free_lists[order]) {
                return list_pop_head((uint8_t)order);
            }
        }
        return NULL;
    }

    for (uint8_t order = desired; order <= g_zone.max_order; ++order) {
        if (g_zone.free_lists[order]) {
            if (priority == TTAK_PRIORITY_BEST_FIT) {
                /* find smallest block in this order */
                ttak_buddy_block_t **cur = &g_zone.free_lists[order];
                while ((*cur) && (*cur)->next) {
                    cur = &((*cur)->next);
                }
            }
            return list_pop_head(order);
        }
    }
    return NULL;
}

static void split_block(uint8_t target_order, ttak_buddy_block_t *block) {
    while (block->order > target_order) {
        uint8_t new_order = block->order - 1;
        size_t size = order_size(new_order);
        ttak_buddy_block_t *buddy =
            (ttak_buddy_block_t *)((unsigned char *)block + size);
        buddy->in_use = 0;
        buddy->order = new_order;
        buddy->next = NULL;
        list_push(new_order, buddy);
        block->order = new_order;
    }
    block->in_use = 1;
}

void *ttak_mem_buddy_alloc(const ttak_mem_req_t *req) {
    if (!req || !g_zone.pool_start) {
        return NULL;
    }

    /* Overflow check */
    if (req->size_bytes > (size_t)-1 - sizeof(ttak_buddy_block_t)) {
        fprintf(stderr, "[Buddy] ENOMEM(12): Requested size %zu overflows size_t.\n", req->size_bytes);
        return NULL;
    }

    size_t needed = req->size_bytes + sizeof(ttak_buddy_block_t);
    uint8_t order = order_for_size(needed);
    if (order > g_zone.max_order) {
        fprintf(stderr, "[Buddy] ENOMEM(12): Requested size %zu + header exceeds max_order %u. g_zone: pool_len=%zu\n",
                req->size_bytes, g_zone.max_order, g_zone.pool_len);
        return NULL;
    }

    buddy_lock();
    ttak_buddy_block_t *block = select_block(order, req->priority);
    if (!block && g_zone.embedded_mode) {
        buddy_defragment();
        block = select_block(order, req->priority);
    }
    if (!block) {
        fprintf(stderr, "[Buddy] ENOMEM(12): Allocation failed for needed size %zu (order %u). Current pool state: pool_len=%zu, max_order=%u\n",
                needed, order, g_zone.pool_len, g_zone.max_order);
        buddy_unlock();
        return NULL;
    }
    split_block(order, block);
    block->owner_tag = req->owner_tag;
    block->call_safety = req->call_safety;
    buddy_unlock();
    return (void *)(block + 1);
}

static void ttak_buddy_cleanup(void *ptr) {
    if (!ptr) return;
    ttak_buddy_block_t *block = (ttak_buddy_block_t *)ptr;
    buddy_lock();
    block->in_use = 0;

    while (block->order < g_zone.max_order) {
        ttak_buddy_block_t *pair = buddy_pair(block, block->order);
        if (!pair || pair->in_use || pair->order != block->order) break;
        list_remove(pair->order, pair);
        block = (block_offset(block) < block_offset(pair)) ? block : pair;
        block->order++;
    }
    list_push(block->order, block);
    buddy_unlock();
}

void ttak_mem_buddy_free(void *ptr) {
    if (!ptr) return;
    ttak_buddy_block_t *block = ((ttak_buddy_block_t *)ptr) - 1;
    block->owner_tag = 0;
    block->call_safety = 0;
    ttak_epoch_retire(block, ttak_buddy_cleanup);
}
