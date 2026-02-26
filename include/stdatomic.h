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

#if defined(__has_include_next) && !defined(__TINYC__)
#  if __has_include_next(<stdatomic.h>)
#    include_next <stdatomic.h>
#    define __TTAK_STDATOMIC_SYSTEM_INCLUDED
#  endif
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
typedef volatile int_least8_t       atomic_int_least8_t;
typedef volatile uint_least8_t      atomic_uint_least8_t;
typedef volatile int_least16_t      atomic_int_least16_t;
typedef volatile uint_least16_t     atomic_uint_least16_t;
typedef volatile int_least32_t      atomic_int_least32_t;
typedef volatile uint_least32_t     atomic_uint_least32_t;
typedef volatile int_least64_t      atomic_int_least64_t;
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

/* --- fetch_add helpers ------------------------------------------------- */
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

/* --- exchange helpers -------------------------------------------------- */
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

/* --- compare-exchange helpers ------------------------------------------ */
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

/* --- atomic_flag ------------------------------------------------------- */
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

#define ATOMIC_FLAG_INIT   {0}
#define ATOMIC_VAR_INIT(v) (v)

#define __TT_ATOMIC_LOCK() pthread_mutex_lock(&__ttak_atomic_global_lock)
#define __TT_ATOMIC_UNLOCK() pthread_mutex_unlock(&__ttak_atomic_global_lock)

#define atomic_thread_fence(order) do { (void)(order); __TT_ATOMIC_LOCK(); __TT_ATOMIC_UNLOCK(); } while (0)

#define atomic_init(obj, value) \
    do { atomic_store_explicit((obj), (value), memory_order_seq_cst); } while (0)

#define atomic_store_explicit(obj, desired, order) \
    do { (void)(order); __TT_ATOMIC_LOCK(); *(obj) = (desired); __TT_ATOMIC_UNLOCK(); } while (0)

#define atomic_store(obj, desired) \
    atomic_store_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_load_explicit(obj, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __val = *(obj); (void)(order); __TT_ATOMIC_UNLOCK(); __val; })

#define atomic_load(obj) \
    atomic_load_explicit((obj) , memory_order_seq_cst)

#define atomic_fetch_add_explicit(obj, operand, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __old = *(obj); *(obj) = __old + (operand); (void)(order); __TT_ATOMIC_UNLOCK(); __old; })

#define atomic_fetch_add(obj, operand) \
    atomic_fetch_add_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_fetch_sub_explicit(obj, operand, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __old = *(obj); *(obj) = __old - (operand); (void)(order); __TT_ATOMIC_UNLOCK(); __old; })

#define atomic_fetch_sub(obj, operand) \
    atomic_fetch_sub_explicit((obj), (operand), memory_order_seq_cst)

#define atomic_exchange_explicit(obj, desired, order) \
    ({ __TT_ATOMIC_LOCK(); __typeof__(*(obj)) __old = *(obj); *(obj) = (desired); (void)(order); __TT_ATOMIC_UNLOCK(); __old; })

#define atomic_exchange(obj, desired) \
    atomic_exchange_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, success, failure) \
    ({ bool __ok; (void)(success); (void)(failure); __TT_ATOMIC_LOCK(); \
       if (*(obj) == *(expected)) { *(obj) = (desired); __ok = true; } \
       else { *(expected) = *(obj); __ok = false; } \
       __TT_ATOMIC_UNLOCK(); __ok; })

#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), memory_order_seq_cst, memory_order_seq_cst)

static inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, memory_order order) {
    (void)order;
    __TT_ATOMIC_LOCK();
    bool prev = obj->_value != 0;
    obj->_value = 1;
    __TT_ATOMIC_UNLOCK();
    return prev;
}

static inline void atomic_flag_clear_explicit(volatile atomic_flag *obj, memory_order order) {
    (void)order;
    __TT_ATOMIC_LOCK();
    obj->_value = 0;
    __TT_ATOMIC_UNLOCK();
}

#define atomic_flag_test_and_set(obj) \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)

#define atomic_flag_clear(obj) \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#endif /* __TTAK_NEEDS_PORTABLE_STDATOMIC__ */

#undef __TTAK_NEEDS_PORTABLE_STDATOMIC__

#endif /* TTAK_PORTABLE_STDATOMIC_H */
