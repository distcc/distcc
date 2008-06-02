/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 * $Header: /data/cvs/distcc/src/h_issource.c,v 1.7 2003/07/13 08:08:02 mbp Exp $
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


const char *rs_program_name = __FILE__;


/**
 * Test harness: determine whether a file is source, and is preprocessed.
 **/
int main(int argc, char *argv[])
{
    if (argc != 2) {
        rs_log_error("usage: %s FILENAME", argv[0]);
        return 1;
    }

    printf("%s %s\n",
       dcc_is_source(argv[1]) ? "source" : "not-source",
       dcc_is_preprocessed(argv[1]) ? "preprocessed" : "not-preprocessed");

    return 0;
}
