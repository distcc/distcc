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


              /* The dead cry out with joy when their books are reprinted
               */



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
#include <sys/select.h>

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
#include "stats.h"

void dcc_manage_kids(int listen_fd);
static void dcc_sigchld_handler(int sig);
static void dcc_create_kids(int listen_fd);
static int dcc_preforked_child(int listen_fd);

/**
 * Main loop for the parent process with the new preforked implementation.
 * The parent is just responsible for keeping a pool of children and they
 * accept connections themselves.
 **/
int dcc_preforking_parent(int listen_fd)
{
    int ret;
    /* use sigaction instead of signal() because we need persistant handler, not oneshot */
    struct sigaction act_child;
    memset(&act_child, 0, sizeof act_child);
    act_child.sa_handler = dcc_sigchld_handler;
    sigaction(SIGCHLD, &act_child, NULL);

    if (arg_stats) {

        ret = dcc_stats_init();
        if (ret) return ret;

        dcc_create_kids(listen_fd);

        /* Start the stats collection and web server */
        return dcc_stats_server(listen_fd);
    } else {
        while (1) {
            dcc_create_kids(listen_fd);

            /* wait for any children to exit, and then start some more */
            dcc_reap_kids(TRUE);

            /* Another little safety brake here: since children should not exit
             * too quickly, pausing before starting them should be harmless. */
            sleep(1);
        }
    }
}


static void dcc_sigchld_handler(int UNUSED(sig)) {
    /* Do nothing.  Only here to break out of select() in dcc_stats_server()
     * and select() in dcc_collect_child(). */
}


/**
 * Functions in the parent can call this to clean up and maintain the pool of
 * children
 **/
void dcc_manage_kids(int listen_fd) {
    dcc_reap_kids(FALSE);
    dcc_create_kids(listen_fd);
}

/**
 * Fork children until we have dcc_max_kids of them
 **/
static void dcc_create_kids(int listen_fd) {
    pid_t kid;

    while (dcc_nkids < dcc_max_kids) {
        if ((kid = fork()) == -1) {
            rs_log_error("fork failed: %s", strerror(errno));
            dcc_exit(EXIT_OUT_OF_MEMORY); /* probably */
        } else if (kid == 0) {
            dcc_stats_init_kid();
            dcc_exit(dcc_preforked_child(listen_fd));
        } else {
            /* in parent */
            ++dcc_nkids;
            rs_trace("up to %d children", dcc_nkids);
        }

        /* Don't start them too quickly, or we might overwhelm a machine
         * that's having trouble. */
        sleep(1);
    }
}



/**
 * Fork a child to repeatedly accept and handle incoming connections.
 *
 * To protect against leaks, we quit after 50 requests and let the parent
 * recreate us.
 **/
static int dcc_preforked_child(int listen_fd)
{
    int ireq;
    const int child_lifetime = 50;

    for (ireq = 0; ireq < child_lifetime; ireq++) {
        int acc_fd;
        struct dcc_sockaddr_storage cli_addr;
        socklen_t cli_len;

        cli_len = sizeof cli_addr;

    /* Cancel any previously scheduled alarm */
        if (dcc_job_lifetime)
            alarm(0);

        do {
            acc_fd = accept(listen_fd, (struct sockaddr *) &cli_addr,
                            &cli_len);
        } while (acc_fd == -1 && errno == EINTR);

        /* Kill this process if the compile job takes too long.
         * The synchronous timeout should happen first, so this alarm
         * should fire only if the client stops transferring network data without disconnecting.
         */
        if (dcc_job_lifetime)
            alarm(dcc_job_lifetime+30);


        if (acc_fd == -1) {
            rs_log_error("accept failed: %s", strerror(errno));
            dcc_exit(EXIT_CONNECT_FAILED);
        }

        dcc_stats_event(STATS_TCP_ACCEPT);

        dcc_service_job(acc_fd, acc_fd,
                           (struct sockaddr *) &cli_addr, cli_len);

        dcc_close(acc_fd);
    }

    rs_log_info("worn out");

    return 0;
}
