#ifndef __cplusplus
#  ifndef _BOOL_DEFINED
#    include <stdbool.h>
     /* GCC/Clang define _Bool as a built-in keyword. 
        Only typedef for MSVC or compilers lacking native _Bool support. */
#    if defined(_MSC_VER) && !defined(__clang__) && _MSC_VER < 1928
#      ifndef _Bool
         typedef bool _Bool;
#      endif
#    endif
#    define _BOOL_DEFINED
#  endif
#else
  /* Map _Bool to bool in C++ (CUDA) to ensure type compatibility 
     without redefining reserved keywords. */
#  ifndef _Bool
#    define _Bool bool
#  endif
#endif

#if defined(__has_include_next) && defined(__GNUC__) && !defined(__clang__) && !defined(__TINYC__)
#  if __has_include_next(<stdatomic.h>)
#    include_next <stdatomic.h>
#    define __TTAK_STDATOMIC_SYSTEM_INCLUDED
#  endif
#endif

#if defined(__clang__) && !defined(__TTAK_STDATOMIC_SYSTEM_INCLUDED)
#  warning "Clang is using portable stdatomic.h!"
#endif
#ifndef TTAK_PORTABLE_STDATOMIC_H
#define TTAK_PORTABLE_STDATOMIC_H

/* -----------------------------------------------------------------------
 * MSVC / Windows: provide atomic operations via interlocked intrinsics.
 * MSVC does not support #include_next so we cannot reach the system
 * <stdatomic.h> from here.  With /std:c17 the compiler handles _Atomic
 * as a keyword; we only need to supply the standard library types and
 * function-like macros that the compiler itself does not define.
 * --------------------------------------------------------------------- */
#if !defined(__TTAK_STDATOMIC_SYSTEM_INCLUDED) && defined(_MSC_VER)
#  define __TTAK_STDATOMIC_SYSTEM_INCLUDED
#  include <intrin.h>
#  include <stdbool.h>
#  include <stddef.h>
#  include <stdint.h>

/* _Atomic as a type qualifier: MSVC older than 17.5 does not support _Atomic
 * as a keyword.  Define it as volatile so that struct field declarations and
 * variable definitions in the rest of the library parse correctly.  The actual
 * atomicity of all read-modify-write operations is provided by the Interlocked
 * intrinsic macros defined below. */
#ifndef _Atomic
#  define _Atomic volatile
#endif

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

typedef volatile bool               atomic_bool;
typedef volatile int                atomic_int;
typedef volatile unsigned int       atomic_uint;
typedef volatile long               atomic_long;
typedef volatile unsigned long      atomic_ulong;
typedef volatile long long          atomic_llong;
typedef volatile unsigned long long atomic_ullong;
typedef volatile uint_least64_t     atomic_uint_least64_t;
typedef volatile size_t             atomic_size_t;
typedef volatile unsigned long long atomic_uint_fast64_t;
typedef volatile uintptr_t          atomic_uintptr_t;

typedef struct { volatile unsigned char _value; } atomic_flag;

#define ATOMIC_FLAG_INIT   {0}
#define ATOMIC_VAR_INIT(v) (v)

#define atomic_thread_fence(order)  ((void)(order), _ReadWriteBarrier())
#define atomic_init(obj, value)     ((void)(*(obj) = (value)))

/* load / store: on x86-64 aligned loads/stores of native-width types are
 * inherently atomic and carry acquire/release semantics.              */
#define atomic_load_explicit(obj, order)           ((void)(order), *(obj))
#define atomic_load(obj)                           (*(obj))
#define atomic_store_explicit(obj, desired, order) ((void)(order), (void)(*(obj) = (desired)))
#define atomic_store(obj, desired)                 ((void)(*(obj) = (desired)))

/* --- fetch_add helpers --- */
static __inline char __ttak_fa8(volatile char *p, char v)
    { return _InterlockedExchangeAdd8(p, v); }
static __inline long __ttak_fa32(volatile long *p, long v)
    { return _InterlockedExchangeAdd(p, v); }
static __inline __int64 __ttak_fa64(volatile __int64 *p, __int64 v)
    { return _InterlockedExchangeAdd64(p, v); }

