/*
 * Stand-in for the BSD <sys/cdefs.h>, providing only what the bundled
 * getopt.h needs on platforms that lack it (e.g. MSVC).
 *
 * This file is part of wampcc and is distributed under the MIT license. See
 * LICENSE for details.
 */

#ifndef WAMPCC_COMPAT_SYS_CDEFS_H
#define WAMPCC_COMPAT_SYS_CDEFS_H

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#endif
