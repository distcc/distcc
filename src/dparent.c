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

                    /* Near is thy forgetfulness of all things; and near the
                     * forgetfulness of thee by all.
                     *                 -- Marcus Aurelius
                     */


/**
 * @file
 *
 * Daemon parent.  Accepts connections, forks, etc.
 *
 * @todo Quite soon we need load management.  Basically when we think
 * we're "too busy" we should stop accepting connections.  This could
 * be because of the load average, or because too many jobs are
 * running, or perhaps just because of a signal from the administrator
 * of this machine.  In that case we want to do a blocking wait() to
 * find out when the current jobs are done, or perhaps a sleep() if
 * we're waiting for the load average to go back down.  However, we
 * probably ought to always keep at least one job running so that we
 * can make progress through the queue.  If you don't want any work
 * done, you should kill the daemon altogether.
 **/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "exitcode.h"
#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "dopt.h"
#include "exec.h"
#include "srvnet.h"
#include "types.h"
#include "daemon.h"
#include "netutil.h"
#include "zeroconf.h"
#ifdef HAVE_GSSAPI
#include "auth.h"
#endif

static void dcc_nofork_parent(int listen_fd) NORETURN;
static void dcc_detach(void);
static void dcc_save_pid(pid_t);
int dcc_nkids = 0;


/**
 * In forking or prefork mode, the maximum number of connections we want to
 * allow at any time.
 **/
int dcc_max_kids = 0;


/**
 * Be a standalone server, with responsibility for sockets and forking
 * children.  Puts the daemon in the background and detaches from the
 * controlling tty.
 **/
int dcc_standalone_server(void)
{
    int listen_fd;
    int n_cpus;
    int ret;
#ifdef HAVE_AVAHI
    void *avahi = NULL;
#endif

    if ((ret = dcc_socket_listen(arg_port, &listen_fd, opt_listen_addr)) != 0)
        return ret;

    dcc_defer_accept(listen_fd);

    set_cloexec_flag(listen_fd, 1);

    if (dcc_ncpus(&n_cpus) == 0)
        rs_log_info("%d CPU%s online on this server", n_cpus, n_cpus == 1 ? "" : "s");

    /* By default, allow one job per CPU, plus two for the pot.  The extra
     * ones are started to allow for a bit of extra concurrency so that the
     * machine is not idle waiting for disk or network IO. */
    if (arg_max_jobs)
        dcc_max_kids = arg_max_jobs;
    else
        dcc_max_kids = 2 + n_cpus;

    rs_log_info("allowing up to %d active jobs", dcc_max_kids);

    if (!opt_no_detach) {
        /* Don't go into the background until we're listening and
         * ready.  This is useful for testing -- when the daemon
         * detaches, we know we can go ahead and try to connect.  */
        dcc_detach();
    } else {
        /* Still create a new process group, even if not detached */
        rs_trace("not detaching");
        if ((ret = dcc_new_pgrp()) != 0)
            return ret;
        dcc_save_pid(getpid());
    }

    /* Don't catch signals until we've detached or created a process group. */
    dcc_daemon_catch_signals();

#ifdef HAVE_AVAHI
    /* Zeroconf registration */
    if (opt_zeroconf) {
        if (!(avahi = dcc_zeroconf_register((uint16_t) arg_port, n_cpus, dcc_max_kids)))
            return EXIT_CONNECT_FAILED;
    }
#endif

    /* This is called in the master daemon, whether that is detached or
     * not.  */
    dcc_master_pid = getpid();

    if (opt_no_fork) {
        dcc_log_daemon_started("non-forking daemon");
        dcc_nofork_parent(listen_fd);
        ret = 0;
    } else {
        dcc_log_daemon_started("preforking daemon");
        ret = dcc_preforking_parent(listen_fd);
    }

#ifdef HAVE_AVAHI
    /* Remove zeroconf registration */
    if (opt_zeroconf) {
        if (dcc_zeroconf_unregister(avahi) != 0)
            return EXIT_CONNECT_FAILED;
    }
#endif

    return ret;
}



static void dcc_log_child_exited(pid_t kid,
                                 int status)
{
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        int severity = sig == SIGTERM ? RS_LOG_INFO : RS_LOG_ERR;

        rs_log(severity, "child %d: signal %d (%s)", (int) kid, sig,
               WCOREDUMP(status) ? "core dumped" : "no core");
    } else if (WIFEXITED(status)) {
        rs_log_info("child %d exited: exit status %d",
                    (int) kid, WEXITSTATUS(status));
    }
}



/**
 * @sa dcc_wait_child(), which is used by a process that wants to do a blocking
 * wait for some task like cpp or gcc.
 *
 * @param must_reap If True, don't return until at least one child has been
 * collected.  Used when e.g. all our process slots are full.  In either case
 * we keep going until all outstanding zombies are collected.
 *
 * FIXME: Are blocking waits meant to collect all of them, or just one?  At
 * the moment it waits until all children have exited.
 **/