#define atomic_fetch_add_explicit(obj, val, order) ((void)(order), \
    _Generic(*(obj), \
        signed char:        (signed char)__ttak_fa8((volatile char*)(obj), (char)(val)), \
        unsigned char:      (unsigned char)__ttak_fa8((volatile char*)(obj), (char)(val)), \
        _Bool:              (unsigned char)__ttak_fa8((volatile char*)(obj), (char)(val)), \
        short:              (short)_InterlockedExchangeAdd16((volatile short*)(obj), (short)(val)), \
        unsigned short:     (unsigned short)_InterlockedExchangeAdd16((volatile short*)(obj), (short)(val)), \
        int:                (int)__ttak_fa32((volatile long*)(obj), (long)(val)), \
        unsigned int:       (unsigned int)__ttak_fa32((volatile long*)(obj), (long)(val)), \
        long:               __ttak_fa32((volatile long*)(obj), (long)(val)), \
        unsigned long:      (unsigned long)__ttak_fa32((volatile long*)(obj), (long)(val)), \
        long long:          (long long)__ttak_fa64((volatile __int64*)(obj), (__int64)(val)), \
        unsigned long long: (unsigned long long)__ttak_fa64((volatile __int64*)(obj), (__int64)(val)), \
        default:            (unsigned long long)__ttak_fa64((volatile __int64*)(obj), (__int64)(val)) \
    ))
#define atomic_fetch_add(obj, val) \
    atomic_fetch_add_explicit((obj), (val), memory_order_seq_cst)

#define atomic_fetch_sub_explicit(obj, val, order) \
    atomic_fetch_add_explicit((obj), -(val), (order))
#define atomic_fetch_sub(obj, val) \
    atomic_fetch_sub_explicit((obj), (val), memory_order_seq_cst)

static __inline unsigned long long __ttak_fo64(volatile unsigned long long *p, unsigned long long v)
    { return (unsigned long long)_InterlockedOr64((volatile __int64*)p, (__int64)v); }
static __inline unsigned long long __ttak_fand64(volatile unsigned long long *p, unsigned long long v)
    { return (unsigned long long)_InterlockedAnd64((volatile __int64*)p, (__int64)v); }
#define atomic_fetch_or_explicit(obj, val, order)     ((void)(order), __ttak_fo64((volatile unsigned long long*)(obj), (unsigned long long)(val)))
#define atomic_fetch_or(obj, val)     atomic_fetch_or_explicit((obj), (val), memory_order_seq_cst)
#define atomic_fetch_and_explicit(obj, val, order)     ((void)(order), __ttak_fand64((volatile unsigned long long*)(obj), (unsigned long long)(val)))
#define atomic_fetch_and(obj, val)     atomic_fetch_and_explicit((obj), (val), memory_order_seq_cst)

/* --- exchange helpers --- */
static __inline long __ttak_xch32(volatile long *p, long v)
    { return _InterlockedExchange(p, v); }
static __inline __int64 __ttak_xch64(volatile __int64 *p, __int64 v)
    { return _InterlockedExchange64(p, v); }

#define atomic_exchange_explicit(obj, desired, order) ((void)(order), \
    _Generic(*(obj), \
        int:                (int)__ttak_xch32((volatile long*)(obj), (long)(desired)), \
        unsigned int:       (unsigned int)__ttak_xch32((volatile long*)(obj), (long)(desired)), \
        long:               __ttak_xch32((volatile long*)(obj), (long)(desired)), \
        unsigned long:      (unsigned long)__ttak_xch32((volatile long*)(obj), (long)(desired)), \
        long long:          (long long)__ttak_xch64((volatile __int64*)(obj), (__int64)(desired)), \
        unsigned long long: (unsigned long long)__ttak_xch64((volatile __int64*)(obj), (__int64)(desired)), \
        default:            (unsigned long long)__ttak_xch64((volatile __int64*)(obj), (__int64)(desired)) \
    ))
#define atomic_exchange(obj, desired) \
    atomic_exchange_explicit((obj), (desired), memory_order_seq_cst)

