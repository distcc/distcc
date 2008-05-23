/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003, 2004 by Martin Pool <mbp@samba.org>
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "types.h"
#include "distcc.h"
#include "rpc.h"
#include "trace.h"
#include "exitcode.h"
#include "snprintf.h"
#include "mon.h"


/**
 * @file
 *
 * Plain text monitor program.  Just prints out the state once, or
 * repeatedly, kind of like Linux vmstat.
 */


const char *rs_program_name = "distccmon-text";


static void usage(void)
{
    fprintf(stderr, "usage: distccmon-text [DELAY]\n"
"\n"
"Displays current compilation jobs in text form.\n"
"\n"
"If delay is specified, repeatedly updates after that many (fractional)\n"
"seconds.  Otherwise, runs just once.\n");
}

int main(int argc, char *argv[])
{
    struct dcc_task_state *list;
    int ret;
    float delay;
    char *end;

    dcc_set_trace_from_env();

    if (argc == 1)
        delay = 0.0;
    else if (argc == 2) {
        delay = strtod(argv[1], &end);
        if (*end) {
            usage();
            return 1;
        }
    } else {
        usage();
        return 1;
    }

    /* We might be writing to e.g. a pipe that's being read by some
     * other program, so make sure we're always line buffered. */
    setvbuf (stdout, NULL, _IOLBF, BUFSIZ);

    do {
        struct dcc_task_state *i;

        if ((ret = dcc_mon_poll(&list)))
            return ret;

        for (i = list; i; i = i->next) {
#if 1
            if (i->curr_phase == DCC_PHASE_DONE)
                continue;
#endif
            /* Assume 80 cols = */
            printf("%6ld  %-10.10s  %-30.30s %24.24s[%d]\n",
                   (long) i->cpid,
                   dcc_get_phase_name(i->curr_phase),
                   i->file, i->host, i->slot);
        }

        printf("\n");

        /* XXX: usleep() is probably not very portable */
        usleep(delay * 1000000);

        dcc_task_state_free(list);
    } while (delay);

    return 0;
}
