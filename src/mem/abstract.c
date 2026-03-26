/**
 * @file abstract.c
 * @brief Pointer-stable abstract memory with relocatable VMA-backed regions.
 *
 * Design summary
 * --------------
 * This module implements a logical-memory handle that remains stable while its
 * backing region may be moved internally. The relocation pipeline is explicit:
 *
 *   1) allocate a new disjoint region
 *   2) memcpy preserved bytes old -> new
 *   3) atomically publish the new region pointer/capacity
 *   4) zero-fill old region
 *   5) release old region
 *
 * The module intentionally avoids epoch/hazard-pointer schemes. Instead, it uses
 * a transparent synchronization model:
 * - readers/writers use pthread rwlock
 * - relocation is performed while holding write lock
 * - publication still uses atomic stores to keep transition semantics explicit
 */

#include <ttak/mem/abstract.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    static inline void *_ttak_abstract_map(size_t size) {
        return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    static inline void _ttak_abstract_unmap(void *addr, size_t size) {
        (void)size;
        if (!addr) return;
        VirtualFree(addr, 0, MEM_RELEASE);
    }
#else
    #include <sys/mman.h>
    static inline void *_ttak_abstract_map(size_t size) {
        void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (p == MAP_FAILED) ? NULL : p;
    }
    static inline void _ttak_abstract_unmap(void *addr, size_t size) {
        if (!addr || size == 0) return;
        munmap(addr, size);
    }
#endif

#define TTAK_ABSTRACT_ALIGNMENT 64u
#define TTAK_ABSTRACT_MIN_CAPACITY 64u
#define TTAK_ABSTRACT_MAX_DISJOINT_RETRIES 8u

struct ttak_abstract_mem {
    pthread_rwlock_t rwlock;
    _Atomic uintptr_t region_base;
    _Atomic size_t capacity;
    size_t logical_size;
    uint64_t relocation_count;
};

static inline size_t ttak_abstract_align_up(size_t n) {
    if (n < TTAK_ABSTRACT_MIN_CAPACITY) n = TTAK_ABSTRACT_MIN_CAPACITY;
    size_t r = n % TTAK_ABSTRACT_ALIGNMENT;
    if (r == 0) return n;
    size_t delta = TTAK_ABSTRACT_ALIGNMENT - r;
    if (n > SIZE_MAX - delta) return 0;
    return n + delta;
}

static inline bool ttak_abstract_ranges_overlap(uintptr_t a_base, size_t a_len, uintptr_t b_base, size_t b_len) {
    if (a_len == 0 || b_len == 0) return false;
    uintptr_t a_end = a_base + a_len;
    uintptr_t b_end = b_base + b_len;
    return (a_base < b_end) && (b_base < a_end);
}

static void *ttak_abstract_alloc_disjoint(size_t capacity, uintptr_t old_base, size_t old_capacity) {
    for (unsigned i = 0; i < TTAK_ABSTRACT_MAX_DISJOINT_RETRIES; ++i) {
        void *candidate = _ttak_abstract_map(capacity);
        if (!candidate) return NULL;

        uintptr_t cbase = (uintptr_t)candidate;
        if (!ttak_abstract_ranges_overlap(cbase, capacity, old_base, old_capacity)) {
            return candidate;
        }
        _ttak_abstract_unmap(candidate, capacity);
    }
    return NULL;
}

static int ttak_abstract_relocate_locked(ttak_abstract_mem_t *mem, size_t new_capacity, size_t new_logical_size) {
    uintptr_t old_base = atomic_load_explicit(&mem->region_base, memory_order_acquire);
    size_t old_capacity = atomic_load_explicit(&mem->capacity, memory_order_acquire);
    uint8_t *old_region = (uint8_t *)old_base;
    uint8_t *new_region = NULL;

    if (new_capacity == 0) {
        new_capacity = TTAK_ABSTRACT_MIN_CAPACITY;
    }
    if (new_logical_size > new_capacity) {
        return -1;
    }

    new_region = (uint8_t *)ttak_abstract_alloc_disjoint(new_capacity, old_base, old_capacity);
    if (!new_region) {
        return -1;
    }

    size_t preserved = mem->logical_size < new_logical_size ? mem->logical_size : new_logical_size;
    if (preserved > 0 && old_region) {
        memcpy(new_region, old_region, preserved);
    }
    if (new_logical_size > preserved) {
        memset(new_region + preserved, 0, new_logical_size - preserved);
    }

    atomic_store_explicit(&mem->region_base, (uintptr_t)new_region, memory_order_release);
    atomic_store_explicit(&mem->capacity, new_capacity, memory_order_release);
    mem->logical_size = new_logical_size;
    mem->relocation_count++;

    if (old_region && old_capacity > 0) {
        memset(old_region, 0, old_capacity);
        _ttak_abstract_unmap(old_region, old_capacity);
    }

    return 0;
}

