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


                /* I put the shotgun in an Adidas bag and padded it
                 * out with four pairs of tennis socks, not my style
                 * at all, but that was what I was aiming for: If they
                 * think you're crude, go technical; if they think
                 * you're technical, go crude.  I'm a very technical
                 * boy.  So I decided to get as crude as possible.
                 * These days, though, you have to be pretty technical
                 * before you can even aspire to crudeness.
                 *              -- William Gibson, "Johnny Mnemonic" */


/**
 * @file
 *
 * Routines to decide on which machine to run a distributable job.
 *
 * The current algorithm (new in 1.2 and subject to change) is as follows.
 *
 * CPU lock is held until the job is complete.
 *
 * Once the request has been transmitted, the lock is released and a second
 * job can be sent.
 *
 * Servers which wish to limit their load can defer accepting jobs, and the
 * client will block with that lock held.
 *
 * cpp is probably cheap enough that we can allow it to run unlocked.  However
 * that is not true for local compilation or linking.
 *
 * @todo Write a test harness for the host selection algorithm.  Perhaps a
 * really simple simulation of machines taking different amounts of time to
 * build stuff?
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/file.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "hosts.h"
#include "lock.h"
#include "where.h"
#include "exitcode.h"


static int dcc_lock_one(struct dcc_hostdef *hostlist,
                        struct dcc_hostdef **buildhost,
                        int *cpu_lock_fd);


void dcc_read_localslots_configuration()
{
    struct dcc_hostdef *hostlist;
    int ret;
    int n_hosts;

    if ((ret = dcc_get_hostlist(&hostlist, &n_hosts)) == 0) {
        while (hostlist) {
            struct dcc_hostdef *l = hostlist;
            hostlist = hostlist->next;
            dcc_free_hostdef(l);
        }
    }
}


int dcc_pick_host_from_list_and_lock_it(struct dcc_hostdef **buildhost,
                            int *cpu_lock_fd)
{
    struct dcc_hostdef *hostlist;
    int ret;
    int n_hosts;

    if ((ret = dcc_get_hostlist(&hostlist, &n_hosts)) != 0) {
        return EXIT_NO_HOSTS;
    }

    if ((ret = dcc_remove_disliked(&hostlist)))
        return ret;

    if (!hostlist) {
        return EXIT_NO_HOSTS;
    }

    return dcc_lock_one(hostlist, buildhost, cpu_lock_fd);

    /* FIXME: Host list is leaked? */
}


static void dcc_lock_pause(void)
{
    /* This could do with some tuning.
     *
     * My assumption basically is that polling a little too often is
     * relatively cheap; sleeping when we should be working is bad.  However,
     * if we hit this code at all we're overloaded, so sleeping a while is
     * perhaps OK.
     *
     * We don't use exponential backoff, because that would tend to prefer
     * later arrivals and penalize jobs that have been waiting for a long
     * time.  This would mean more compiler processes hanging around than is
     * really necessary, and also by making jobs complete very-out-of-order is
     * more likely to find Makefile bugs. */

    unsigned pause_time_ms = 1000;

    char *pt = getenv("DISTCC_PAUSE_TIME_MSEC");
    if (pt)
	pause_time_ms = atoi(pt);

	/*	This call to dcc_note_state() is made before the host is known, so it
		does not make sense and does nothing useful as far as I can tell.	*/
    /*	dcc_note_state(DCC_PHASE_BLOCKED, NULL, NULL, DCC_UNKNOWN);	*/

    rs_trace("nothing available, sleeping %ums...", pause_time_ms);

    if (pause_time_ms > 0)
	usleep(pause_time_ms * 1000);
}


/**
 * Find a host that can run a distributed compilation by examining local state.
 * It can be either a remote server or localhost (if that is in the list).
 *
 * This function does not return (except for errors) until a host has been
 * selected.  If necessary it sleeps until one is free.
 *
 * @todo We don't need transmit locks for local operations.
 **/
static int dcc_lock_one(struct dcc_hostdef *hostlist,
                        struct dcc_hostdef **buildhost,
                        int *cpu_lock_fd)
{
    struct dcc_hostdef *h;
    int i_cpu;
    int ret;

    while (1) {
        for (i_cpu = 0; i_cpu < 10000; i_cpu++) {
            char i_cpu_is_usable = 0;

            for (h = hostlist; h; h = h->next) {
                if (i_cpu >= h->n_slots)
                    continue;

                i_cpu_is_usable = 1;

                ret = dcc_lock_host("cpu", h, i_cpu, 0, cpu_lock_fd);

                if (ret == 0) {
                    *buildhost = h;
                    dcc_note_state_slot(i_cpu, strcmp(h->hostname, "localhost") == 0 ? DCC_LOCAL : DCC_REMOTE);
                    return 0;
                } else if (ret == EXIT_BUSY) {
                    continue;
                } else {
                    rs_log_error("failed to lock");
                    return ret;
                }
            }

            if (!i_cpu_is_usable)
                break;
        }

        dcc_lock_pause();
    }
}



/**
 * Lock localhost.  Used to get the right balance of jobs when some of
 * them must be local.
 **/
int dcc_lock_local(int *cpu_lock_fd)
{
    struct dcc_hostdef *chosen;

    return dcc_lock_one(dcc_hostdef_local, &chosen, cpu_lock_fd);
}

int dcc_lock_local_cpp(int *cpu_lock_fd)
{
    int ret;
    struct dcc_hostdef *chosen;
    ret = dcc_lock_one(dcc_hostdef_local_cpp, &chosen, cpu_lock_fd);
    if (ret == 0) {
        dcc_note_state(DCC_PHASE_CPP, NULL, chosen->hostname, DCC_LOCAL);
    }
    return ret;
}
