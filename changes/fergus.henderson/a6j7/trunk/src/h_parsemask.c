/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003 by Martin Pool <mbp@samba.org>
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "types.h"
#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "access.h"

const char * rs_program_name = "h_parsemask";

int main(int argc, char **argv)
{
    int ret;
    dcc_address_t value, mask;
    struct sockaddr_in client_ia;

    rs_add_logger(rs_logger_file, RS_LOG_DEBUG, NULL, STDERR_FILENO);
    rs_trace_set_level(RS_LOG_INFO);

    if (argc != 3) {
        rs_log_error("usage: h_parsemask MASK CLIENT");
        return EXIT_BAD_ARGUMENTS;
    }

    ret = dcc_parse_mask(argv[1], &value, &mask);
    if (ret)
        return ret;

    client_ia.sin_family = AF_INET;
    if (!inet_aton(argv[2], &client_ia.sin_addr)) {
        rs_log_error("can't parse client address \"%s\"", argv[2]);
        return EXIT_BAD_ARGUMENTS;
    }

    return dcc_check_address((struct sockaddr *) &client_ia, &value, &mask);
}
