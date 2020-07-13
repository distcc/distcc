/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
 * Copyright 2007 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */


            /* 15 Every one that is found shall be thrust
             * through; and every one that is joined unto
             * them shall fall by the sword.
             *        -- Isaiah 13 */


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include <sys/stat.h>

#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "rpc.h"
#include "snprintf.h"


/**
 * @file
 *
 * Very simple RPC-like layer.  Requests and responses are build of
 * little packets each containing a 4-byte ascii token, an 8-byte hex
 * value or length, and optionally data corresponding to the length.
 *
 * 'x' means transmit, and 'r' means receive.
 *
 * This builds on top of io.c and is called by the various routines
 * that handle communication.
 **/


/**
 * Transmit token name (4 characters) and value (32-bit int, as 8 hex
 * characters).
 **/
int dcc_x_token_int(int ofd, const char *token, unsigned param)
{
    char buf[13];
    int shift;
    char *p;
    const char *hex = "0123456789abcdef";

    if (strlen(token) != 4) {
        rs_log_crit("token \"%s\" seems wrong", token);
        return EXIT_PROTOCOL_ERROR;
    }
    memcpy(buf, token, 4);

    /* Quick and dirty int->hex.  The only standard way is to call snprintf
     * (?), which is undesirably slow for such a frequently-called
     * function. */
    for (shift=28, p = &buf[4];
         shift >= 0;
         shift -= 4, p++) {
        *p = hex[(param >> shift) & 0xf];
    }
    buf[12] = '\0';

    rs_trace("send %s", buf);
    return dcc_writex(ofd, buf, 12);
}


/**
 * Send start of a result: DONE <version>
 **/
int dcc_x_result_header(int ofd,
                        enum dcc_protover protover)
{
    return dcc_x_token_int(ofd, "DONE", protover);
}


int dcc_x_cc_status(int ofd, int status)
{
    return dcc_x_token_int(ofd, "STAT", (unsigned) status);
}


int dcc_r_token(int ifd, char *buf)
{
    return dcc_readx(ifd, buf, 4);
}


/**
 * We got a mismatch on a token, which indicates either a bug in distcc, or
 * that somebody (inetd?) is interfering with our network stream, or perhaps
 * some other network problem.  Whatever's happened, a bit more debugging
 * information would be handy.
 **/
int dcc_explain_mismatch(const char *buf,
                         size_t buflen,
                         int ifd)
{
    ssize_t ret;
    char extrabuf[200];
    char *p;
    size_t l;

    memcpy(extrabuf, buf, buflen);

    /* Read a bit more context, and find the printable prefix. */
    ret = read(ifd, extrabuf + buflen, sizeof extrabuf - 1 - buflen);
    if (ret == -1) {
        ret = 0;                /* pah, use what we've got */
    }

    l = buflen + ret;

    extrabuf[l] = '\0';
    for (p = extrabuf; *p; p++)
        if (!(isprint((uint8_t)*p) || *p == ' ' || *p == '\t')) {
            *p = '\0';
            break;
        }

    rs_log_error("error context: \"%s\"", extrabuf);

    return 0;                   /* i just feel really sad... */
}


/**
 * Read a token and value.  The receiver always knows what token name
 * is expected next -- indeed the names are really only there as a
 * sanity check and to aid debugging.
 *
 * @param ifd      fd to read from
 * @param expected 4-char token that is expected to come in next
 * @param val      receives the parameter value
 **/
int dcc_r_token_int(int ifd, const char *expected, unsigned *val)
{
    char buf[13], *bum;
    int ret;

    if (strlen(expected) != 4) {
        rs_log_error("expected token \"%s\" seems wrong", expected);
        return EXIT_PROTOCOL_ERROR;
    }

    if ((ret = dcc_readx(ifd, buf, 12))) {
        rs_log_error("read failed while waiting for token \"%s\"",
                    expected);
        return ret;
    }

    if (memcmp(buf, expected, 4)) {
        rs_log_error("protocol derailment: expected token \"%s\"", expected);
        dcc_explain_mismatch(buf, 12, ifd);
        return EXIT_PROTOCOL_ERROR;
    }

    buf[12] = '\0';             /* terminate */

    *val = strtoul(&buf[4], &bum, 16);
    if (bum != &buf[12]) {
        rs_log_error("failed to parse parameter of token \"%s\"",
                     expected);
        dcc_explain_mismatch(buf, 12, ifd);
        return EXIT_PROTOCOL_ERROR;
    }

    rs_trace("got %s", buf);

    return 0;
}

