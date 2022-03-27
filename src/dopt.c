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


/* dopt.c -- Parse and apply server options. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <popt.h>
#include <limits.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "types.h"
#include "distcc.h"
#include "trace.h"
#include "dopt.h"
#include "exitcode.h"
#include "daemon.h"
#include "access.h"
#include "exec.h"

int opt_niceness = 5;           /* default */

#ifdef HAVE_LINUX
int opt_oom_score_adj = INT_MIN; /* default is not to change */
#endif

/**
 * Number of children running jobs on this machine.  If zero (recommended),
 * then dynamically set from the number of CPUs.
 **/
int arg_max_jobs = 0;

#ifdef HAVE_GSSAPI
/* If true perform GSS-API based authentication. */
int opt_auth_enabled = 0;
/* Control access through a specified list file. */
int opt_blacklist_enabled = 0;
int opt_whitelist_enabled = 0;
const char *arg_list_file = NULL;
#endif

int arg_port = DISTCC_DEFAULT_PORT;
int arg_stats = DISTCC_DEFAULT_STATS_ENABLED;
int arg_stats_port = DISTCC_DEFAULT_STATS_PORT;

/** If true, serve all requests directly from listening process
    without forking.  Better for debugging. **/
int opt_no_fork = 0;

int opt_daemon_mode = 0;
int opt_inetd_mode = 0;
int opt_no_fifo = 0;

/** If non-NULL, listen on only this address. **/
char *opt_listen_addr = NULL;

struct dcc_allow_list *opt_allowed = NULL;

int opt_allow_private = 0;

/**
 * If true, don't detach from the parent.  This is probably necessary
 * for use with daemontools or other monitoring programs, and is also
 * used by the test suite.
 **/
int opt_no_detach = 0;

int opt_log_stderr = 0;

int opt_log_level_num = RS_LOG_NOTICE;

/**
 * If true, do not check if a link to distcc exists in /usr/lib/distcc
 * for every program executed remotely.
 **/
int opt_enable_tcp_insecure = 0;

/**
 * Daemon exits after this many seconds.  Intended mainly for testing, to make
 * sure daemons don't persist for too long.
 */
int opt_lifetime = 0;

const char *arg_pid_file = NULL;
const char *arg_log_file = NULL;

int opt_job_lifetime = 0;

/* Enumeration values for options that don't have single-letter name.  These
 * must be numerically above all the ascii letters. */
enum {
    opt_log_to_file = 300,
    opt_log_level
};

#ifdef HAVE_AVAHI
/* Flag for enabling/disabling Zeroconf using Avahi */
int opt_zeroconf = 0;
#endif


static const char *dcc_private_networks[] = {"192.168.0.0/16",
                                             "10.0.0.0/8",
                                             "172.16.0.0/12",
                                             "127.0.0.0/8",

                                             "fe80::/10",
                                              "fc00::/7",
                                              "::1/128"};

