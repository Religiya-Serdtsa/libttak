/**
 * @file abstract.c
 * @brief Pointer-stable logical memory with fragmented backing and scoped maps.
 */

#include <ttak/mem/abstract.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    static inline void *ttak_abstract_fragment_alloc(size_t size) {
        return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
    }
    static inline void ttak_abstract_fragment_free(void *addr, size_t size) {
        (void)size;
        if (addr != NULL) {
            VirtualFree(addr, 0, MEM_RELEASE);
        }
    }
#elif __linux__
    #include <sys/mman.h>
    static inline void *ttak_abstract_fragment_alloc(size_t size) {
        void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (p == MAP_FAILED) ? NULL : p;
    }
    static inline void ttak_abstract_fragment_free(void *addr, size_t size) {
        if (addr != NULL && size > 0U) {
            munmap(addr, size);
        }
    }
#else
    static inline void *ttak_abstract_fragment_alloc(size_t size) {
        return size == 0U ? NULL : calloc(1U, size);
    }
    static inline void ttak_abstract_fragment_free(void *addr, size_t size) {
        (void)size;
        free(addr);
    }
#endif

#define TTAK_ABSTRACT_FRAGMENT_BYTES_MAX (64U * 1024U)
#define TTAK_ABSTRACT_FRAGMENT_BYTES_MIN 8U
#define TTAK_ABSTRACT_EXTENT_SLOTS_MIN 8U
#define TTAK_ABSTRACT_MAP_FLAG_STAGING 0x1U

typedef struct ttak_abstract_extent {
    uint8_t *data;
    size_t size;
} ttak_abstract_extent_t;

struct ttak_abstract_mem {
    pthread_rwlock_t rwlock;
    ttak_abstract_extent_t *extents;
    size_t extent_count;
    size_t extent_capacity;
    size_t logical_size;
    size_t physical_size;
    uint64_t relocation_count;
};

static bool ttak_abstract_grow_extent_table(ttak_abstract_mem_t *mem,
                                            size_t target_count)
{
    if (mem == NULL) {
        return false;
    }
    if (target_count <= mem->extent_capacity) {
        return true;
    }

    size_t new_capacity = mem->extent_capacity;
    if (new_capacity < TTAK_ABSTRACT_EXTENT_SLOTS_MIN) {
        new_capacity = TTAK_ABSTRACT_EXTENT_SLOTS_MIN;
    }
    while (new_capacity < target_count) {
        if (new_capacity > SIZE_MAX / 2U) {
            return false;
        }
        new_capacity *= 2U;
    }

    ttak_abstract_extent_t *resized =
        (ttak_abstract_extent_t *)realloc(mem->extents,
                                          new_capacity * sizeof(*resized));
    if (resized == NULL) {
        return false;
    }
    for (size_t idx = mem->extent_capacity; idx < new_capacity; ++idx) {
        resized[idx].data = NULL;
        resized[idx].size = 0U;
    }
    mem->extents = resized;
    mem->extent_capacity = new_capacity;
    return true;
}

static bool ttak_abstract_push_extent_locked(ttak_abstract_mem_t *mem,
                                             uint8_t *data, size_t size)
{
    if (mem == NULL || data == NULL || size == 0U) {
        return false;
    }
    if (!ttak_abstract_grow_extent_table(mem, mem->extent_count + 1U)) {
        return false;
    }
    mem->extents[mem->extent_count].data = data;
    mem->extents[mem->extent_count].size = size;
    mem->extent_count++;
    mem->physical_size += size;
    mem->relocation_count++;
    return true;
}

static size_t ttak_abstract_pick_fragment_size(size_t remaining)
{
    size_t fragment = TTAK_ABSTRACT_FRAGMENT_BYTES_MAX;
    if (fragment > remaining) {
        fragment = remaining;
    }
    if (fragment < TTAK_ABSTRACT_FRAGMENT_BYTES_MIN) {
        fragment = TTAK_ABSTRACT_FRAGMENT_BYTES_MIN;
    }
    return fragment;
}

