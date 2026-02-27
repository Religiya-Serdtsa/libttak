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
#  define TTAK_FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#  define TTAK_FORCE_INLINE __forceinline
#else
#  define TTAK_FORCE_INLINE inline
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

#endif /* TTAK_COMPILER_H */