const struct poptOption options[] = {
    { "allow", 'a',      POPT_ARG_STRING, 0, 'a', 0, 0 },
    { "allow-private", 0,POPT_ARG_NONE, &opt_allow_private, 0, 0, 0 },
#ifdef HAVE_GSSAPI
    { "auth", 0,	 POPT_ARG_NONE, &opt_auth_enabled, 'A', 0, 0 },
    { "blacklist", 0,    POPT_ARG_STRING, &arg_list_file, 'b', 0, 0 },
#endif
    { "jobs", 'j',       POPT_ARG_INT, &arg_max_jobs, 'j', 0, 0 },
    { "daemon", 0,       POPT_ARG_NONE, &opt_daemon_mode, 0, 0, 0 },
    { "help", 0,         POPT_ARG_NONE, 0, '?', 0, 0 },
    { "inetd", 0,        POPT_ARG_NONE, &opt_inetd_mode, 0, 0, 0 },
    { "lifetime", 0,     POPT_ARG_INT, &opt_lifetime, 0, 0, 0 },
    { "listen", 0,       POPT_ARG_STRING, &opt_listen_addr, 0, 0, 0 },
    { "log-file", 0,     POPT_ARG_STRING, &arg_log_file, 0, 0, 0 },
    { "log-level", 0,    POPT_ARG_STRING, 0, opt_log_level, 0, 0 },
    { "log-stderr", 0,   POPT_ARG_NONE, &opt_log_stderr, 0, 0, 0 },
    { "job-lifetime", 0, POPT_ARG_INT, &opt_job_lifetime, 'l', 0, 0 },
    { "nice", 'N',       POPT_ARG_INT,  &opt_niceness,  0, 0, 0 },
    { "no-detach", 0,    POPT_ARG_NONE, &opt_no_detach, 0, 0, 0 },
    { "no-fifo", 0,      POPT_ARG_NONE, &opt_no_fifo, 0, 0, 0 },
    { "no-fork", 0,      POPT_ARG_NONE, &opt_no_fork, 0, 0, 0 },
#ifdef HAVE_LINUX
    { "oom-score-adj",0, POPT_ARG_INT,  &opt_oom_score_adj, 0, 0, 0 },
#endif
    { "pid-file", 'P',   POPT_ARG_STRING, &arg_pid_file, 0, 0, 0 },
    { "port", 'p',       POPT_ARG_INT, &arg_port, 0, 0, 0 },
#ifdef HAVE_GSSAPI
    { "show-principal", 0,	 POPT_ARG_NONE, 0, 'P', 0, 0 },
#endif
    { "user", 0,         POPT_ARG_STRING, &opt_user, 'u', 0, 0 },
    { "verbose", 0,      POPT_ARG_NONE, 0, 'v', 0, 0 },
    { "version", 0,      POPT_ARG_NONE, 0, 'V', 0, 0 },
#ifdef HAVE_GSSAPI
    { "whitelist", 0,    POPT_ARG_STRING, &arg_list_file, 'w', 0, 0 },
#endif
    { "wizard", 'W',     POPT_ARG_NONE, 0, 'W', 0, 0 },
    { "stats", 0,        POPT_ARG_NONE, &arg_stats, 0, 0, 0 },
    { "stats-port", 0,   POPT_ARG_INT, &arg_stats_port, 0, 0, 0 },
#ifdef HAVE_AVAHI
    { "zeroconf", 0,     POPT_ARG_NONE, &opt_zeroconf, 0, 0, 0 },
#endif
    { "make-me-a-botnet", 0, POPT_ARG_NONE, &opt_enable_tcp_insecure, 0, 0, 0 },
    { "enable-tcp-insecure", 0, POPT_ARG_NONE, &opt_enable_tcp_insecure, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0 }
};

static void distccd_show_usage(void)
{
    dcc_show_version("distccd");
    printf (
"Usage:\n"
"   distccd [OPTIONS]\n"
"\n"
"Options:\n"
"    --help                     explain usage and exit\n"
"    --version                  show version and exit\n"
#ifdef HAVE_GSSAPI
"    --show-principal           show current GSS-API principal and exit\n"
#endif
"    -P, --pid-file FILE        save daemon process id to file\n"
"    -N, --nice LEVEL           lower priority, 20=most nice\n"
#ifdef HAVE_LINUX
"    --oom-score-adj ADJ        set OOM score adjustment, -1000 to 1000\n"
#endif
"    --user USER                if run by root, change to this persona\n"
"    --jobs, -j LIMIT           maximum tasks at any time\n"
"    --job-lifetime SECONDS     maximum lifetime of a compile request\n"
"  Networking:\n"
"    -p, --port PORT            TCP port to listen on\n"
"    --listen ADDRESS           IP address to listen on\n"
"    -a, --allow IP[/BITS]      client address access control\n"
#ifdef HAVE_GSSAPI
"    --auth                     enable GSS-API based mutual authenticaton\n"
"    --blacklist=FILE           control client access through a blacklist\n"
"    --whitelist=FILE           control client access through a whitelist\n"
#endif
"    --stats                    enable statistics reporting via HTTP server\n"
"    --stats-port PORT          TCP port to listen on for statistics requests\n"
#ifdef HAVE_AVAHI
"    --zeroconf                 register via mDNS/DNS-SD\n"
#endif
"  Debug and trace:\n"
"    --log-level=LEVEL          set detail level for log file\n"
"      levels: critical, error, warning, notice, info, debug\n"
"    --verbose                  set log level to \"debug\"\n"
"    --no-detach                don't detach from parent (for daemontools, etc)\n"
"    --log-file=FILE            send messages here instead of syslog\n"
"    --log-stderr               send messages to stderr\n"
"    --wizard                   for running under gdb\n"
"  Mode of operation:\n"
"    --inetd                    serve client connected to stdin\n"
"    --daemon                   bind and listen on socket\n"
"\n"
"distccd runs either from inetd or as a standalone daemon to compile\n"
"files submitted by the distcc client.\n"
"\n"
"distccd should only run on trusted networks.\n"
);
}

#ifdef HAVE_GSSAPI
/*
 * Print out the name of the principal.
 */
static void dcc_gssapi_show_principal(void) {
    char *princ_env_val = NULL;

    if ((princ_env_val = getenv("DISTCCD_PRINCIPAL"))) {
	    printf("Principal is\t: %s\n", princ_env_val);
    } else {
        printf("Principal\t: Not Set\n");
    }
}
#endif

