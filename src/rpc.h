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

                /* His hand is stretched out, and who shall turn it back?
                 * -- Isaiah 14:27 */

#ifndef __DISTCC_RPC_H__
#define __DISTCC_RPC_H__

int dcc_x_result_header(int ofd, enum dcc_protover);
int dcc_r_result_header(int ofd, enum dcc_protover);

int dcc_x_cc_status(int, int);
int dcc_r_cc_status(int, int *);

int dcc_x_token_int(int ofd, const char *token, unsigned param);
int dcc_r_token_int(int ifd, const char *expected, unsigned int *val);

int dcc_x_token_string(int fd,
                       const char *token,
                       const char *buf);

int dcc_r_token_string(int ifd, const char *expect_token,
                       char **p_str);
int dcc_r_sometoken_int(int ifd, char *token, unsigned *val);

int dcc_explain_mismatch(const char *buf, size_t buflen, int ifd);

/* srvrpc.c */
int dcc_r_request_header(int ifd, enum dcc_protover *);
int dcc_r_argv(int ifd,
               const char *argc_token,
               const char *argv_token,
               /*@out@*/ char ***argv);


#endif