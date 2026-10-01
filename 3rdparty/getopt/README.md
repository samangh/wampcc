## Origin

`getopt_long.c` and `getopt.h` are unmodified copies from OpenBSD,
fetched from https://github.com/openbsd/src. They are under the ISC and
2-clause BSD licenses given in each file's header; those notices must be
kept.

## Local additions

`compat/err.h` and `compat/sys/cdefs.h` stand-ins for the BSD headers
that the upstream files include. Keeping them separate means the
upstream files can be replaced with a newer version without editing
them.
