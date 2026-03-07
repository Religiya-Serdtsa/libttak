/**
 * @file ttak_compiler.h
 * @brief Compiler-specific macros and compatibility layer.
 */

#ifndef TTAK_COMPILER_H
#define TTAK_COMPILER_H

#if defined(__GNUC__) || defined(__clang__)
#  define TTAK_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define TTAK_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define TTAK_LIKELY(x)   (x)
#  define TTAK_UNLIKELY(x) (x)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define TTAK_FORCE_INLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#  define TTAK_FORCE_INLINE static __forceinline
#else
#  define TTAK_FORCE_INLINE static inline
#endif

#if !defined(__GNUC__) && !defined(__clang__)
static inline int __ttak_ctzll(unsigned long long v) {
    int r = 0;
    if (!v) return 64;
    while (!(v & 1)) { v >>= 1; r++; }
    return r;
}
static inline int __ttak_clzll(unsigned long long v) {
    int r = 0;
    if (!v) return 64;
    for (int i = 63; i >= 0; i--) {
        if (v & (1ULL << i)) break;
        r++;
    }
    return r;
}
#define __builtin_ctzll(x) __ttak_ctzll(x)
#define __builtin_clzll(x) __ttak_clzll(x)
#endif

#if defined(__TINYC__) && defined(__x86_64__)
/**
 * Optimized atomic operations for TinyC Compiler on x86_64.
 * 
 * Provides inline assembly implementations to bypass standard library 
 * overhead for common atomic patterns.
 */
#define TTAK_FAST_ATOMIC_ADD_U64(ptr, val) \
    __extension__ ({ \
        uint64_t __v = (val); \
        __asm__ volatile ("lock; xaddq %0, %1" : "+r"(__v), "+m"(*(volatile uint64_t *)(ptr)) : : "memory", "cc"); \
        __v; \
    })

#define TTAK_FAST_ATOMIC_ADD_U32(ptr, val) \
    __extension__ ({ \
        uint32_t __v = (val); \
        __asm__ volatile ("lock; xaddl %0, %1" : "+r"(__v), "+m"(*(volatile uint32_t *)(ptr)) : : "memory", "cc"); \
        __v; \
    })

#define TTAK_FAST_ATOMIC_LOAD_U64(ptr) \
    __extension__ ({ \
        uint64_t __v; \
        __asm__ volatile ("movq %1, %0" : "=r"(__v) : "m"(*(volatile uint64_t *)(ptr)) : "memory"); \
        __v; \
    })

#define TTAK_FAST_ATOMIC_STORE_U32(ptr, val) \
    __extension__ ({ \
        uint32_t __v = (val); \
        __asm__ volatile ("movl %1, %0" : "=m"(*(volatile uint32_t *)(ptr)) : "r"(__v) : "memory"); \
    })

#define TTAK_FAST_ATOMIC_STORE_BOOL(ptr, val) \
    __extension__ ({ \
        uint8_t __v = (val); \
        __asm__ volatile ("movb %1, %0" : "=m"(*(volatile uint8_t *)(ptr)) : "r"(__v) : "memory"); \
    })

#define TTAK_FAST_ATOMIC_XCHG_U64(ptr, val) \
    __extension__ ({ \
        uint64_t __v = (val); \
        __asm__ volatile ("lock; xchgq %0, %1" : "+r"(__v), "+m"(*(volatile uint64_t *)(ptr)) : : "memory"); \
        __v; \
    })

#else
#include <stdatomic.h>
#define TTAK_FAST_ATOMIC_ADD_U64(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)
#define TTAK_FAST_ATOMIC_ADD_U32(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)
#define TTAK_FAST_ATOMIC_LOAD_U64(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#define TTAK_FAST_ATOMIC_STORE_U32(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELAXED)
#define TTAK_FAST_ATOMIC_STORE_BOOL(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELAXED)
#define TTAK_FAST_ATOMIC_XCHG_U64(ptr, val) __atomic_exchange_n((ptr), (val), __ATOMIC_RELAXED)
#endif

#endif /* TTAK_COMPILER_H */