/* --- compare-exchange helpers --- */
static __inline _Bool __ttak_cx32(volatile long *obj, long *exp, long des) {
    long old = _InterlockedCompareExchange(obj, des, *exp);
    if (old == *exp) return 1;
    *exp = old; return 0;
}
static __inline _Bool __ttak_cx64(volatile __int64 *obj, __int64 *exp, __int64 des) {
    __int64 old = _InterlockedCompareExchange64(obj, des, *exp);
    if (old == *exp) return 1;
    *exp = old; return 0;
}

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
    ((void)(succ), (void)(fail), \
    _Generic(*(obj), \
        int:          __ttak_cx32((volatile long*)(obj), (long*)(expected), (long)(desired)), \
        unsigned int: __ttak_cx32((volatile long*)(obj), (long*)(expected), (long)(desired)), \
        long:         __ttak_cx32((volatile long*)(obj), (long*)(expected), (long)(desired)), \
        unsigned long: __ttak_cx32((volatile long*)(obj), (long*)(expected), (long)(desired)), \
        long long:    __ttak_cx64((volatile __int64*)(obj), (__int64*)(expected), (__int64)(desired)), \
        unsigned long long: __ttak_cx64((volatile __int64*)(obj), (__int64*)(expected), (__int64)(desired)), \
        default:      __ttak_cx64((volatile __int64*)(obj), (__int64*)(expected), (__int64)(desired)) \
    ))
#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), \
        memory_order_seq_cst, memory_order_seq_cst)
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), (succ), (fail))
#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_weak((obj), (expected), (desired))

/* --- atomic_flag --- */
static __inline _Bool __msvc_flag_tas(volatile atomic_flag *f) {
    return _InterlockedExchange8((volatile char*)&f->_value, 1) != 0;
}
static __inline void __msvc_flag_clr(volatile atomic_flag *f) {
    _InterlockedExchange8((volatile char*)&f->_value, 0);
}
static inline _Bool atomic_flag_test_and_set_explicit(
        volatile atomic_flag *obj, memory_order order) {
    (void)order; return __msvc_flag_tas(obj);
}
static inline void atomic_flag_clear_explicit(
        volatile atomic_flag *obj, memory_order order) {
    (void)order; __msvc_flag_clr(obj);
}
#define atomic_flag_test_and_set(obj) \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)
#define atomic_flag_clear(obj) \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#endif /* !__TTAK_STDATOMIC_SYSTEM_INCLUDED && _MSC_VER */

#if defined(__TINYC__) || !defined(__TTAK_STDATOMIC_SYSTEM_INCLUDED)
#define __TTAK_NEEDS_PORTABLE_STDATOMIC__ 1
#else
#define __TTAK_NEEDS_PORTABLE_STDATOMIC__ 0
#endif

#if __TTAK_NEEDS_PORTABLE_STDATOMIC__

#define ATOMIC_FLAG_INIT   {0}
#define ATOMIC_VAR_INIT(v) (v)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#ifndef _Atomic
#define _Atomic volatile
#endif

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

typedef volatile bool               atomic_bool;
typedef volatile char               atomic_char;
typedef volatile signed char        atomic_schar;
typedef volatile unsigned char      atomic_uchar;
typedef volatile short              atomic_short;
typedef volatile unsigned short     atomic_ushort;
typedef volatile int                atomic_int;
typedef volatile unsigned int       atomic_uint;
typedef volatile long               atomic_long;
typedef volatile unsigned long      atomic_ulong;
typedef volatile long long          atomic_llong;
typedef volatile unsigned long long atomic_ullong;
typedef volatile int_least8_t       atomic_int_least8_t;
typedef volatile uint_least8_t      atomic_uint_least8_t;
typedef volatile int_least16_t      atomic_int_least16_t;
typedef volatile uint_least16_t     atomic_uint_least16_t;
typedef volatile int_least32_t      atomic_int_least32_t;
typedef volatile uint_least32_t     atomic_uint_least32_t;
typedef volatile int_least64_t      atomic_int_least64_t;
typedef volatile uint_least64_t     atomic_uint_least64_t;
typedef volatile int_fast8_t        atomic_int_fast8_t;
typedef volatile uint_fast8_t       atomic_uint_fast8_t;
typedef volatile int_fast16_t       atomic_int_fast16_t;
typedef volatile uint_fast16_t      atomic_uint_fast16_t;
typedef volatile int_fast32_t       atomic_int_fast32_t;
typedef volatile uint_fast32_t      atomic_uint_fast32_t;
typedef volatile int_fast64_t       atomic_int_fast64_t;
typedef volatile uint_fast64_t      atomic_uint_fast64_t;
typedef volatile intptr_t           atomic_intptr_t;
typedef volatile uintptr_t          atomic_uintptr_t;
typedef volatile size_t             atomic_size_t;
typedef volatile ptrdiff_t          atomic_ptrdiff_t;
typedef volatile intmax_t           atomic_intmax_t;
typedef volatile uintmax_t          atomic_uintmax_t;

