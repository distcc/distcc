/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * librsync -- generate and apply network deltas
 *
 * Copyright (C) 2000, 2001, 2002, 2003, 2004 by Martin Pool
 * Copyright 2007 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/**
 * @file
 *
 * Reusable trace library.
 *
 * @todo A function like perror that includes strerror output.  Apache
 * does this by adding flags as well as the severity level which say
 * whether such information should be included.
 *
 * @todo Also check in configure for the C9X predefined identifier `_function', or
 * whatever it's called.
 **/

/* Provide simple macro statement wrappers (adapted from glib, and originally from Perl):
 *  RS_STMT_START { statements; } RS_STMT_END;
 *  can be used as a single statement, as in
 *  if (x) RS_STMT_START { ... } RS_STMT_END; else ...
 *
 *  For gcc we will wrap the statements within `({' and `})' braces.
 *  For SunOS they will be wrapped within `if (1)' and `else (void) 0',
 *  and otherwise within `do' and `while (0)'.
 */
#if !(defined (RS_STMT_START) && defined (RS_STMT_END))
#  if defined (__GNUC__) && !defined (__STRICT_ANSI__) && !defined (__cplusplus)
#    define RS_STMT_START    (void)(
#    define RS_STMT_END        )
#  else
#    if (defined (sun) || defined (__sun__))
#      define RS_STMT_START    if (1)
#      define RS_STMT_END    else (void)0
#    else
#      define RS_STMT_START    do
#      define RS_STMT_END    while (0)
#    endif
#  endif
#endif


#include <stdarg.h>

/* unconditionally on */
#define DO_RS_TRACE

/**
 * Log severity levels.
 *
 * These have the same numeric values as the levels for syslog, at
 * least in glibc.
 *
 * Trace may be turned off.
 *
 * Error is always on, but you can return and continue in some way.
 */
typedef enum {
    RS_LOG_EMERG         = 0,   /**< System is unusable */
    RS_LOG_ALERT         = 1,   /**< Action must be taken immediately */
    RS_LOG_CRIT          = 2,   /**< Critical conditions */
    RS_LOG_ERR           = 3,   /**< Error conditions */
    RS_LOG_WARNING       = 4,   /**< Warning conditions */
    RS_LOG_NOTICE        = 5,   /**< Normal but significant condition */
    RS_LOG_INFO          = 6,   /**< Informational */
    RS_LOG_DEBUG         = 7    /**< Debug-level messages */
} rs_loglevel;

int rs_loglevel_from_name(const char *name);

enum {
    RS_LOG_PRIMASK       = 7,   /**< Mask to extract priority
                                   part. \internal */

    RS_LOG_NONAME        = 8,   /**< \b Don't show function name in
                                   message. */

    RS_LOG_NO_PROGRAM   = 16,
    RS_LOG_NO_PID       = 32
};


/**
 * \typedef rs_logger_fn
 * \brief Callback to write out log messages.
 * \param level a syslog level.
 * \param msg message to be logged.
 *
 * \param private Opaque data passed in when logger was added.  For
 * example, pointer to file descriptor.
 */
typedef void    rs_logger_fn(int flags, const char *fn,
                              char const *msg, va_list,
                              void *private_ptr, int private_int);

void rs_format_msg(char *buf, size_t, int, const char *,
                   const char *fmt, va_list);

void            rs_trace_set_level(rs_loglevel level);

void rs_add_logger(rs_logger_fn *, int level, void *, int);
void rs_remove_logger(rs_logger_fn *, int level, void *, int);
void rs_remove_all_loggers(void);


void rs_logger_file(int level, const char *fn, char const *fmt, va_list va,
                    void *, int);

void rs_logger_syslog(int level, const char *fn, char const *fmt, va_list va,
                      void *, int);

/** Check whether the library was compiled with debugging trace support. */
int             rs_supports_trace(void);

void rs_log0(int level, char const *fn, char const *fmt, ...)
#if defined(__GNUC__)
    __attribute__ ((format(printf, 3, 4)))
#endif /* __GNUC__ */
  ;


  /* TODO: Check for the __FUNCTION__ thing, rather than gnuc */
#if defined(HAVE_VARARG_MACROS)  && defined(__GNUC__)

#if 1 || defined(DO_RS_TRACE)
#  define rs_trace(fmt, arg...)                            \
    do { rs_log0(RS_LOG_DEBUG, __FUNCTION__, fmt , ##arg);  \
    } while (0)
#else
#  define rs_trace(s, str...)
#endif    /* !DO_RS_TRACE */

#define rs_log(l, s, str...) do {              \
     rs_log0((l), __FUNCTION__, (s) , ##str);  \
     } while (0)


#define rs_log_crit(s, str...) do {                         \
     rs_log0(RS_LOG_CRIT,  __FUNCTION__, (s) , ##str);          \
     } while (0)

#define rs_log_error(s, str...) do {                            \
     rs_log0(RS_LOG_ERR,  __FUNCTION__, (s) , ##str);           \
     } while (0)

#define rs_log_notice(s, str...) do {                           \
     rs_log0(RS_LOG_NOTICE,  __FUNCTION__, (s) , ##str);        \
     } while (0)

#define rs_log_warning(s, str...) do {                          \
     rs_log0(RS_LOG_WARNING,  __FUNCTION__, (s) , ##str);       \
     } while (0)

#define rs_log_info(s, str...) do {                             \
     rs_log0(RS_LOG_INFO,  __FUNCTION__, (s) , ##str);          \
     } while (0)

#else /* not defined HAVE_VARARG_MACROS */

/* If we don't have gcc vararg macros, then we fall back to making the
 * log routines just plain functions.  On platforms without gcc (boo
 * hiss!) this means at least you get some messages, but not the nice
 * function names etc. */
#define rs_log rs_log0_nofn

#define rs_trace        rs_log_trace_nofn
#define rs_log_info     rs_log_info_nofn
#define rs_log_notice   rs_log_notice_nofn
#define rs_log_warning  rs_log_warning_nofn
#define rs_log_error    rs_log_error_nofn
#define rs_log_crit     rs_log_critical_nofn
#endif /* HAVE_VARARG_MACROS */



void rs_log_trace_nofn(char const *s, ...);
void rs_log_info_nofn(char const *, ...);
void rs_log_notice_nofn(char const *, ...);
void rs_log_warning_nofn(char const *s, ...);
void rs_log_error_nofn(char const *s, ...);
void rs_log_critical_nofn(char const *, ...);

void rs_log0_nofn(int level, char const *fmt, ...);



/**
 * \macro rs_trace_enabled()
 *
 * Call this before putting too much effort into generating trace
 * messages.
 */

/* really bool */
extern int rs_trace_syslog;
extern int rs_trace_level;

#ifdef DO_RS_TRACE
#  define rs_trace_enabled() ((rs_trace_level & RS_LOG_PRIMASK) >= RS_LOG_DEBUG)
#else
#  define rs_trace_enabled() 0
#endif

/**
 * Name of the program, to be included in log messages.
 *
 * @note This must be defined exactly once in each program that links to
 * trace.c
 **/
extern const char *rs_program_name;

void dcc_job_summary_clear(void);
void dcc_job_summary(void);
void dcc_job_summary_append(const char *s);
