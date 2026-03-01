/**
 * @file ttak_align.h
 * @brief Alignment utilities and compatibility helpers.
 */

#ifndef TTAK_ALIGN_H
#define TTAK_ALIGN_H

#include <stddef.h>

/**
 * @brief Alias for the widest alignment supported by the compiler.
 *
 * TinyCC does not ship a definition for max_align_t, so we provide a struct
 * that matches the requirements for the other supported compilers.
 */
#if defined(__TINYC__)
typedef struct {
    long long   __ttak_max_align_ll;
    long double __ttak_max_align_ld;
} ttak_max_align_t;
#else
typedef max_align_t ttak_max_align_t;
#endif

#endif /* TTAK_ALIGN_H */