static bool ttak_abstract_reserve_locked(ttak_abstract_mem_t *mem,
                                         size_t target_size)
{
    if (mem == NULL) {
        return false;
    }
    if (target_size <= mem->physical_size) {
        return true;
    }

    size_t remaining = target_size - mem->physical_size;
    while (remaining > 0U) {
        size_t desired = ttak_abstract_pick_fragment_size(remaining);
        size_t fragment_size = desired;
        uint8_t *fragment = NULL;

        while (fragment_size >= TTAK_ABSTRACT_FRAGMENT_BYTES_MIN) {
            fragment = (uint8_t *)ttak_abstract_fragment_alloc(fragment_size);
            if (fragment != NULL) {
                memset(fragment, 0, fragment_size);
                break;
            }
            if (fragment_size == TTAK_ABSTRACT_FRAGMENT_BYTES_MIN) {
                break;
            }
            fragment_size /= 2U;
            if (fragment_size < TTAK_ABSTRACT_FRAGMENT_BYTES_MIN) {
                fragment_size = TTAK_ABSTRACT_FRAGMENT_BYTES_MIN;
            }
        }

        if (fragment == NULL) {
            return false;
        }
        if (!ttak_abstract_push_extent_locked(mem, fragment, fragment_size)) {
            ttak_abstract_fragment_free(fragment, fragment_size);
            return false;
        }
        if (fragment_size >= remaining) {
            remaining = 0U;
        } else {
            remaining -= fragment_size;
        }
    }

    return true;
}

static size_t ttak_abstract_find_extent_locked(const ttak_abstract_mem_t *mem,
                                               size_t offset,
                                               size_t *extent_offset)
{
    size_t base = 0U;
    if (extent_offset != NULL) {
        *extent_offset = 0U;
    }
    if (mem == NULL) {
        return 0U;
    }

    for (size_t idx = 0U; idx < mem->extent_count; ++idx) {
        size_t extent_size = mem->extents[idx].size;
        if (offset < base + extent_size) {
            if (extent_offset != NULL) {
                *extent_offset = offset - base;
            }
            return idx;
        }
        base += extent_size;
    }

    if (extent_offset != NULL && mem->extent_count > 0U &&
        offset == mem->physical_size) {
        *extent_offset = mem->extents[mem->extent_count - 1U].size;
        return mem->extent_count - 1U;
    }
    return mem->extent_count;
}

static void ttak_abstract_zero_range_locked(ttak_abstract_mem_t *mem,
                                            size_t offset, size_t len)
{
    if (mem == NULL || len == 0U) {
        return;
    }

    size_t remaining = len;
    size_t cursor = offset;
    while (remaining > 0U) {
        size_t extent_offset = 0U;
        size_t extent_index =
            ttak_abstract_find_extent_locked(mem, cursor, &extent_offset);
        if (extent_index >= mem->extent_count) {
            break;
        }
        size_t chunk = mem->extents[extent_index].size - extent_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }
        memset(mem->extents[extent_index].data + extent_offset, 0, chunk);
        cursor += chunk;
        remaining -= chunk;
    }
}

static void ttak_abstract_copy_out_locked(const ttak_abstract_mem_t *mem,
                                          size_t offset, void *out,
                                          size_t len)
{
    if (mem == NULL || out == NULL || len == 0U) {
        return;
    }

    uint8_t *dst = (uint8_t *)out;
    size_t remaining = len;
    size_t cursor = offset;
    while (remaining > 0U) {
        size_t extent_offset = 0U;
        size_t extent_index =
            ttak_abstract_find_extent_locked(mem, cursor, &extent_offset);
        if (extent_index >= mem->extent_count) {
            break;
        }
        size_t chunk = mem->extents[extent_index].size - extent_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }
        memcpy(dst, mem->extents[extent_index].data + extent_offset, chunk);
        dst += chunk;
        cursor += chunk;
        remaining -= chunk;
    }
}

