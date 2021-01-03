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

#include <setjmp.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

/* util.c */
int dcc_timecmp(struct timeval a, struct timeval b);
int dcc_getcurrentload(void);
void dcc_getloadavg(double loadavg[3]);
int argv_contains(char **argv, const char *s);
int dcc_redirect_fd(int, const char *fname, int);
int str_startswith(const char *head, const char *worm);
char *dcc_gethostname(void);
void dcc_exit(int exitcode) NORETURN;
int dcc_getenv_bool(const char *name, int def_value);
int set_cloexec_flag (int desc, int value);
int dcc_ignore_sigpipe(int val);
int dcc_remove_if_exists(const char *fname);
int dcc_trim_path(const char *compiler_name);
int dcc_set_path(const char *newpath);
char *dcc_abspath(const char *path, int path_len);
int dcc_get_dns_domain(const char **domain_name);

#define str_equal(a, b) (!strcmp((a), (b)))


void dcc_get_proc_stats(int *num_D, int *max_RSS, char **max_RSS_name);
void dcc_get_disk_io_stats(int *n_reads, int *n_writes);

int dcc_which(const char *cmd, char **out);

int dcc_dup_part(const char **psrc, char **pdst, const char *sep);

#ifndef HAVE_STRLCPY
size_t strlcpy(char *d, const char *s, size_t bufsize);
#endif

int dcc_tokenize_string(const char *in, char ***argv_ptr);

#ifndef HAVE_GETLINE
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif

int not_sd_is_socket(int fd, int family, int type, int listening);