typedef struct {
    volatile unsigned char _value;
} atomic_flag;

extern pthread_mutex_t __ttak_atomic_global_lock;

#if defined(__GNUC__) || defined(__clang__)

#define ATOMIC_FLAG_INIT   {0}
#define ATOMIC_VAR_INIT(v) (v)

/* Use compiler built-ins for GCC and Clang to guarantee lock-free performance 
   and bypass potentially broken system stdatomic.h headers. */

#define atomic_thread_fence(order) __atomic_thread_fence(order)

#define atomic_init(obj, value) \
    do { *(obj) = (value); } while (0)

#define atomic_store_explicit(obj, desired, order) \
    __atomic_store_n((obj), (desired), (order))

#define atomic_store(obj, desired) \
    atomic_store_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_load_explicit(obj, order) \
    __atomic_load_n((obj), (order))

#define atomic_load(obj) \
    atomic_load_explicit((obj), memory_order_seq_cst)

#define atomic_fetch_add_explicit(obj, operand, order) \
    __atomic_fetch_add((obj), (operand), (order))

#define atomic_fetch_add(obj, operand) \
    atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_fetch_sub_explicit(obj, operand, order) \
    __atomic_fetch_sub((obj), (operand), (order))

#define atomic_fetch_sub(obj, operand) \
    atomic_fetch_sub_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_fetch_or_explicit(obj, operand, order) \
    __atomic_fetch_or((obj), (operand), (order))

#define atomic_fetch_or(obj, operand) \
    atomic_fetch_or_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_fetch_and_explicit(obj, operand, order) \
    __atomic_fetch_and((obj), (operand), (order))

#define atomic_fetch_and(obj, operand) \
    atomic_fetch_and_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_exchange_explicit(obj, desired, order) \
    __atomic_exchange_n((obj), (desired), (order))

#define atomic_exchange(obj, desired) \
    atomic_exchange_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    __atomic_compare_exchange_n((obj), (expected), (desired), 1, (success), (failure))

#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)

#define atomic_compare_exchange_strong_explicit(obj, expected, desired, success, failure) \
    __atomic_compare_exchange_n((obj), (expected), (desired), 0, (success), (failure))

#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)

static inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, memory_order order) {
    return __atomic_test_and_set(&(obj->_value), order);
}

#define atomic_flag_test_and_set(obj) \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)

static inline void atomic_flag_clear_explicit(volatile atomic_flag *obj, memory_order order) {
    __atomic_clear(&(obj->_value), order);
}

#define atomic_flag_clear(obj) \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#else /* Fallback for TCC and others */

#define ATOMIC_FLAG_INIT   {0}
#define ATOMIC_VAR_INIT(v) (v)

#if defined(__x86_64__) || defined(__i386__)
#define atomic_thread_fence(order) __asm__ __volatile__ ("mfence" ::: "memory")
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)
#define atomic_store_explicit(obj, desired, order) \
    do { \
        __asm__ __volatile__ ("" ::: "memory"); \
        *(obj) = (desired); \
        if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("mfence" ::: "memory"); \
    } while (0)
#define atomic_store(obj, desired) atomic_store_explicit((obj), (desired), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) \
    ({ __asm__ __volatile__ ("" ::: "memory"); \
       __typeof__(*(obj)) __val = *(obj); \
       __asm__ __volatile__ ("" ::: "memory"); \
       __val; })