/**
 * Read a token and value.  Fill in both token and value;
 * unlike dcc_r_token_int this is for the case when we do not know what
 * the next token will be.
 *
 * @param ifd      fd to read from
 * @param token    receives the 4-char token
 * @param val      receives the parameter value
 **/
int dcc_r_sometoken_int(int ifd, char *token, unsigned *val)
{
    char buf[13], *bum;
    int ret;

    if ((ret = dcc_readx(ifd, buf, 12))) {
        rs_log_error("read failed while waiting for some token");
        return ret;
    }

    memcpy(token, buf, 4);
    token[4] = '\0';

    buf[12] = '\0';             /* terminate */

    *val = strtoul(&buf[4], &bum, 16);
    if (bum != &buf[12]) {
        rs_log_error("failed to parse parameter of token \"%s\"",
                     token);
        dcc_explain_mismatch(buf, 12, ifd);
        return EXIT_PROTOCOL_ERROR;
    }

    rs_trace("got %s", buf);

    return 0;
}

/**
 * Read a byte string of length @p l into a newly allocated buffer, returned in @p buf.
 **/
int dcc_r_str_alloc(int fd, unsigned l, char **buf)
{
     char *s;

#if 0
     /* never true  */
     if (l < 0) {
         rs_log_crit("oops, l < 0");
         return EXIT_PROTOCOL_ERROR;
     }
#endif

/*      rs_trace("read %d byte string", l); */

     s = *buf = malloc((size_t) l + 1);
     if (!s)
          rs_log_error("malloc failed");
     if (dcc_readx(fd, s, (size_t) l))
          return EXIT_OUT_OF_MEMORY;

     s[l] = 0;

     return 0;
}


/**
 * Write a token, and then the string @p buf.
 *
 * The length of buf is determined by its nul delimiter, but the \0 is not sent.
 **/
int dcc_x_token_string(int fd,
                       const char *token,
                       const char *buf)
{
    int ret;
    size_t len;

    len = strlen(buf);
    if ((ret = dcc_x_token_int(fd, token, (unsigned) len)))
        return ret;
    if ((ret = dcc_writex(fd, buf, len)))
        return ret;
    rs_trace("send string '%s'", buf);
    return 0;
}


int dcc_r_token_string(int ifd, const char *expect_token,
                       char **p_str)
{
    unsigned a_len;
    int ret;

    if ((ret = dcc_r_token_int(ifd, expect_token, &a_len)))
        return ret;

    if ((ret = dcc_r_str_alloc(ifd, a_len, p_str)))
        return ret;

    rs_trace("got '%s'", *p_str);

    return 0;
}

/**
 * Read an argv-type vector from the network.
 **/
int dcc_r_argv(int ifd,
               const char *argc_token,
               const char *argv_token,
               /*@out@*/ char ***argv)
{
    unsigned i;
    unsigned argc;
    char **a;
    int ret;

    *argv = NULL;

    if (dcc_r_token_int(ifd, argc_token, &argc))
        return EXIT_PROTOCOL_ERROR;

    rs_trace("reading %d arguments from job submission", argc);

    /* Have to make the argv one element too long, so that it can be
     * terminated by a null element. */
    *argv = a = (char **) calloc((size_t) argc+1, sizeof a[0]);
    if (a == NULL) {
        rs_log_error("alloc failed");
        return EXIT_OUT_OF_MEMORY;
    }
    a[argc] = NULL;

    for (i = 0; i < argc; i++) {
        if ((ret = dcc_r_token_string(ifd, argv_token, &a[i])))
            return ret;

        rs_trace("argv[%d] = \"%s\"", i, a[i]);
    }

    dcc_trace_argv("got arguments", a);

    return 0;
}
