/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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


/* daemon.c */
extern const char *dcc_daemon_wd;

int dcc_refuse_root(void);
int dcc_set_lifetime(void);
int dcc_log_daemon_started(const char *role);


/* dsignal.c */
void dcc_ignore_sighup(void);
void dcc_daemon_catch_signals(void);

/* dparent.c */
int dcc_standalone_server(void);
void dcc_remove_pid(void);
void dcc_reap_kids(int must_reap);


/* prefork.c */
int dcc_preforking_parent(int listen_fd);


/* serve.c */
struct sockaddr;
int dcc_service_job(int in_fd, int out_fd, struct sockaddr *, int);

/* setuid.c */
int dcc_discard_root(void);
extern const char *opt_user;


extern int dcc_max_kids;
extern int dcc_nkids;

extern volatile pid_t dcc_master_pid;
