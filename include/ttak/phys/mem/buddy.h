#ifndef TTAK_PHYS_MEM_BUDDY_H
#define TTAK_PHYS_MEM_BUDDY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EMBEDDED
#define EMBEDDED 0
#endif

typedef enum ttak_priority {
    TTAK_PRIORITY_BEST_FIT,
    TTAK_PRIORITY_WORST_FIT,
    TTAK_PRIORITY_FIRST_FIT
} ttak_priority_t;

typedef struct ttak_mem_req {
    size_t size_bytes;
    ttak_priority_t priority;
    uint32_t owner_tag;
    uint32_t call_safety;
    uint32_t flags;
} ttak_mem_req_t;

void ttak_mem_buddy_init(void *pool_start, size_t pool_len, int embedded_mode);
void ttak_mem_buddy_set_pool(void *pool_start, size_t pool_len);
void *ttak_mem_buddy_alloc(const ttak_mem_req_t *req);
void ttak_mem_buddy_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_PHYS_MEM_BUDDY_H */
