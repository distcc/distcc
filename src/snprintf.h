/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*- */
/*
 * Copyright Patrick Powell 1995
 * This code is based on code written by Patrick Powell (papowell@astart.com)
 * It may be used for any purpose as long as this notice remains intact
 * on all source code distributions
 */

#include <stdarg.h>
#include "config.h"

#ifdef __GNUC__
/** Use gcc attribute to check printf fns.  a1 is the 1-based index of
 * the parameter containing the format, and a2 the index of the first
 * argument.  **/
#define PRINTF_ATTRIBUTE(a1, a2) __attribute__ ((format (__printf__, a1, a2)))
#else
#define PRINTF_ATTRIBUTE(a1, a2)
#endif


/* Note that the HAVE_DECL macros are defined to 0 if the declaration
 * is not present, rather than being undefined as is the case for most
 * autoconf tests. */


#if !HAVE_DECL_VASPRINTF
int vasprintf(char **ptr, const char *format, va_list ap);
#endif
#if !HAVE_DECL_SNPRINTF
int snprintf(char *,size_t ,const char *, ...) PRINTF_ATTRIBUTE(3,4);
#endif
#if !HAVE_DECL_ASPRINTF
int asprintf(char **,const char *, ...) PRINTF_ATTRIBUTE(2,3);
#endif

#if !HAVE_DECL_VSNPRINTF
int vsnprintf(char *, size_t, const char *, va_list);
#endif

int checked_asprintf(char **, const char *, ...) PRINTF_ATTRIBUTE(2,3);
