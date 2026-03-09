/**
 * @file fastpath.c
 * @brief Compiler/arch-specific fast memory allocation hot path.
 *
 * Contains inline-assembly optimised allocation sequences for TinyCC on
 * x86-64.  The generic C fallback is used on all other targets.
 * @warning This file contains platform-specific inline assembly.
 *          Do not modify the assembly blocks without testing all targets.
 */

#include <ttak/mem/fastpath.h>

#if defined(__TINYC__)

static inline void ttak_mem_zero_fallback(uint8_t *dst, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        dst[i] = 0;
    }
}

static inline void ttak_mem_copy_fallback(uint8_t *dst, const uint8_t *src, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
}

void ttak_mem_stream_zero(void *dst, size_t len) {
    if (!len) return;
    uint8_t *ptr = (uint8_t *)dst;
#if defined(__x86_64__)
    size_t qwords = len >> 3;
    if (qwords) {
        __asm__ volatile(
            "cld\n"
            "xor %%rax, %%rax\n"
            "rep stosq"
            : "+D"(ptr), "+c"(qwords)
            :
            : "rax", "memory");
    }
    size_t tail = len & 7;
    if (tail) {
        __asm__ volatile(
            "cld\n"
            "xor %%rax, %%rax\n"
            "rep stosb"
            : "+D"(ptr), "+c"(tail)
            :
            : "rax", "memory");
    }
    return;
#elif defined(__aarch64__)
    size_t loops = len / 32;
    size_t bulk = loops * 32;
    if (loops) {
        __asm__ volatile(
            "1:\n"
            "stp xzr, xzr, [%0], #16\n"
            "stp xzr, xzr, [%0], #16\n"
            "subs %1, %1, #1\n"
            "b.ne 1b\n"
            : "+r"(ptr), "+r"(loops)
            :
            : "memory");
    }
    size_t rem = len - bulk;
    while (rem >= 8) {
        *((uint64_t *)ptr) = 0;
        ptr += 8;
        rem -= 8;
    }
    if (rem) ttak_mem_zero_fallback(ptr, rem);
    return;
#elif defined(__riscv_xlen) && (__riscv_xlen == 64)
    size_t loops = len / 8;
    size_t bulk = loops * 8;
    if (loops) {
        __asm__ volatile(
            "1:\n"
            "sd zero, 0(%0)\n"
            "addi %0, %0, 8\n"
            "addi %1, %1, -1\n"
            "bnez %1, 1b\n"
            : "+r"(ptr), "+r"(loops)
            :
            : "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_zero_fallback(ptr, rem);
    return;
#elif defined(__mips64) || defined(__mips64__) || (defined(__mips) && (__mips == 64))
    size_t loops = len / 8;
    size_t bulk = loops * 8;
    if (loops) {
        __asm__ volatile(
            ".set push\n"
            ".set noreorder\n"
            "1:\n"
            "sd $0, 0(%0)\n"
            "daddiu %0, %0, 8\n"
            "daddiu %1, %1, -1\n"
            "bnez %1, 1b\n"
            "nop\n"
            ".set pop\n"
            : "+r"(ptr), "+r"(loops)
            :
            : "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_zero_fallback(ptr, rem);
    return;
#elif defined(__powerpc64__) || defined(__ppc64__)
    size_t loops = len / 32;
    size_t bulk = loops * 32;
    uint64_t zero_val = 0;
    size_t ctr = loops;
    if (ctr) {
        __asm__ volatile(
            "mtctr %1\n"
            "1:\n"
            "std %2, 0(%0)\n"
            "std %2, 8(%0)\n"
            "std %2, 16(%0)\n"
            "std %2, 24(%0)\n"
            "addi %0, %0, 32\n"
            "bdnz 1b\n"
            : "+r"(ptr)
            : "r"(ctr), "r"(zero_val)
            : "ctr", "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_zero_fallback(ptr, rem);
    return;
#else
    ttak_mem_zero_fallback(ptr, len);
    return;
#endif
}

void ttak_mem_stream_copy(void *dst, const void *src, size_t len) {
    if (!len) return;
    uint8_t *dst_ptr = (uint8_t *)dst;
    const uint8_t *src_ptr = (const uint8_t *)src;
#if defined(__x86_64__)
    size_t qwords = len >> 3;
    if (qwords) {
        __asm__ volatile(
            "cld\n"
            "rep movsq"
            : "+D"(dst_ptr), "+S"(src_ptr), "+c"(qwords)
            :
            : "memory");
    }
    size_t tail = len & 7;
    if (tail) {
        __asm__ volatile(
            "cld\n"
            "rep movsb"
            : "+D"(dst_ptr), "+S"(src_ptr), "+c"(tail)
            :
            : "memory");
    }
    return;
#elif defined(__aarch64__)
    size_t loops = len / 32;
    size_t bulk = loops * 32;
    if (loops) {
        __asm__ volatile(
            "1:\n"
            "ldp x8, x9, [%1], #16\n"
            "ldp x10, x11, [%1], #16\n"
            "stp x8, x9, [%0], #16\n"
            "stp x10, x11, [%0], #16\n"
            "subs %2, %2, #1\n"
            "b.ne 1b\n"
            : "+r"(dst_ptr), "+r"(src_ptr), "+r"(loops)
            :
            : "x8", "x9", "x10", "x11", "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_copy_fallback(dst_ptr, src_ptr, rem);
    return;
#elif defined(__riscv_xlen) && (__riscv_xlen == 64)
    size_t loops = len / 8;
    size_t bulk = loops * 8;
    if (loops) {
        uint64_t tmp;
        __asm__ volatile(
            "1:\n"
            "ld %3, 0(%1)\n"
            "sd %3, 0(%0)\n"
            "addi %1, %1, 8\n"
            "addi %0, %0, 8\n"
            "addi %2, %2, -1\n"
            "bnez %2, 1b\n"
            : "+r"(dst_ptr), "+r"(src_ptr), "+r"(loops), "=&r"(tmp)
            :
            : "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_copy_fallback(dst_ptr, src_ptr, rem);
    return;
#elif defined(__mips64) || defined(__mips64__) || (defined(__mips) && (__mips == 64))
    size_t loops = len / 8;
    size_t bulk = loops * 8;
    if (loops) {
        uint64_t tmp;
        __asm__ volatile(
            ".set push\n"
            ".set noreorder\n"
            "1:\n"
            "ld %2, 0(%1)\n"
            "sd %2, 0(%0)\n"
            "daddiu %1, %1, 8\n"
            "daddiu %0, %0, 8\n"
            "daddiu %3, %3, -1\n"
            "bnez %3, 1b\n"
            "nop\n"
            ".set pop\n"
            : "+r"(dst_ptr), "+r"(src_ptr), "=&r"(tmp), "+r"(loops)
            :
            : "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_copy_fallback(dst_ptr, src_ptr, rem);
    return;
#elif defined(__powerpc64__) || defined(__ppc64__)
    size_t loops = len / 32;
    size_t bulk = loops * 32;
    if (loops) {
        size_t ctr = loops;
        uint64_t tmp1, tmp2, tmp3, tmp4;
        __asm__ volatile(
            "mtctr %6\n"
            "1:\n"
            "ld %2, 0(%1)\n"
            "ld %3, 8(%1)\n"
            "ld %4, 16(%1)\n"
            "ld %5, 24(%1)\n"
            "std %2, 0(%0)\n"
            "std %3, 8(%0)\n"
            "std %4, 16(%0)\n"
            "std %5, 24(%0)\n"
            "addi %1, %1, 32\n"
            "addi %0, %0, 32\n"
            "bdnz 1b\n"
            : "+r"(dst_ptr), "+r"(src_ptr), "=&r"(tmp1), "=&r"(tmp2),
              "=&r"(tmp3), "=&r"(tmp4)
            : "r"(ctr)
            : "ctr", "memory");
    }
    size_t rem = len - bulk;
    if (rem) ttak_mem_copy_fallback(dst_ptr, src_ptr, rem);
    return;
#else
    ttak_mem_copy_fallback(dst_ptr, src_ptr, len);
    return;
#endif
}

#endif /* __TINYC__ */