static void ttak_abstract_copy_in_locked(ttak_abstract_mem_t *mem,
                                         size_t offset, const void *src,
                                         size_t len)
{
    if (mem == NULL || src == NULL || len == 0U) {
        return;
    }

    const uint8_t *input = (const uint8_t *)src;
    size_t remaining = len;
    size_t cursor = offset;
    while (remaining > 0U) {
        size_t extent_offset = 0U;
        size_t extent_index =
            ttak_abstract_find_extent_locked(mem, cursor, &extent_offset);
        if (extent_index >= mem->extent_count) {
            break;
        }
        size_t chunk = mem->extents[extent_index].size - extent_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }
        memcpy(mem->extents[extent_index].data + extent_offset, input, chunk);
        input += chunk;
        cursor += chunk;
        remaining -= chunk;
    }
}

static void ttak_abstract_release_all_locked(ttak_abstract_mem_t *mem)
{
    if (mem == NULL) {
        return;
    }
    for (size_t idx = 0U; idx < mem->extent_count; ++idx) {
        if (mem->extents[idx].data != NULL) {
            memset(mem->extents[idx].data, 0, mem->extents[idx].size);
            ttak_abstract_fragment_free(mem->extents[idx].data,
                                        mem->extents[idx].size);
            mem->extents[idx].data = NULL;
            mem->extents[idx].size = 0U;
        }
    }
    mem->extent_count = 0U;
    mem->physical_size = 0U;
}

static void ttak_abstract_trim_tail_locked(ttak_abstract_mem_t *mem)
{
    if (mem == NULL) {
        return;
    }

    size_t live_size = mem->logical_size;
    size_t prefix = 0U;
    size_t keep = 0U;
    for (; keep < mem->extent_count; ++keep) {
        size_t next = prefix + mem->extents[keep].size;
        if (next >= live_size) {
            keep++;
            break;
        }
        prefix = next;
    }

    for (size_t idx = keep; idx < mem->extent_count; ++idx) {
        if (mem->extents[idx].data != NULL) {
            memset(mem->extents[idx].data, 0, mem->extents[idx].size);
            mem->physical_size -= mem->extents[idx].size;
            ttak_abstract_fragment_free(mem->extents[idx].data,
                                        mem->extents[idx].size);
            mem->extents[idx].data = NULL;
            mem->extents[idx].size = 0U;
        }
    }
    mem->extent_count = keep;
    if (mem->logical_size == 0U) {
        mem->physical_size = 0U;
    }
}

ttak_abstract_mem_t *ttak_abstract_alloc(size_t size)
{
    ttak_abstract_mem_t *mem =
        (ttak_abstract_mem_t *)calloc(1U, sizeof(*mem));
    if (mem == NULL) {
        return NULL;
    }

    if (pthread_rwlock_init(&mem->rwlock, NULL) != 0) {
        free(mem);
        return NULL;
    }

    if (!ttak_abstract_reserve_locked(mem, size)) {
        ttak_abstract_free(mem);
        return NULL;
    }
    mem->logical_size = size;
    return mem;
}

void ttak_abstract_free(ttak_abstract_mem_t *mem)
{
    if (mem == NULL) {
        return;
    }

    pthread_rwlock_wrlock(&mem->rwlock);
    ttak_abstract_release_all_locked(mem);
    free(mem->extents);
    mem->extents = NULL;
    mem->extent_capacity = 0U;
    mem->logical_size = 0U;
    pthread_rwlock_unlock(&mem->rwlock);

    pthread_rwlock_destroy(&mem->rwlock);
    free(mem);
}

size_t ttak_abstract_size(const ttak_abstract_mem_t *mem)
{
    if (mem == NULL) {
        return 0U;
    }

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mem->rwlock);
    size_t size = mem->logical_size;
    pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
    return size;
}

int ttak_abstract_read(const ttak_abstract_mem_t *mem, size_t offset, void *out,
                       size_t len)
{
    if (mem == NULL || (out == NULL && len > 0U)) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mem->rwlock);
    if (offset > mem->logical_size || len > (mem->logical_size - offset)) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
        return -1;
    }
    ttak_abstract_copy_out_locked(mem, offset, out, len);
    pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
    return 0;
}

