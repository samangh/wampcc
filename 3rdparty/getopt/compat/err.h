/*
 * Stand-in for the BSD <err.h>, providing only the warnx() used by the
 * bundled getopt_long.c on platforms that lack it (e.g. MSVC).
 *
 * This file is part of wampcc and is distributed under the MIT license. See
 * LICENSE for details.
 */

#ifndef WAMPCC_COMPAT_ERR_H
#define WAMPCC_COMPAT_ERR_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Print "progname: message" to stderr, as BSD warnx() does. */
static void warnx(const char* fmt, ...)
{
  va_list ap;
#ifdef _WIN32
  if (__argv && __argv[0])
    fprintf(stderr, "%s: ", __argv[0]);
#endif
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

#endif
