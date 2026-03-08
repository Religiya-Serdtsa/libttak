/**
 * @file fastpath.h
 * @brief Architecture-aware memory primitives for TinyCC builds.
 *
 * Exposes streaming zero/copy helpers that fall back to plain C when the
 * target ISA is not covered or when a non-TinyCC compiler is driving the
 * build. The helpers retain memcpy/memset semantics (non-overlapping copy,
 * byte-exact zeroing) but are written so libttak can keep high throughput
 * even when TinyCC emits -O0 code.
 */

#ifndef TTAK_MEM_FASTPATH_H
#define TTAK_MEM_FASTPATH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__TINYC__)
void ttak_mem_stream_zero(void *dst, size_t len);
void ttak_mem_stream_copy(void *dst, const void *src, size_t len);
#else
static inline void ttak_mem_stream_zero(void *dst, size_t len) {
    if (len) memset(dst, 0, len);
}

static inline void ttak_mem_stream_copy(void *dst, const void *src, size_t len) {
    if (len) memcpy(dst, src, len);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* TTAK_MEM_FASTPATH_H */
