/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * ecolog - Reusable application logging library.
 *
 * Copyright (C) 2000 - 2003 by Martin Pool <mbp@samba.org>
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

                                     /*
                                      | Finality is death.
                                      | Perfection is finality.
                                      | Nothing is perfect.
                                      | There are lumps in it.
                                      */


#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <sys/file.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>

#include "distcc.h"
#include "trace.h"
#include "snprintf.h"
#include "va_copy.h"

struct rs_logger_list {
    rs_logger_fn               *fn;
    void                        *private_ptr;
    int                         private_int;
    int                         max_level;
    struct rs_logger_list       *next;
};

static struct rs_logger_list *logger_list = NULL;

/* really bool */
int rs_trace_syslog = FALSE;
int rs_trace_level = RS_LOG_NOTICE;

#ifdef UNUSED
/* nothing */
#elif defined(__GNUC__)
#  define UNUSED(x) x __attribute__((unused))
#elif defined(__LCLINT__)
#  define UNUSED(x) /*@unused@*/ x
#else                /* !__GNUC__ && !__LCLINT__ */
#  define UNUSED(x) x
#endif                /* !__GNUC__ && !__LCLINT__ */


static void rs_log_va(int level, char const *fn, char const *fmt, va_list va);

#if SIZEOF_SIZE_T > SIZEOF_LONG
#  warning size_t is larger than a long integer, values in trace messages may be wrong
#endif


/**
 * Log severity strings, if any.  Must match ordering in
 * ::rs_loglevel.
 */
static const char *rs_severities[] = {
    "EMERGENCY! ", "ALERT! ", "CRITICAL! ", "ERROR: ", "Warning: ",
    "", "", ""
};


/**********************************************************************
 * Functions for manipulating the list of loggers
 **********************************************************************/

void rs_remove_all_loggers(void)
{
    struct rs_logger_list *l, *next;

    for (l = logger_list; l; l = next) {
        next = l -> next;       /* save before destruction */
        free(l);
    }
    logger_list = NULL;
}


void rs_add_logger(rs_logger_fn fn,
                   int max_level,
                   void *private_ptr,
                   int private_int)
{
    struct rs_logger_list *l;

    if ((l = malloc(sizeof *l)) == NULL)
        return;

    l->fn = fn;
    l->max_level = max_level;
    l->private_ptr = private_ptr;
    l->private_int = private_int;

    l->next = logger_list;
    logger_list = l;
}


/**
 * Remove only the logger that exactly matches the specified parameters
 **/
void rs_remove_logger(rs_logger_fn fn,
                      int max_level,
                      void *private_ptr,
                      int private_int)
{
    struct rs_logger_list *l, **pl;

    for (pl = &logger_list; *pl; pl = &((*pl)->next)) {
        l = *pl;
        if (l->fn == fn
            && l->max_level == max_level
            && l->private_ptr == private_ptr
            && l->private_int == private_int) {
            /* unhook from list by adjusting whoever points to this. */
            *pl = l->next;
            free(l);
            return;
        }
    }
}


/**
 * Set the least important (i.e. largest) message severity that
 * will be output.
 */
void
rs_trace_set_level(rs_loglevel level)
{
    rs_trace_level = level;
}



/**
 * Work out a log level from a string name.
 *
 * Returns -1 for invalid names.
 */
int
rs_loglevel_from_name(const char *name)
{
    if (!strcmp(name, "emerg") || !strcmp(name, "emergency"))
        return RS_LOG_EMERG;
    else if (!strcmp(name, "alert"))
        return RS_LOG_ALERT;
    else if (!strcmp(name, "critical") || !strcmp(name, "crit"))
        return RS_LOG_CRIT;
    else if (!strcmp(name, "error") || !strcmp(name, "err"))
        return RS_LOG_ERR;
    else if (!strcmp(name, "warning") || !strcmp(name, "warn"))
        return RS_LOG_WARNING;
    else if (!strcmp(name, "notice") || !strcmp(name, "note"))
        return RS_LOG_NOTICE;
    else if (!strcmp(name, "info"))
        return RS_LOG_INFO;
    else if (!strcmp(name, "debug"))
        return RS_LOG_DEBUG;

    return -1;
}


/**
 * If you don't initialize a logger before first logging, then we
 * write to stderr by default.
 **/
static void rs_lazy_default(void)
{
    static int called;

    if (called)
        return;

    called = 1;
    if (logger_list == NULL)
        rs_add_logger(rs_logger_file, RS_LOG_WARNING, NULL, STDERR_FILENO);
}

/* Heart of the matter */
static void
rs_log_va(int flags, char const *caller_fn_name, char const *fmt, va_list va)


{
    int level = flags & RS_LOG_PRIMASK;
    struct rs_logger_list *l;

    rs_lazy_default();

    if (level <= rs_trace_level)
      for (l = logger_list; l; l = l->next)
          if (level <= l->max_level) {
              /* We need to use va_copy() here, because functions like vsprintf
               * may destructively modify their va_list argument, but we need
               * to ensure that it's still valid next time around the loop. */
              va_list copied_va;
              VA_COPY(copied_va, va);
              l->fn(flags, caller_fn_name,
                    fmt, copied_va, l->private_ptr, l->private_int);
              VA_COPY_END(copied_va);
          }
}


