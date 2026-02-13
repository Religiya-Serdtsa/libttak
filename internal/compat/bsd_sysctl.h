#ifndef TTAK_COMPAT_BSD_SYSCTL_H
#define TTAK_COMPAT_BSD_SYSCTL_H

/*
 * BSD source compatibility shim for <sys/sysctl.h>.
 *
 * The project-wide -D_XOPEN_SOURCE=700 flag hides BSD-specific types
 * that <sys/sysctl.h> depends on:
 *
 *   NetBSD  – devmajor_t, u_int, sysctl() prototype are gated on
 *             _NETBSD_SOURCE, which _XOPEN_SOURCE suppresses.
 *   OpenBSD – u_long and other BSD types are gated on __BSD_VISIBLE,
 *             which <sys/cdefs.h> forces to 0 when _XOPEN_SOURCE is set.
 *
 * Fix: temporarily remove _XOPEN_SOURCE while pulling in the sysctl
 * interface, then restore it so the rest of the translation unit
 * keeps its POSIX semantics.
 *
 * If <sys/types.h> was already processed under _XOPEN_SOURCE, its
 * include guard prevents re-processing, so the BSD types it gates
 * remain hidden.  We therefore provide explicit fallback typedefs
 * for the specific types that <sys/sysctl.h> requires.
 */

#if defined(__NetBSD__) || defined(__OpenBSD__)

#  ifdef _XOPEN_SOURCE
#    define _TTAK_SAVED_XOPEN _XOPEN_SOURCE
#    undef  _XOPEN_SOURCE
#  endif

#  include <sys/param.h>

/*  -- NetBSD: devmajor_t and u_int are gated on _NETBSD_SOURCE ---
 *  If <sys/featuretest.h> was already processed while _XOPEN_SOURCE
 *  was active, _NETBSD_SOURCE was never defined and the types are
 *  missing.  Provide them explicitly so <sys/sysctl.h> compiles.   */
#  if defined(__NetBSD__) && !defined(_NETBSD_SOURCE)
#    include <stdint.h>
     typedef int32_t      devmajor_t;
     typedef unsigned int u_int;
#  endif

/*  -- OpenBSD: u_long is gated on __BSD_VISIBLE -------------------
 *  <sys/cdefs.h> sets __BSD_VISIBLE to 0 when _XOPEN_SOURCE >= 500.
 *  If it was already processed, u_long is still hidden.             */
#  if defined(__OpenBSD__) && defined(__BSD_VISIBLE) && !__BSD_VISIBLE
     typedef unsigned long u_long;
#  endif

#  include <sys/sysctl.h>

#  ifdef _TTAK_SAVED_XOPEN
#    define _XOPEN_SOURCE _TTAK_SAVED_XOPEN
#    undef  _TTAK_SAVED_XOPEN
#  endif

#endif /* __NetBSD__ || __OpenBSD__ */

#endif /* TTAK_COMPAT_BSD_SYSCTL_H */