#define atomic_load(obj) atomic_load_explicit((obj), memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __val = (operand); \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("lock xaddq %0, %1" : "+r" (__val), "+m" (*(obj)) : : "memory", "cc"); \
       else if (sizeof(*(obj)) == 4) __asm__ __volatile__ ("lock xaddl %0, %1" : "+r" (__val), "+m" (*(obj)) : : "memory", "cc"); \
       else if (sizeof(*(obj)) == 1) __asm__ __volatile__ ("lock xaddb %0, %1" : "+q" (__val), "+m" (*(obj)) : : "memory", "cc"); \
       __val; })
#define atomic_fetch_add(obj, operand) atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)

/* Use CAS loops for fetch_or / fetch_and / fetch_sub on x86 to avoid complex asm */
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ret; __typeof__(*(obj)) __exp = *(expected); \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("lock cmpxchgq %3, %1; setz %0" : "=q" (__ret), "+m" (*(obj)), "+a" (__exp) : "r" ((__typeof__(*(obj)))(desired)) : "memory", "cc"); \
       else if (sizeof(*(obj)) == 4) __asm__ __volatile__ ("lock cmpxchgl %3, %1; setz %0" : "=q" (__ret), "+m" (*(obj)), "+a" (__exp) : "r" ((__typeof__(*(obj)))(desired)) : "memory", "cc"); \
       else if (sizeof(*(obj)) == 1) __asm__ __volatile__ ("lock cmpxchgb %3, %1; setz %0" : "=q" (__ret), "+m" (*(obj)), "+a" (__exp) : "q" ((__typeof__(*(obj)))(desired)) : "memory", "cc"); \
       if (!__ret) *(expected) = __exp; \
       __ret; })

#elif defined(__aarch64__)
#define atomic_thread_fence(order) __asm__ __volatile__ ("dmb ish" ::: "memory")
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)
#define atomic_store_explicit(obj, desired, order) \
    do { \
        if ((order) == memory_order_release || (order) == memory_order_seq_cst) __asm__ __volatile__ ("dmb ish" ::: "memory"); \
        *(obj) = (desired); \
        if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("dmb ish" ::: "memory"); \
    } while (0)
#define atomic_store(obj, desired) atomic_store_explicit((obj), (desired), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) \
    ({ if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("dmb ish" ::: "memory"); \
       __typeof__(*(obj)) __val = *(obj); \
       if ((order) == memory_order_acquire || (order) == memory_order_seq_cst) __asm__ __volatile__ ("dmb ish" ::: "memory"); \
       __val; })
#define atomic_load(obj) atomic_load_explicit((obj), memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __old, __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: ldxr %0, [%2]\n add %1, %0, %3\n stxr %w1, %1, [%2]\n cbnz %w1, 1b" : "=&r" (__old), "=&r" (__tmp) : "r" (obj), "r" ((__typeof__(*(obj)))(operand)) : "memory", "cc"); \
       else __asm__ __volatile__ ("1: ldaxr %w0, [%2]\n add %w1, %w0, %w3\n stlxr %w1, %w1, [%2]\n cbnz %w1, 1b" : "=&r" (__old), "=&r" (__tmp) : "r" (obj), "r" ((__typeof__(*(obj)))(operand)) : "memory", "cc"); \
       __old; })
#define atomic_fetch_add(obj, operand) atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ret; __typeof__(*(obj)) __exp = *(expected), __old; int __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: ldxr %0, [%3]\n cmp %0, %4\n b.ne 2f\n stxr %w1, %5, [%3]\n cmp %w1, #0\n b.ne 1b\n 2:" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "r" (obj), "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory", "cc"); \
       else __asm__ __volatile__ ("1: ldaxr %w0, [%3]\n cmp %w0, %w4\n b.ne 2f\n stlxr %w1, %w5, [%3]\n cmp %w1, #0\n b.ne 1b\n 2:" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "r" (obj), "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory", "cc"); \
       __ret = (__old == __exp); if (!__ret) *(expected) = __old; \
       __ret; })

