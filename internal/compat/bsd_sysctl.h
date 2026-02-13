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
 *   OpenBSD – <sys/cdefs.h> forces __BSD_VISIBLE=0 when _XOPEN_SOURCE
 *             is set, regardless of any prior -D__BSD_VISIBLE=1.
 *
 * Fix: temporarily remove _XOPEN_SOURCE while pulling in the sysctl
 * interface, then restore it so the rest of the translation unit
 * keeps its POSIX semantics.
 */

#if defined(__NetBSD__) || defined(__OpenBSD__)

#  ifdef _XOPEN_SOURCE
#    define _TTAK_SAVED_XOPEN _XOPEN_SOURCE
#    undef  _XOPEN_SOURCE
#  endif

#  include <sys/param.h>
#  include <sys/sysctl.h>

#  ifdef _TTAK_SAVED_XOPEN
#    define _XOPEN_SOURCE _TTAK_SAVED_XOPEN
#    undef  _TTAK_SAVED_XOPEN
#  endif

#endif /* __NetBSD__ || __OpenBSD__ */

#endif /* TTAK_COMPAT_BSD_SYSCTL_H */