void rs_format_msg(char *buf,
                   size_t buf_len,
                   int flags,
                   const char *fn,
                   const char *fmt,
                   va_list va)
{
    unsigned level = flags & RS_LOG_PRIMASK;
    int len;
    const char *sv;

    *buf = '\0';
    len = 0;

    if (!(flags & RS_LOG_NO_PROGRAM)) {
        strcpy(buf, rs_program_name);
        len = strlen(buf);
    }

    if (!(flags & RS_LOG_NO_PID)) {
        /* You might like to cache the pid, but that would cause trouble when we fork. */
        sprintf(buf+len, "[%d] ", (int) getpid());
    } else if (~flags & RS_LOG_NO_PROGRAM) {
        strcat(buf+len, ": ");
    }
    len = strlen(buf);

    if (!(flags & RS_LOG_NONAME) && fn) {
        sprintf(buf+len, "(%s) ", fn);
        len = strlen(buf);
    }

    sv = rs_severities[level];
    if (*sv) {
        strcpy(buf + len, sv);
        len = strlen(buf);
    }

    vsnprintf(buf + len, buf_len - len, fmt, va);
}



/**
 * Called by a macro, used on platforms where we can't determine the
 * calling function name.
 */
void
rs_log0_nofn(int level, char const *fmt, ...)
{
    va_list         va;

    va_start(va, fmt);
    rs_log_va(level, NULL, fmt, va);
    va_end(va);
}


void rs_log0(int level, char const *fn, char const *fmt, ...)
{
    va_list         va;

    va_start(va, fmt);
    rs_log_va(level, fn, fmt, va);
    va_end(va);
}


void
rs_logger_syslog(int flags, const char *fn, char const *fmt, va_list va,
                 void * UNUSED(private_ptr), int UNUSED(private_int))
{
    /* NOTE NO TRAILING NUL */
    char buf[4090];

    /* you're never going to want program or pid in a syslog message,
     * because it's redundant. */
    rs_format_msg(buf, sizeof buf,
                  flags | RS_LOG_NO_PROGRAM | RS_LOG_NO_PID,
                  fn, fmt, va);
    syslog(flags & RS_LOG_PRIMASK, "%s", buf);
}


void
rs_logger_file(int flags, const char *fn, char const *fmt, va_list va,
               void * UNUSED(private_ptr), int log_fd)
{
    /* NOTE NO TRAILING NUL */
    char buf[4090];
    size_t len;
    ssize_t ret;

    rs_format_msg(buf, sizeof buf, flags, fn, fmt, va);

    len = strlen(buf);
    if (len > (int) sizeof buf - 2)
        len = (int) sizeof buf - 2;
    strcpy(&buf[len], "\n");

    ret = write(log_fd, buf, len + 1);
    if (ret == -1) {
      ret = write(/* stderr */ 2, buf, len + 1);
    }
}



/* ======================================================================== */
/* functions for handling compilers without varargs macros */

/* This is called directly if the machine doesn't allow varargs
 * macros. */
void
rs_log_error_nofn(char const *s, ...)
{
    va_list    va;

    va_start(va, s);
    rs_log_va(RS_LOG_ERR, NULL, s, va);
    va_end(va);
}

/* This is called directly if the machine doesn't allow varargs
 * macros. */
void
rs_log_warning_nofn(char const *s, ...)
{
    va_list    va;

    va_start(va, s);
    rs_log_va(RS_LOG_WARNING, NULL, s, va);
    va_end(va);
}


/* This is called directly if the machine doesn't allow varargs
 * macros. */
void
rs_log_critical_nofn(char const *s, ...)
{
    va_list    va;

    va_start(va, s);
    rs_log_va(RS_LOG_CRIT, NULL, s, va);
    va_end(va);
}

/* This is called directly if the machine doesn't allow varargs
 * macros. */
void
rs_log_info_nofn(char const *s, ...)
{
    va_list    va;

    va_start(va, s);
    rs_log_va(RS_LOG_INFO, NULL, s, va);
    va_end(va);
}


/* This is called directly if the machine doesn't allow varargs
 * macros. */
void
rs_log_notice_nofn(char const *s, ...)
{
    va_list    va;

    va_start(va, s);
    rs_log_va(RS_LOG_NOTICE, NULL, s, va);
    va_end(va);
}


/* This is called directly if the machine doesn't allow varargs
 * macros. */
void
rs_log_trace_nofn(char const *s, ...)
{
    va_list    va;

    va_start(va, s);
    rs_log_va(RS_LOG_DEBUG, NULL, s, va);
    va_end(va);
}


/**
 * Return true if the library contains trace code; otherwise false.
 * If this returns false, then trying to turn trace on will achieve
 * nothing.
 */
int
rs_supports_trace(void)
{
#ifdef DO_RS_TRACE
    return 1;
#else
    return 0;
#endif                /* !DO_RS_TRACE */
}


static char job_summary[4096*4];
void dcc_job_summary_clear(void) {
    job_summary[0] = 0;
    job_summary[sizeof(job_summary) - 1] = '\0';
}

void dcc_job_summary(void) {
    rs_log_notice("%s", job_summary);
}

void dcc_job_summary_append(const char *s) {
    int64_t len = (4096 * 4 - 1) - strlen(job_summary);
    if (len > 0)
        strncat(job_summary, s, len);
}