#elif defined(__mips64)
#define atomic_thread_fence(order) __asm__ __volatile__ ("sync" ::: "memory")
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)
#define atomic_store_explicit(obj, desired, order) \
    do { \
        if ((order) == memory_order_release || (order) == memory_order_seq_cst) __asm__ __volatile__ ("sync" ::: "memory"); \
        *(obj) = (desired); \
        if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("sync" ::: "memory"); \
    } while (0)
#define atomic_store(obj, desired) atomic_store_explicit((obj), (desired), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) \
    ({ if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("sync" ::: "memory"); \
       __typeof__(*(obj)) __val = *(obj); \
       if ((order) == memory_order_acquire || (order) == memory_order_seq_cst) __asm__ __volatile__ ("sync" ::: "memory"); \
       __val; })
#define atomic_load(obj) atomic_load_explicit((obj), memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __old, __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: lld %0, %2\n daddu %1, %0, %3\n scd %1, %2\n beqz %1, 1b\n nop" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "m" (*(obj)), "r" ((__typeof__(*(obj)))(operand)) : "memory"); \
       else __asm__ __volatile__ ("1: ll %0, %2\n addu %1, %0, %3\n sc %1, %2\n beqz %1, 1b\n nop" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "m" (*(obj)), "r" ((__typeof__(*(obj)))(operand)) : "memory"); \
       __old; })
#define atomic_fetch_add(obj, operand) atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ret; __typeof__(*(obj)) __exp = *(expected), __old; int __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: lld %0, %3\n bne %0, %4, 2f\n nop\n move %1, %5\n scd %1, %3\n beqz %1, 1b\n nop\n 2:" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "m" (*(obj)), "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory"); \
       else __asm__ __volatile__ ("1: ll %0, %3\n bne %0, %4, 2f\n nop\n move %1, %5\n sc %1, %3\n beqz %1, 1b\n nop\n 2:" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "m" (*(obj)), "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory"); \
       __ret = (__old == __exp); if (!__ret) *(expected) = __old; \
       __ret; })

#elif defined(__PPC64__)
#define atomic_thread_fence(order) __asm__ __volatile__ ("sync" ::: "memory")
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)
#define atomic_store_explicit(obj, desired, order) \
    do { \
        if ((order) == memory_order_release || (order) == memory_order_seq_cst) __asm__ __volatile__ ("lwsync" ::: "memory"); \
        *(obj) = (desired); \
        if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("sync" ::: "memory"); \
    } while (0)
#define atomic_store(obj, desired) atomic_store_explicit((obj), (desired), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) \
    ({ if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("sync" ::: "memory"); \
       __typeof__(*(obj)) __val = *(obj); \
       if ((order) == memory_order_acquire || (order) == memory_order_seq_cst) __asm__ __volatile__ ("lwsync" ::: "memory"); \
       __val; })
#define atomic_load(obj) atomic_load_explicit((obj), memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __old, __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: ldarx %0, 0, %2\n add %1, %0, %3\n stdcx. %1, 0, %2\n bne- 1b" : "=&r" (__old), "=&r" (__tmp) : "r" (obj), "r" ((__typeof__(*(obj)))(operand)) : "memory", "cr0"); \
       else __asm__ __volatile__ ("1: lwarx %0, 0, %2\n add %1, %0, %3\n stwcx. %1, 0, %2\n bne- 1b" : "=&r" (__old), "=&r" (__tmp) : "r" (obj), "r" ((__typeof__(*(obj)))(operand)) : "memory", "cr0"); \
       __old; })
#define atomic_fetch_add(obj, operand) atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ret; __typeof__(*(obj)) __exp = *(expected), __old; int __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: ldarx %0, 0, %3\n cmpd %0, %4\n bne- 2f\n stdcx. %5, 0, %3\n bne- 1b\n 2:" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "r" (obj), "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory", "cr0"); \
       else __asm__ __volatile__ ("1: lwarx %0, 0, %3\n cmpw %0, %4\n bne- 2f\n stwcx. %5, 0, %3\n bne- 1b\n 2:" : "=&r" (__old), "=&r" (__tmp), "+m" (*(obj)) : "r" (obj), "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory", "cr0"); \
       __ret = (__old == __exp); if (!__ret) *(expected) = __old; \
       __ret; })

