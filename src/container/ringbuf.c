#include <ttak/container/ringbuf.h>
#include <ttak/mem/mem.h>
#include <ttak/timing/timing.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief Creates ring buffer.
 */
ttak_ringbuf_t *ttak_ringbuf_create(size_t capacity, size_t item_size) {
    ttak_ringbuf_t *rb = malloc(sizeof(ttak_ringbuf_t));
    if (!rb) return NULL;
    
    // Using simple malloc for internal buffer to avoid lifecycle complexity inside ringbuf
    rb->buffer = malloc(capacity * item_size);
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }
    
    rb->capacity = capacity;
    rb->item_size = item_size;
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
    ttak_rwlock_init(&rb->lock);
    
    return rb;
}

/**
 * @brief Destroys ring buffer.
 */
void ttak_ringbuf_destroy(ttak_ringbuf_t *rb) {
    if (rb) {
        free(rb->buffer);
        ttak_rwlock_destroy(&rb->lock);
        free(rb);
    }
}

/**
 * @brief Pushes item (copy).
 */
bool ttak_ringbuf_push(ttak_ringbuf_t *rb, const void *item) {
    ttak_rwlock_wrlock(&rb->lock);
    if (rb->full) {
        ttak_rwlock_unlock(&rb->lock);
        return false;
    }
    
    char *dest = (char *)rb->buffer + (rb->head * rb->item_size);
    memcpy(dest, item, rb->item_size);
    
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->head == rb->tail) {
        rb->full = true;
    }
    
    ttak_rwlock_unlock(&rb->lock);
    return true;
}

/**
 * @brief Pops item (copy).
 */
bool ttak_ringbuf_pop(ttak_ringbuf_t *rb, void *out_item) {
    ttak_rwlock_wrlock(&rb->lock);
    if (ttak_ringbuf_is_empty(rb)) {
        ttak_rwlock_unlock(&rb->lock);
        return false;
    }
    
    char *src = (char *)rb->buffer + (rb->tail * rb->item_size);
    if (out_item) {
        memcpy(out_item, src, rb->item_size);
    }
    
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->full = false;
    
    ttak_rwlock_unlock(&rb->lock);
    return true;
}

bool ttak_ringbuf_is_empty(ttak_ringbuf_t *rb) {
    return (!rb->full && (rb->head == rb->tail));
}

bool ttak_ringbuf_is_full(ttak_ringbuf_t *rb) {
    return rb->full;
}

size_t ttak_ringbuf_count(ttak_ringbuf_t *rb) {
    if (rb->full) return rb->capacity;
    if (rb->head >= rb->tail) return rb->head - rb->tail;
    return rb->capacity + rb->head - rb->tail;
}