void dcc_reap_kids(int must_reap)
{
    while (1) {
        int status;
        pid_t kid;

        kid = waitpid(WAIT_ANY, &status, must_reap ? 0 : WNOHANG);
        if (kid == 0) {
            /* nobody has exited */
            break;
        } else if (kid != -1) {
            /* child exited */
            --dcc_nkids;
            rs_trace("down to %d children", dcc_nkids);

            dcc_log_child_exited(kid, status);
        } else if (errno == ECHILD) {
            /* No children left?  That's ok, we'll go back to waiting
             * for new connections. */
            break;
        } else if (errno == EINTR) {
            /* If we got a SIGTERM or something, then on the next pass
             * through the loop we'll find no children done, and we'll
             * return to the top loop at which point we'll exit.  So
             * no special action is required here. */
            continue;       /* loop again */
        } else {
            rs_log_error("wait failed: %s", strerror(errno));
            /* e.g. too many open files; nothing we can do */
            dcc_exit(EXIT_DISTCC_FAILED);
        }

        /* If there are more children keep looking, but don't block once we've
         * collected at least one. */
        must_reap = FALSE;
    }
}


/**
 * Main loop for no-fork mode.
 *
 * Much slower and may leak.  Should only be used when you want to run gdb on
 * distccd.
 **/
static void dcc_nofork_parent(int listen_fd)
{
    while (1) {
        int acc_fd;
        struct dcc_sockaddr_storage cli_addr;
        socklen_t cli_len;

        rs_log_info("waiting to accept connection");

        cli_len = sizeof cli_addr;
        acc_fd = accept(listen_fd,
                        (struct sockaddr *) &cli_addr, &cli_len);
        if (acc_fd == -1 && errno == EINTR) {
            ;
        }  else if (acc_fd == -1) {
            rs_log_error("accept failed: %s", strerror(errno));

#ifdef HAVE_GSSAPI
            if (dcc_auth_enabled) {
                dcc_gssapi_release_credentials();

                if (opt_blacklist_enabled || opt_whitelist_enabled) {
                    dcc_gssapi_free_list();
	            }
            }
#endif
            dcc_exit(EXIT_CONNECT_FAILED);
        } else {
            dcc_service_job(acc_fd, acc_fd, (struct sockaddr *) &cli_addr, cli_len);
            dcc_close(acc_fd);
        }
    }
}


/**
 * Save the pid of the child process into the pid file, if any.
 *
 * This is called from the parent so that we have the invariant that
 * the pid file exists before the parent exits, hich is useful for
 * test harnesses.  Otherwise, there is a race where the parent has
 * exited and they try to go ahead and read the child's pid, but it's
 * not there yet.
 **/
static void dcc_save_pid(pid_t pid)
{
    FILE *fp;

    if (!arg_pid_file)
        return;

    if (!(fp = fopen(arg_pid_file, "wt"))) {
        rs_log_error("failed to open pid file: %s: %s", arg_pid_file,
                     strerror(errno));
        return;
    }

    fprintf(fp, "%ld\n", (long) pid);

    if (fclose(fp) == -1) {
        rs_log_error("failed to close pid file: %s: %s", arg_pid_file,
                     strerror(errno));
        return;
    }

    atexit(dcc_remove_pid);
}


/**
 * Remove our pid file on exit.
 *
 * Must be reentrant -- called from signal handler.
 **/
void dcc_remove_pid(void)
{
    if (!arg_pid_file)
        return;

                                /* this may be called from a signal handler,
                                   and syslog is not safe from signal handler */
    if (unlink(arg_pid_file) && !rs_trace_syslog) {
        rs_log_warning("failed to remove pid file %s: %s",
                       arg_pid_file, strerror(errno));
    }
}


/**
 * Become a daemon, discarding the controlling terminal.
 *
 * Borrowed from rsync.
 *
 * This function returns in the child, but not in the parent.
 **/
static void dcc_detach(void)
{
    int i;
    pid_t pid;
    pid_t sid;

    dcc_ignore_sighup();

    if ((pid = fork()) == -1) {
        rs_log_error("fork failed: %s", strerror(errno));
        exit(EXIT_DISTCC_FAILED);
    } else if (pid != 0) {
        /* In the parent.  This guy is about to go away so as to
         * detach from the controlling process, but first save the
         * child's pid. */
        dcc_save_pid(pid);
        _exit(0);
    }

    /* This is called in the detached child */

    /* detach from the terminal */
#ifdef HAVE_SETSID
    if ((sid = setsid()) == -1) {
        rs_log_error("setsid failed: %s", strerror(errno));
    } else {
        rs_trace("setsid to session %d", (int) sid);
    }
#else /* no HAVE_SETSID */
#ifdef TIOCNOTTY
    i = open("/dev/tty", O_RDWR);
    if (i >= 0) {
        ioctl(i, (int) TIOCNOTTY, (char *)0);
        close(i);
    }
#endif /* TIOCNOTTY */
#endif /* not HAVE_SETSID */

    /* make sure that stdin, stdout an stderr don't stuff things
       up (library functions, for example) */
    for (i=0;i<3;i++) {
        close(i);
        open("/dev/null", O_RDWR);
    }

    /* If there's a lifetime limit on this server (for testing) then it needs
     * to apply after detaching as well. */
    dcc_set_lifetime();
}