#elif defined(__riscv) && __riscv_xlen == 64
#define atomic_thread_fence(order) __asm__ __volatile__ ("fence rw, rw" ::: "memory")
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)
#define atomic_store_explicit(obj, desired, order) \
    do { \
        if ((order) == memory_order_release || (order) == memory_order_seq_cst) __asm__ __volatile__ ("fence rw, w" ::: "memory"); \
        *(obj) = (desired); \
        if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("fence w, rw" ::: "memory"); \
    } while (0)
#define atomic_store(obj, desired) atomic_store_explicit((obj), (desired), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) \
    ({ if ((order) == memory_order_seq_cst) __asm__ __volatile__ ("fence rw, rw" ::: "memory"); \
       __typeof__(*(obj)) __val = *(obj); \
       if ((order) == memory_order_acquire || (order) == memory_order_seq_cst) __asm__ __volatile__ ("fence r, rw" ::: "memory"); \
       __val; })
#define atomic_load(obj) atomic_load_explicit((obj), memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __old; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("amoadd.d.aqrl %0, %2, %1" : "=r" (__old), "+A" (*(obj)) : "r" ((__typeof__(*(obj)))(operand)) : "memory"); \
       else __asm__ __volatile__ ("amoadd.w.aqrl %0, %2, %1" : "=r" (__old), "+A" (*(obj)) : "r" ((__typeof__(*(obj)))(operand)) : "memory"); \
       __old; })
#define atomic_fetch_add(obj, operand) atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ret; __typeof__(*(obj)) __exp = *(expected), __old; int __tmp; \
       if (sizeof(*(obj)) == 8) __asm__ __volatile__ ("1: lr.d.aqrl %0, %2\n bne %0, %3, 2f\n sc.d.aqrl %1, %4, %2\n bnez %1, 1b\n 2:" : "=&r" (__old), "=&r" (__tmp), "+A" (*(obj)) : "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory"); \
       else __asm__ __volatile__ ("1: lr.w.aqrl %0, %2\n bne %0, %3, 2f\n sc.w.aqrl %1, %4, %2\n bnez %1, 1b\n 2:" : "=&r" (__old), "=&r" (__tmp), "+A" (*(obj)) : "r" (__exp), "r" ((__typeof__(*(obj)))(desired)) : "memory"); \
       __ret = (__old == __exp); if (!__ret) *(expected) = __old; \
       __ret; })

#else
/* Fallback to global locks for unsupported architectures or if we need a catch-all */
extern pthread_mutex_t __ttak_atomic_global_lock;
#define __TT_ATOMIC_LOCK() pthread_mutex_lock(&__ttak_atomic_global_lock)
#define __TT_ATOMIC_UNLOCK() pthread_mutex_unlock(&__ttak_atomic_global_lock)

#define atomic_thread_fence(order) do { (void)(order); __TT_ATOMIC_LOCK(); __TT_ATOMIC_UNLOCK(); } while (0)
#define atomic_init(obj, value) do { *(obj) = (value); } while (0)
#define atomic_store_explicit(obj, desired, order) do { (void)(order); __TT_ATOMIC_LOCK(); *(obj) = (desired); __TT_ATOMIC_UNLOCK(); } while (0)
#define atomic_store(obj, desired) atomic_store_explicit((obj), (desired), memory_order_seq_cst)
#define atomic_load_explicit(obj, order) ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __val = *(obj); (void)(order); __TT_ATOMIC_UNLOCK(); __val; })
#define atomic_load(obj) atomic_load_explicit((obj) , memory_order_seq_cst)
#define atomic_fetch_add_explicit(obj, operand, order) ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __old = *(obj); *(obj) = __old + (operand); (void)(order); __TT_ATOMIC_UNLOCK(); __old; })
#define atomic_fetch_add(obj, operand) atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ok; (void)(success); (void)(failure); __TT_ATOMIC_LOCK(); \
       if (*(obj) == *(expected)) { *(obj) = (desired); __ok = true; } \
       else { *(expected) = *(obj); __ok = false; } \
       __TT_ATOMIC_UNLOCK(); __ok; })
#endif

