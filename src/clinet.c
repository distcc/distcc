/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool
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


                        /* I just wish I could get caller-IQ on my phones...
                                   -- The Purple People-Eater, NANAE */



#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <netdb.h>

#include "types.h"
#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "clinet.h"
#include "util.h"
#include "netutil.h"

#ifndef h_errno
extern int h_errno;
#endif


const int dcc_connect_timeout = 4; /* seconds */

/*
 * Client-side networking.
 *
 * These are called with an alarm set so we get a single timeout over the
 * whole resolution and connection process.
 *
 * TODO: In error messages, show the name of the relevant host.
 * Should do this even in readx(), etc.
 *
 * TODO: After connecting, perhaps try to read 0 bytes to see if there's an
 * error.
 */


/*
 * Connect to a host given its binary address, with a timeout.
 *
 * host and port are only here to aid printing debug messages.
 */
int dcc_connect_by_addr(struct sockaddr *sa, size_t salen,
                        int *p_fd)
{
    int fd;
    int ret;
    char *s;
    int failed;
    int connecterr;
    int tries = 3;

    dcc_sockaddr_to_string(sa, salen, &s);
    if (s == NULL) return EXIT_OUT_OF_MEMORY;

    rs_trace("started connecting to %s", s);

    if ((fd = socket(sa->sa_family, SOCK_STREAM, 0)) == -1) {
        rs_log_error("failed to create socket: %s", strerror(errno));
        ret = EXIT_CONNECT_FAILED;
        goto out_failed;
    }

    dcc_set_nonblocking(fd);

    /* start the nonblocking connect... */
    do
        failed = connect(fd, sa, salen);
    while (failed == -1 &&
           (errno == EINTR ||
            (errno == EAGAIN && tries-- && poll(NULL, 0, 500) == 0)));

   if (failed == -1 && errno != EINPROGRESS) {
       rs_log(RS_LOG_ERR|RS_LOG_NONAME,
              "failed to connect to %s: %s", s, strerror(errno));
       ret = EXIT_CONNECT_FAILED;
       goto out_failed;
   }

    do {
       socklen_t len;

       if ((ret = dcc_select_for_write(fd, dcc_connect_timeout))) {
           rs_log(RS_LOG_ERR|RS_LOG_NONAME,
                  "timeout while connecting to %s", s);
           goto out_failed;
       }

       connecterr = -1;
       len = sizeof(connecterr);
       if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&connecterr, &len) < 0) {
               rs_log_error("getsockopt SO_ERROR failed?!");
               ret = EXIT_CONNECT_FAILED;
               goto out_failed;
       }

       /* looping is unlikely, but I believe I needed this in dkftpbench */
       /* fixme: should reduce timeout on each time around this loop */
    } while (connecterr == EINPROGRESS);

    if (connecterr) {
       rs_log(RS_LOG_ERR|RS_LOG_NONAME,
                "nonblocking connect to %s failed: %s", s, strerror(connecterr));
       ret = EXIT_CONNECT_FAILED;
       goto out_failed;
    }

    *p_fd = fd;
    free(s);
    return 0;

out_failed:
    free(s);
    return ret;
}


#if defined(ENABLE_RFC2553)

/**
 * Open a socket to a tcp remote host with the specified port.
 **/
int dcc_connect_by_name(const char *host, int port, int *p_fd)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int error;
    int ret;
    char portname[20];

    rs_trace("connecting to %s port %d", host, port);

    /* Unfortunately for us, getaddrinfo wants the port (service) as a string */
    snprintf(portname, sizeof portname, "%d", port);

    memset(&hints, 0, sizeof(hints));
    /* set-up hints structure */
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    error = getaddrinfo(host, portname, &hints, &res);
    if (error) {
        rs_log_error("failed to resolve host %s port %d: %s", host, port,
                     gai_strerror(error));
        return EXIT_CONNECT_FAILED;
    }

    /* Try each of the hosts possible addresses. */
    do {
        ret = dcc_connect_by_addr(res->ai_addr, res->ai_addrlen, p_fd);
    } while (ret != 0 && (res = res->ai_next));

    return ret;
}


#else /* not ENABLE_RFC2553 */

/**
 * Open a socket to a tcp remote host with the specified port.
 *
 * @todo Don't try for too long to connect.
 **/
int dcc_connect_by_name(const char *host, int port, int *p_fd)
{
    struct sockaddr_in sock_out;
    struct hostent *hp;

    /* FIXME: "warning: gethostbyname() leaks memory.  Use gethostbyname_r
     * instead!" (or indeed perhaps use getaddrinfo?) */
    hp = gethostbyname(host);
    if (!hp) {
        rs_log_error("failed to look up host \"%s\": %s", host,
                     hstrerror(h_errno));
        return EXIT_CONNECT_FAILED;
    }

    memcpy(&sock_out.sin_addr, hp->h_addr, (size_t) hp->h_length);
    sock_out.sin_port = htons((in_port_t) port);
    sock_out.sin_family = PF_INET;

    return dcc_connect_by_addr((struct sockaddr *) &sock_out,
                               sizeof sock_out, p_fd);
}

#endif /* not ENABLE_RFC2553 */