ttak_abstract_mem_t *ttak_abstract_alloc(size_t size) {
    ttak_abstract_mem_t *mem = NULL;
    size_t capacity = ttak_abstract_align_up(size);
    if (size > 0 && capacity == 0) return NULL;

    mem = (ttak_abstract_mem_t *)calloc(1, sizeof(*mem));
    if (!mem) return NULL;

    if (pthread_rwlock_init(&mem->rwlock, NULL) != 0) {
        free(mem);
        return NULL;
    }

    void *region = _ttak_abstract_map(capacity);
    if (!region) {
        pthread_rwlock_destroy(&mem->rwlock);
        free(mem);
        return NULL;
    }

    memset(region, 0, capacity);
    atomic_store_explicit(&mem->region_base, (uintptr_t)region, memory_order_release);
    atomic_store_explicit(&mem->capacity, capacity, memory_order_release);
    mem->logical_size = size;
    mem->relocation_count = 0;
    return mem;
}

void ttak_abstract_free(ttak_abstract_mem_t *mem) {
    if (!mem) return;

    pthread_rwlock_wrlock(&mem->rwlock);
    uintptr_t base = atomic_load_explicit(&mem->region_base, memory_order_acquire);
    size_t cap = atomic_load_explicit(&mem->capacity, memory_order_acquire);
    if (base && cap) {
        memset((void *)base, 0, cap);
        _ttak_abstract_unmap((void *)base, cap);
    }
    atomic_store_explicit(&mem->region_base, (uintptr_t)NULL, memory_order_release);
    atomic_store_explicit(&mem->capacity, 0, memory_order_release);
    mem->logical_size = 0;
    pthread_rwlock_unlock(&mem->rwlock);

    pthread_rwlock_destroy(&mem->rwlock);
    free(mem);
}

size_t ttak_abstract_size(const ttak_abstract_mem_t *mem) {
    if (!mem) return 0;
    pthread_rwlock_rdlock((pthread_rwlock_t *)&mem->rwlock);
    size_t n = mem->logical_size;
    pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
    return n;
}

int ttak_abstract_read(const ttak_abstract_mem_t *mem, size_t offset, void *out, size_t len) {
    if (!mem || (!out && len > 0)) return -1;
    if (len == 0) return 0;

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mem->rwlock);
    if (offset > mem->logical_size || len > (mem->logical_size - offset)) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
        return -1;
    }
    uintptr_t base = atomic_load_explicit((atomic_uintptr_t *)&mem->region_base, memory_order_acquire);
    memcpy(out, (const void *)(base + offset), len);
    pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
    return 0;
}

int ttak_abstract_write(ttak_abstract_mem_t *mem, size_t offset, const void *src, size_t len) {
    if (!mem || (!src && len > 0)) return -1;
    if (len == 0) return 0;

    pthread_rwlock_wrlock(&mem->rwlock);
    if (offset > mem->logical_size || len > (mem->logical_size - offset)) {
        pthread_rwlock_unlock(&mem->rwlock);
        return -1;
    }

    uintptr_t base = atomic_load_explicit(&mem->region_base, memory_order_acquire);
    memcpy((void *)(base + offset), src, len);

    size_t cap = atomic_load_explicit(&mem->capacity, memory_order_acquire);
    if (cap > (mem->logical_size * 4) && mem->logical_size > 0) {
        size_t target = ttak_abstract_align_up(mem->logical_size * 2);
        if (target >= mem->logical_size && target < cap) {
            (void)ttak_abstract_relocate_locked(mem, target, mem->logical_size);
        }
    }

    pthread_rwlock_unlock(&mem->rwlock);
    return 0;
}

int ttak_abstract_resize(ttak_abstract_mem_t *mem, size_t new_size) {
    if (!mem) return -1;

    pthread_rwlock_wrlock(&mem->rwlock);
    size_t cap = atomic_load_explicit(&mem->capacity, memory_order_acquire);
    size_t old_size = mem->logical_size;

    if (new_size <= cap && new_size >= (cap / 2)) {
        if (new_size > old_size) {
            uintptr_t base = atomic_load_explicit(&mem->region_base, memory_order_acquire);
            memset((void *)(base + old_size), 0, new_size - old_size);
        }
        mem->logical_size = new_size;
        pthread_rwlock_unlock(&mem->rwlock);
        return 0;
    }

    size_t new_cap = ttak_abstract_align_up(new_size);
    if (new_size > 0 && new_cap == 0) {
        pthread_rwlock_unlock(&mem->rwlock);
        return -1;
    }

    int rc = ttak_abstract_relocate_locked(mem, new_cap, new_size);
    pthread_rwlock_unlock(&mem->rwlock);
    return rc;
}

int ttak_abstract_compact(ttak_abstract_mem_t *mem) {
    if (!mem) return -1;
    pthread_rwlock_wrlock(&mem->rwlock);
    size_t new_cap = ttak_abstract_align_up(mem->logical_size);
    if (new_cap == 0) {
        pthread_rwlock_unlock(&mem->rwlock);
        return -1;
    }
    int rc = ttak_abstract_relocate_locked(mem, new_cap, mem->logical_size);
    pthread_rwlock_unlock(&mem->rwlock);
    return rc;
}
