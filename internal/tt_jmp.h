#ifndef TTAK_INTERNAL_TT_JMP_H
#define TTAK_INTERNAL_TT_JMP_H

#include <setjmp.h>
#include <stdint.h>

/**
 * @brief Magic sentinel written by tt_setjmp and checked before tt_longjmp
 *        to confirm that the jump buffer is valid.
 */
#define TT_JMP_MAGIC 0x54544A4D50ULL /* "TTJMP" */

/**
 * @brief Portable setjmp/longjmp wrappers.
 *
 * POSIX platforms use sigsetjmp/siglongjmp so that the signal mask is
 * saved and restored across non-local jumps.  Windows (MinGW) lacks
 * the sig* variants, so we fall back to plain setjmp/longjmp.
 *
 * Both wrappers additionally record a magic value and the current
 * pthread ID so that callers can verify the buffer is live and belongs
 * to the executing thread.
 */

#ifdef _WIN32

#define tt_setjmp(env, magic_ptr, tid_ptr) \
    ( *(magic_ptr) = TT_JMP_MAGIC,        \
      *(tid_ptr) = (uint64_t)pthread_self(), \
      setjmp(env) )

#define tt_longjmp(env, magic_ptr, tid_ptr, val) \
    do {                                          \
        *(magic_ptr) = 0;                         \
        longjmp(env, val);                        \
    } while (0)

#else /* POSIX */

#define tt_setjmp(env, magic_ptr, tid_ptr) \
    ( *(magic_ptr) = TT_JMP_MAGIC,        \
      *(tid_ptr) = (uint64_t)pthread_self(), \
      sigsetjmp(env, 0) )

#define tt_longjmp(env, magic_ptr, tid_ptr, val) \
    do {                                          \
        *(magic_ptr) = 0;                         \
        siglongjmp(env, val);                     \
    } while (0)

#endif /* _WIN32 */

#endif /* TTAK_INTERNAL_TT_JMP_H */
