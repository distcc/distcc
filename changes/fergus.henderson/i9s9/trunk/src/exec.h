/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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

/* exec.c */
extern const int timeout_null_fd;
extern int dcc_job_lifetime;

int dcc_redirect_fds(const char *stdin_file,
                     const char *stdout_file,
                     const char *stderr_file);

int dcc_spawn_child(char **argv, pid_t *pidptr,
                    const char *, const char *, const char *);

/* if in_fd is timeout_null_fd, means this parameter is not used */
int dcc_collect_child(const char *what, pid_t pid,
                      int *wait_status, int in_fd);
int dcc_critique_status(int s,
                        const char *,
                        const char *,
                        struct dcc_hostdef *host,
                        int verbose);
void dcc_note_execution(struct dcc_hostdef *host, char **argv);

int dcc_new_pgrp(void);
void dcc_reset_signal(int whichsig);

#ifndef W_EXITCODE
#  define W_EXITCODE(exit, signal) ((exit)<<8 | (signal))
#endif
