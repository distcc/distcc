/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 * $Header: /data/cvs/distcc/src/h_argvtostr.c,v 1.4 2003/07/13 08:08:02 mbp Exp $
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

#include "distcc.h"
#include "trace.h"
#include "util.h"

const char *rs_program_name = __FILE__;


/**
 * @file
 *
 * Test argv-to-string converter.
 **/


int main(int argc, char *argv[])
{
    rs_trace_set_level(RS_LOG_WARNING);

    if (argc < 2) {
        rs_log_error("usage: h_scanargs COMMAND ARG...\n");
        return 1;
    }

    printf("%s\n", dcc_argv_tostr(&argv[1]));

    return 0;
}
