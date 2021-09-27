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


/**
 * @file
 *
 * Keep track of hosts which are, or are not, usable.
 **/

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
#include "exitcode.h"
#include "snprintf.h"
#include "lock.h"
#include "timefile.h"
#include "hosts.h"


static int dcc_backoff_period = 60; /* seconds */

static int dcc_get_backoff_period(void)
{
    char *bp;
    bp = getenv("DISTCC_BACKOFF_PERIOD");
    if (bp)
        dcc_backoff_period = atoi(bp);
    return dcc_backoff_period;
}

int dcc_backoff_is_enabled(void)
{
    return dcc_get_backoff_period() != 0;
}

/**
 * Remember that this host is working OK.
 *
 * For the moment this just means removing any backoff timer scored against
 * it.
 **/
int dcc_enjoyed_host(const struct dcc_hostdef *host)
{
    /* special-case: if DISTCC_BACKOFF_PERIOD==0, don't manage backoff files */
    if (!dcc_backoff_is_enabled())
        return 0;

    return dcc_remove_timefile("backoff", host);
}

int dcc_disliked_host(const struct dcc_hostdef *host)
{
    /* special-case: if DISTCC_BACKOFF_PERIOD==0, don't manage backoff files */
    if (!dcc_backoff_is_enabled())
	return 0;

    /* i hate you (but only for dcc_backoff_period seconds) */
    return dcc_mark_timefile("backoff", host);
}


static int dcc_check_backoff(struct dcc_hostdef *host)
{
    int ret;
    time_t mtime;

    if ((ret = dcc_check_timefile("backoff", host, &mtime)))
        return ret;

    if (difftime(time(NULL), mtime) < (double) dcc_backoff_period) {
        rs_trace("still in backoff period for %s", host->hostdef_string);
        return EXIT_BUSY;
    }

    return 0;
}


/**
 * Walk through @p hostlist and remove any hosts that are marked unavailable.
 **/
int dcc_remove_disliked(struct dcc_hostdef **hostlist)
{
    struct dcc_hostdef *h;

    if (!dcc_backoff_is_enabled())
	return 0;

    while ((h = *hostlist) != NULL) {
        if (dcc_check_backoff(h) != 0) {
            rs_trace("remove %s from list", h->hostdef_string);
            *hostlist = h->next;
            free(h);
        } else {
            /* check next one */
            hostlist = &h->next;
        }
    }

    return 0;
}
