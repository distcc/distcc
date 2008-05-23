/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003 by Martin Pool <mbp@samba.org>
 * Copyright 2008 Google Inc.
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

#include <sys/types.h>
#include <sys/stat.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"
#include "hosts.h"

/* TODO: Perhaps in the future allow filenames to be given in environment
 * variables to cause other files to be "included". */



/**
 * Return a hostlist read from fname (possibly recursively.)
 **/
int dcc_parse_hosts_file(const char *fname,
                         struct dcc_hostdef **ret_list,
                         int *ret_nhosts)
{
    char *body;
    int ret;

    rs_trace("load hosts from %s", fname);

    if ((ret = dcc_load_file_string(fname, &body)) != 0)
        return ret;

    ret = dcc_parse_hosts(body, fname, ret_list, ret_nhosts, NULL);

    free(body);

    return ret;
}
