/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002 by Martin Pool <mbp@samba.org>
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


/**
 * @file
 *
 * Test harness for dotd.c.
 **/


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "dotd.h"
#include "trace.h"

#define USAGE \
"usage: h_dotd COMMAND ARGS...\n" \
    "where\n" \
    "  COMMAND is dcc_get_dotd_info, ARGS is NAME\n"

const char *rs_program_name = __FILE__;


int main(int argc, char *argv[])
{
    rs_trace_set_level(RS_LOG_WARNING);

    if (argc < 2) {
        rs_log_error(USAGE);
        return 1;
    }

    if (strcmp(argv[1], "dcc_get_dotd_info") == 0) {
        char *dotd_fname;
        int needs_dotd, sets_dotd_target;
        char *dotd_target;
        dcc_get_dotd_info(argv + 2, &dotd_fname, &needs_dotd, &sets_dotd_target,
                          &dotd_target);
        /* Print out in a format easily digested in Python. */
        printf("{'dotd_fname':'%s', 'needs_dotd':%d, 'sets_dotd_target':%d,"
               " 'dotd_target':'%s'}",
               dotd_fname, needs_dotd, sets_dotd_target,
               dotd_target ? dotd_target : "None");
    } else {
        rs_log_error(USAGE);
        return 1;
    }
    return 0;
}
