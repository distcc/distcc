/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 * $Header: /data/cvs/distcc/src/h_scanargs.c,v 1.9 2003/07/13 08:08:02 mbp Exp $
 *
 * Copyright (C) 2002 by Martin Pool <mbp@samba.org>
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "implicit.h"

const char *rs_program_name = __FILE__;


/**
 * Test harness: make argument-parsing code accessible from the
 * command line so that it can be tested.
 **/
int main(int argc, char *argv[])
{
    int result;
    char *infname, *outfname;
    char **newargv, **outargv;

    rs_trace_set_level(RS_LOG_DEBUG);

    if (argc < 2) {
        rs_log_error("usage: h_scanargs COMMAND ARG...\n");
        return 1;
    }

    result = dcc_find_compiler(argv, &newargv);

    if (result)
    return result;

    result = dcc_scan_args(newargv, &infname, &outfname, &outargv);

    printf("%s %s %s\n",
       result == 0 ? "distribute" : "local",
       infname ? infname : "(NULL)", outfname ? outfname : "(NULL)");

    return 0;
}
