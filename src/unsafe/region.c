#include <ttak/unsafe/region.h>
#include <string.h>

static bool tags_match(const char *a, const char *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static bool region_can_accept(const ttak_unsafe_region_t *region) {
    if (!region) return false;
    if (region->pin_count != 0) return false;
    if (region->ptr != NULL || region->size != 0) return false;
    return true;
}

void ttak_unsafe_region_init(ttak_unsafe_region_t *region, uint32_t ctx_id, const char *allocator_tag) {
    if (!region) return;
    region->ptr = NULL;
    region->size = 0;
    region->capacity = 0;
    region->pin_count = 0;
    region->ctx_id = ctx_id;
    region->allocator_tag = allocator_tag ? allocator_tag : __TTAK_REGION_CANONICAL_ALLOC__;
}

void ttak_unsafe_region_reset(ttak_unsafe_region_t *region) {
    if (!region) return;
    region->ptr = NULL;
    region->size = 0;
    region->capacity = 0;
    region->pin_count = 0;
    region->ctx_id = __TTAK_REGION_CANONICAL_CTX__;
    region->allocator_tag = __TTAK_REGION_CANONICAL_ALLOC__;
}

bool ttak_unsafe_region_is_empty(const ttak_unsafe_region_t *region) {
    if (!region) return true;
    return region->ptr == NULL && region->size == 0 && region->capacity == 0;
}

bool ttak_unsafe_region_pin(ttak_unsafe_region_t *region) {
    if (!region) return false;
    if (region->pin_count == UINT32_MAX) return false;
    region->pin_count++;
    return true;
}

bool ttak_unsafe_region_unpin(ttak_unsafe_region_t *region) {
    if (!region) return false;
    if (region->pin_count == 0) return false;
    region->pin_count--;
    return true;
}

bool ttak_unsafe_region_move(ttak_unsafe_region_t *dst, ttak_unsafe_region_t *src) {
    if (!dst || !src) return false;
    if (dst == src) return true;
    if (!region_can_accept(dst)) return false;
    if (src->pin_count != 0) return false;
    if (dst->ctx_id != src->ctx_id) return false;
    if (!tags_match(dst->allocator_tag, src->allocator_tag)) return false;
    dst->ptr = src->ptr;
    dst->size = src->size;
    dst->capacity = src->capacity;
    dst->allocator_tag = src->allocator_tag;
    ttak_unsafe_region_reset(src);
    return true;
}

bool ttak_unsafe_region_move_cross_ctx(ttak_unsafe_region_t *dst, ttak_unsafe_region_t *src,
                                       uint32_t new_ctx_id, const char *new_allocator_tag) {
    if (!dst || !src) return false;
    if (!region_can_accept(dst)) return false;
    if (src->pin_count != 0) return false;
    dst->ptr = src->ptr;
    dst->size = src->size;
    dst->capacity = src->capacity;
    dst->allocator_tag = new_allocator_tag ? new_allocator_tag : src->allocator_tag;
    dst->ctx_id = new_ctx_id;
    ttak_unsafe_region_reset(src);
    return true;
}

bool ttak_unsafe_region_adopt(ttak_unsafe_region_t *dst, void *ptr, size_t size, size_t capacity,
                              const char *allocator_tag, uint32_t ctx_id) {
    if (!dst) return false;
    if (!region_can_accept(dst)) return false;
    if (ptr == NULL && (size != 0 || capacity != 0)) return false;
    if (size > capacity) return false;
    dst->ptr = ptr;
    dst->size = size;
    dst->capacity = capacity;
    dst->ctx_id = ctx_id;
    dst->allocator_tag = allocator_tag ? allocator_tag : __TTAK_REGION_CANONICAL_ALLOC__;
    return true;
}

bool ttak_unsafe_region_steal(ttak_unsafe_region_t *dst, ttak_unsafe_region_t *src) {
    if (!dst || !src) return false;
    if (src->pin_count != 0 || dst->pin_count != 0) return false;
    dst->ptr = src->ptr;
    dst->size = src->size;
    dst->capacity = src->capacity;
    dst->ctx_id = src->ctx_id;
    dst->allocator_tag = src->allocator_tag;
    ttak_unsafe_region_reset(src);
    return true;
}
