/**
 * @file ttak_arch_features.c
 * @brief Runtime architecture feature detection.
 *
 * Currently a placeholder for the narrow-first rollout. Future phases can add
 * cpuid / getauxval probing and expose a ttak_arch_features_t struct.
 */

#include <ttak/arch/ttak_arch.h>

#if defined(TTAK_ARCH_X86_64) && !defined(TTAK_COMPILER_TCC)
/* x86 runtime feature detection will go here. */
#endif

#if defined(TTAK_ARCH_AARCH64) && defined(__linux__) && !defined(TTAK_COMPILER_TCC)
/* getauxval(AT_HWCAP) probing will go here. */
#endif