int ttak_abstract_write(ttak_abstract_mem_t *mem, size_t offset,
                        const void *src, size_t len)
{
    if (mem == NULL || (src == NULL && len > 0U)) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }

    pthread_rwlock_wrlock(&mem->rwlock);
    if (offset > mem->logical_size || len > (mem->logical_size - offset)) {
        pthread_rwlock_unlock(&mem->rwlock);
        return -1;
    }
    ttak_abstract_copy_in_locked(mem, offset, src, len);
    pthread_rwlock_unlock(&mem->rwlock);
    return 0;
}

int ttak_abstract_resize(ttak_abstract_mem_t *mem, size_t new_size)
{
    if (mem == NULL) {
        return -1;
    }

    pthread_rwlock_wrlock(&mem->rwlock);
    size_t old_size = mem->logical_size;
    if (new_size > old_size && !ttak_abstract_reserve_locked(mem, new_size)) {
        pthread_rwlock_unlock(&mem->rwlock);
        return -1;
    }

    if (new_size > old_size) {
        ttak_abstract_zero_range_locked(mem, old_size, new_size - old_size);
    } else if (new_size < old_size) {
        ttak_abstract_zero_range_locked(mem, new_size, old_size - new_size);
        mem->logical_size = new_size;
        ttak_abstract_trim_tail_locked(mem);
    }

    mem->logical_size = new_size;
    pthread_rwlock_unlock(&mem->rwlock);
    return 0;
}

int ttak_abstract_compact(ttak_abstract_mem_t *mem)
{
    if (mem == NULL) {
        return -1;
    }

    pthread_rwlock_wrlock(&mem->rwlock);
    ttak_abstract_trim_tail_locked(mem);
    pthread_rwlock_unlock(&mem->rwlock);
    return 0;
}

int ttak_abstract_map(ttak_abstract_mem_t *mem, size_t offset, size_t len,
                      ttak_abstract_access_t access, ttak_abstract_map_t *map)
{
    if (mem == NULL || map == NULL) {
        return -1;
    }

    memset(map, 0, sizeof(*map));
    if (access == TTAK_ABSTRACT_ACCESS_WRITE) {
        pthread_rwlock_wrlock(&mem->rwlock);
    } else if (access == TTAK_ABSTRACT_ACCESS_READ) {
        pthread_rwlock_rdlock(&mem->rwlock);
    } else {
        return -1;
    }

    if (offset > mem->logical_size || len > (mem->logical_size - offset)) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
        return -1;
    }

    map->size = len;
    map->offset = offset;
    map->mem = mem;
    map->access = access;

    if (len == 0U) {
        return 0;
    }

    size_t extent_offset = 0U;
    size_t extent_index =
        ttak_abstract_find_extent_locked(mem, offset, &extent_offset);
    if (extent_index < mem->extent_count &&
        len <= mem->extents[extent_index].size - extent_offset) {
        map->data = mem->extents[extent_index].data + extent_offset;
        return 0;
    }

    void *scratch = malloc(len);
    if (scratch == NULL) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
        memset(map, 0, sizeof(*map));
        return -1;
    }

    ttak_abstract_copy_out_locked(mem, offset, scratch, len);
    map->data = scratch;
    map->opaque = scratch;
    map->flags = TTAK_ABSTRACT_MAP_FLAG_STAGING;
    return 0;
}

void ttak_abstract_unmap(ttak_abstract_map_t *map)
{
    if (map == NULL || map->mem == NULL) {
        return;
    }

    ttak_abstract_mem_t *mem = (ttak_abstract_mem_t *)map->mem;
    if ((map->flags & TTAK_ABSTRACT_MAP_FLAG_STAGING) != 0U &&
        map->opaque != NULL) {
        if (map->access == TTAK_ABSTRACT_ACCESS_WRITE && map->size > 0U) {
            ttak_abstract_copy_in_locked(mem, map->offset, map->opaque,
                                         map->size);
        }
        memset(map->opaque, 0, map->size);
        free(map->opaque);
    }

    pthread_rwlock_unlock((pthread_rwlock_t *)&mem->rwlock);
    memset(map, 0, sizeof(*map));
}
