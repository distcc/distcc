/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 * $Header: /data/cvs/distcc/src/h_sa2str.c,v 1.1 2004/01/10 23:15:32 mbp Exp $
 *
 * Copyright (C) 2004 by Martin Pool <mbp@samba.org>
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
#include <netdb.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>


#include "types.h"
#include "exitcode.h"
#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "srvnet.h"
#include "access.h"
#include "netutil.h"
#include "snprintf.h"

const char *rs_program_name = "h_sa2str";

/* Try to print out a sockaddr */
int main(void)
{
    struct sockaddr_in  sa;
    char *buf;
    int ret;

    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = (in_addr_t) htonl(0x01020304);
    sa.sin_port = 4200;

    if ((ret = dcc_sockaddr_to_string((struct sockaddr *) &sa, sizeof sa, &buf)))
        return ret;
    puts(buf);
    free(buf);

    return 0;
}
