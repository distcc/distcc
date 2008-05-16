/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4 fill-column: 78 -*-
 * 
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


/* dopt.c */
extern struct dcc_allow_list *opt_allowed;
int distccd_parse_options(int argc, const char *argv[]);

extern int arg_port;
extern int arg_stats;
extern int arg_stats_port;
extern int opt_log_level_num;
extern int arg_max_jobs;
extern const char *arg_pid_file;
extern int opt_no_fork;
extern int opt_no_prefork;
extern int opt_no_detach;
extern int opt_daemon_mode, opt_inetd_mode;
extern int opt_job_lifetime;
extern const char *arg_log_file;
extern int opt_no_fifo;
extern int opt_log_stderr;
extern int opt_lifetime;
extern char *opt_listen_addr;
extern int opt_niceness;

#ifdef HAVE_AVAHI
extern int opt_zeroconf;
#endif
