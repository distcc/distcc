/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
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

                /*
                 * A servant will not be corrected by words: for
                 * though he understand he will not answer.
                 *      -- Proverbs 29:19
                 */

/**
 * @file
 * @brief Daemon signal handling.
 *
 * Signals are handled differently in the daemon parent and its children.
 *
 * When the parent is killed, the entire process group is shut down, and the
 * pid file (if any) is removed.
 *
 * For both cases any temporary files created by the process are removed.
 **/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include "exitcode.h"
#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "dopt.h"
#include "exec.h"
#include "daemon.h"
#ifdef HAVE_GSSAPI
#include "auth.h"
#endif


/* This stores the pid of the parent daemon.  It's used to make sure
 * that we only run the whole-group cleanup from inside the parent.
 * Remains 0 before parent initialization is complete and when run
 * from inetd. */
volatile pid_t dcc_master_pid = 0;

static RETSIGTYPE dcc_daemon_terminate(int);

/**
 * Catch all relevant termination signals.  Set up in parent and also
 * applies to children.
 **/
void dcc_daemon_catch_signals(void)
{
    /* SIGALRM is caught to allow for built-in timeouts when running test
     * cases. */

    signal(SIGTERM, &dcc_daemon_terminate);
    signal(SIGINT, &dcc_daemon_terminate);
    signal(SIGHUP, &dcc_daemon_terminate);
    signal(SIGALRM, &dcc_daemon_terminate);
}



/**
 * Ignore hangup signal.
 *
 * This is only used in detached mode to make sure the daemon does not
 * quit when whoever started it closes their terminal.  In nondetached
 * mode, the signal is logged and causes an exit as normal.
 **/
void dcc_ignore_sighup(void)
{
    signal(SIGHUP, SIG_IGN);

    rs_trace("ignoring SIGHUP");
}



/**
 * Just log, remove pidfile, and exit.
 *
 * Called when a daemon gets a fatal signal.
 *
 * Some cleanup is done only if we're the master/parent daemon.
 **/
static RETSIGTYPE dcc_daemon_terminate(int whichsig)
{
    int am_parent;

    /* Make sure to remove handler before re-raising signal */
    signal(whichsig, SIG_DFL);

    am_parent = getpid() == dcc_master_pid;

    /* syslog is not safe from a signal handler */
    if (am_parent && !rs_trace_syslog) {
#ifdef HAVE_STRSIGNAL
        rs_log_info("%s", strsignal(whichsig));
#else
        rs_log_info("terminated by signal %d", whichsig);
#endif
    }

    dcc_cleanup_tempfiles_from_signal_handler();

    if (am_parent) {
        dcc_remove_pid();

        /* kill whole group */
        kill(0, whichsig);
    }

    raise(whichsig);

/* malloc() stuff not safe from a signal handler, but keep this here in case
   this memory non-leak is to be fixed in the future. */
/*
#ifdef HAVE_GSSAPI
    if (dcc_auth_enabled) {
        dcc_gssapi_release_credentials();

        if (opt_blacklist_enabled || opt_whitelist_enabled) {
            dcc_gssapi_free_list();
        }
    }
#endif*/
}