int distccd_parse_options(int argc, const char **argv)
{
    poptContext po;
    int po_err, exitcode;
    struct dcc_allow_list *new;

    po = poptGetContext("distccd", argc, argv, options, 0);

    while ((po_err = poptGetNextOpt(po)) != -1) {
        switch (po_err) {
        case '?':
            distccd_show_usage();
            exitcode = 0;
            goto out_exit;

        case 'a': {
            /* TODO: Allow this to be a hostname, which is resolved to an address. */
            /* TODO: Split this into a small function. */
            new = malloc(sizeof *new);
            if (!new) {
                rs_log_crit("malloc failed");
                exitcode = EXIT_OUT_OF_MEMORY;
                goto out_exit;
            }
            new->next = opt_allowed;
            opt_allowed = new;
            if ((exitcode = dcc_parse_mask(poptGetOptArg(po), &new->addr, &new->mask)))
                goto out_exit;
        }
            break;

#ifdef HAVE_GSSAPI
	    /* Set the flag to indicate that authentication is requested. */
        case 'A': {
	        if (opt_auth_enabled < 0) {
		        opt_auth_enabled = 0;
            }

	        dcc_auth_enabled = opt_auth_enabled;
	        break;
        }

        case 'b': {
            if (opt_whitelist_enabled) {
	            rs_log_error("Can't specify both --whitelist and --blacklist.");
                exitcode = EXIT_BAD_ARGUMENTS;
                goto out_exit;
	        } else {
		        opt_blacklist_enabled = 1;
	        }

	        break;
	    }
#endif

        case 'j':
            if (arg_max_jobs < 1 ) {
                rs_log_error("--jobs argument must be more than 0");
                exitcode = EXIT_BAD_ARGUMENTS;
                goto out_exit;
            }
            break;

        case 'l':
            if (opt_job_lifetime < 0) {
                opt_job_lifetime = 0;
            }
            dcc_job_lifetime = opt_job_lifetime;
            break;

#ifdef HAVE_GSSAPI
        case 'P': {
	        dcc_gssapi_show_principal();
	        exitcode = 0;
            goto out_exit;
        }
#endif

        case 'u':
            if (getuid() != 0 && geteuid() != 0) {
                rs_log_warning("--user is ignored when distccd is not run by root");
                /* continue */
            }
            break;

        case 'V':
            dcc_show_version("distccd");
            exitcode = EXIT_SUCCESS;
            goto out_exit;

        case opt_log_level:
            {
                int level;
                const char *level_name;

                level_name = poptGetOptArg(po);
                level = rs_loglevel_from_name(level_name);
                if (level == -1) {
                    rs_log_warning("invalid --log-level argument \"%s\"",
                                   level_name);
                } else {
                    rs_trace_set_level(level);
                    opt_log_level_num = level;
                }
            }
            break;

        case 'v':
            rs_trace_set_level(RS_LOG_DEBUG);
            opt_log_level_num = RS_LOG_DEBUG;
            break;

#ifdef HAVE_GSSAPI
	    case 'w': {
            if (opt_blacklist_enabled) {
	            rs_log_error("Can't specify both --blacklist and --whitelist.");
                exitcode = EXIT_BAD_ARGUMENTS;
                goto out_exit;
	        } else {
		        opt_whitelist_enabled = 1;
	        }

	        break;
	    }
#endif

        case 'W':
            /* catchall for running under gdb */
            opt_log_stderr = 1;
            opt_daemon_mode = 1;
            opt_no_detach = 1;
            opt_no_fork = 1;
            opt_no_fifo = 1;
            rs_trace_set_level(RS_LOG_DEBUG);
            opt_log_level_num = RS_LOG_DEBUG;
            break;

        default:                /* bad? */
            rs_log(RS_LOG_NONAME|RS_LOG_ERR|RS_LOG_NO_PID, "%s: %s",
                   poptBadOption(po, POPT_BADOPTION_NOALIAS),
                   poptStrerror(po_err));
            exitcode = EXIT_BAD_ARGUMENTS;
            goto out_exit;
        }
    }

    if (opt_allow_private) {
        int i;
        for (i = 0;i<6;i++) {
            new = malloc(sizeof *new);
            if (!new) {
                rs_log_crit("malloc failed");
                exitcode = EXIT_OUT_OF_MEMORY;
                goto out_exit;
            }
            new->next = opt_allowed;
            opt_allowed = new;
            if ((exitcode = dcc_parse_mask(dcc_private_networks[i], &new->addr, &new->mask)))
                goto out_exit;
        }
    }

    poptFreeContext(po);
    return 0;

    out_exit:
    poptFreeContext(po);
    exit(exitcode);
}
