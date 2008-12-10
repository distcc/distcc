/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 * $Header: /data/cvs/distcc/src/h_exten.c,v 1.7 2003/07/13 08:08:02 mbp Exp $
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
 * Test harness for functions in compile.c. (Only one so far.)
 **/

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
#include "compile.h"


#define USAGE \
"usage: h_compile COMMAND ARGS...\n" \
"where\n" \
"  COMMAND is dcc_fresh_dependency_exists,\n" \
"    with ARGS being DOTD_FNAME EXCL_PAT REF_TIME\n" \
"or\n" \
"  COMMAND is dcc_discrepancy_filename\n"

const char *rs_program_name = __FILE__;


int main(int argc, char *argv[])
{
    rs_trace_set_level(RS_LOG_DEBUG);
    rs_add_logger(rs_logger_file, RS_LOG_DEBUG, NULL, STDERR_FILENO);
    if (argc < 2) {
        rs_log_error(USAGE);
        return 1;
    }

    if (strcmp(argv[1], "dcc_fresh_dependency_exists") == 0) {
        if (argc != 5) {
            rs_log_error("dcc_fresh_dependency_exists expects DOTD_FNAME "
                         "EXCL_PAT REF_TIME");
            return 1;
        }
        errno = 0;
        char *ptr;
        time_t ref_time = (time_t)strtol(argv[4], &ptr, 0);
        if (errno || (*ptr != '\0')) {
            rs_log_error("strtol failed");
            return 1;
        } else {
            char *result;
            int ret;
            ret = dcc_fresh_dependency_exists((const char *)argv[2],
                                              (const char *)argv[3],
                                              ref_time,
                                              &result);
            if (ret)
                printf("h_compile.c: UNEXPECTED RETURN VALUE\n");
            else
                printf("result %s\n", result ? result : "(NULL)");
            if (result) free(result);
        }
    } else if (strcmp(argv[1], "dcc_discrepancy_filename") == 0) {
        if (argc != 2) {
            rs_log_error("dcc_discrepancy_filename expects no arguments");
            return 1;
        }
        char *result;
        int ret = dcc_discrepancy_filename(&result);
        if (ret)
            printf("h_compile.c: UNEXPECTED RETURN VALUE\n");
        else
            printf("%s", result ? result : "(NULL)");
    } else {
        rs_log_error(USAGE);
        return 1;
    }
    return 0;
}