/* Fallback macros for missing explicit variants */
#ifndef atomic_fetch_sub_explicit
#define atomic_fetch_sub_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __expected = atomic_load_explicit(obj, memory_order_relaxed); \
       while (!atomic_compare_exchange_weak_explicit(obj, &__expected, __expected - (operand), order, memory_order_relaxed)); \
       __expected; })
#endif
#ifndef atomic_fetch_sub
#define atomic_fetch_sub(obj, operand) atomic_fetch_sub_explicit((obj), (operand), memory_order_seq_cst)
#endif

#ifndef atomic_fetch_or_explicit
#define atomic_fetch_or_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __expected = atomic_load_explicit(obj, memory_order_relaxed); \
       while (!atomic_compare_exchange_weak_explicit(obj, &__expected, __expected | (operand), order, memory_order_relaxed)); \
       __expected; })
#endif
#ifndef atomic_fetch_or
#define atomic_fetch_or(obj, operand) atomic_fetch_or_explicit((obj), (operand), memory_order_seq_cst)
#endif

#ifndef atomic_fetch_and_explicit
#define atomic_fetch_and_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __expected = atomic_load_explicit(obj, memory_order_relaxed); \
       while (!atomic_compare_exchange_weak_explicit(obj, &__expected, __expected & (operand), order, memory_order_relaxed)); \
       __expected; })
#endif
#ifndef atomic_fetch_and
#define atomic_fetch_and(obj, operand) atomic_fetch_and_explicit((obj), (operand), memory_order_seq_cst)
#endif

#ifndef atomic_exchange_explicit
#define atomic_exchange_explicit(obj, desired, order) \
    ({ __typeof__(*(obj)) __expected = atomic_load_explicit(obj, memory_order_relaxed); \
       while (!atomic_compare_exchange_weak_explicit(obj, &__expected, (desired), order, memory_order_relaxed)); \
       __expected; })
#endif
#ifndef atomic_exchange
#define atomic_exchange(obj, desired) atomic_exchange_explicit((obj), (desired), memory_order_seq_cst)
#endif

#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)

#define atomic_compare_exchange_strong_explicit(obj, expected, desired, success, failure) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), (success), (failure))

#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)

static inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, memory_order order) {
    bool expected = false;
    return atomic_exchange_explicit(&obj->_value, true, order) != 0;
}

static inline void atomic_flag_clear_explicit(volatile atomic_flag *obj, memory_order order) {
    atomic_store_explicit(&obj->_value, 0, order);
}

#define atomic_flag_test_and_set(obj) \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)

#define atomic_flag_clear(obj) \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#ifndef atomic_compare_exchange_strong_explicit
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, success, failure) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), (success), (failure))
#endif

#ifndef atomic_compare_exchange_strong
#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)
#endif

#endif /* __GNUC__ || __clang__ fallback */

#endif /* __TTAK_NEEDS_PORTABLE_STDATOMIC__ */

#ifndef atomic_compare_exchange_strong_explicit
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, success, failure) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), (success), (failure))
#endif

#ifndef atomic_compare_exchange_strong
#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)
#endif

#ifndef atomic_fetch_or_explicit
#define atomic_fetch_or_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __expected = atomic_load_explicit(obj, memory_order_relaxed); \
       while (!atomic_compare_exchange_weak_explicit(obj, &__expected, __expected | (operand), order, memory_order_relaxed)); \
       __expected; })
#endif

#ifndef atomic_fetch_or
#define atomic_fetch_or(obj, operand) \
    atomic_fetch_or_explicit((obj), (operand), memory_order_seq_cst)
#endif

#ifndef atomic_fetch_and_explicit
#define atomic_fetch_and_explicit(obj, operand, order) \
    ({ __typeof__(*(obj)) __expected = atomic_load_explicit(obj, memory_order_relaxed); \
       while (!atomic_compare_exchange_weak_explicit(obj, &__expected, __expected & (operand), order, memory_order_relaxed)); \
       __expected; })
#endif

#ifndef atomic_fetch_and
#define atomic_fetch_and(obj, operand) \
    atomic_fetch_and_explicit((obj), (operand), memory_order_seq_cst)
#endif

#undef __TTAK_NEEDS_PORTABLE_STDATOMIC__

#endif /* TTAK_PORTABLE_STDATOMIC_H */
