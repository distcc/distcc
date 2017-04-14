/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * Copyright 2005 Google Inc.
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

/* don't blame me, I was in a hurry */

#include <config.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>

#include "rslave.h"

/*--------------------------------------------------------------------------
 Class to provide asynchronous DNS lookup.
 To use, first call rslave_init() very early in your program to fork the
 dns slave processes.
 Then call rslave_write() any time you need a DNS name resolved
 and rslave_read() to retrieve the next result.  Order of lookup requests
 is not preserved.
 Call rslave_getfd() and select on that fd for readability if you
 want to only call rslave_read() once it won't block.
 The slaves will shut down when their input file descriptor is closed,
 which normally happens when your program exits.
--------------------------------------------------------------------------*/

int rslave_getfd_fromSlaves(struct rslave_s *rslave)
{
    return rslave->pipeFromSlaves[0];
}

int rslave_getfd_toSlaves(struct rslave_s *rslave)
{
    return rslave->pipeToSlaves[1];
}

void rslave_request_init(struct rslave_request_s *buf, const char *hostname, int id)
{
    memset(buf, 0, sizeof(*buf));
    strncpy(buf->hname, hostname, rslave_HOSTLEN);
    buf->id = id;
}

int rslave_writeRequest(struct rslave_s *rslave, const struct rslave_request_s *req)
{
    if (write(rslave->pipeToSlaves[1], req, sizeof(*req)) != sizeof(*req))
        return -1;
    return 0;
}

int rslave_gethostbyname(struct rslave_s *rslave, const char *hostname, int id)
{
    struct rslave_request_s buf;
    rslave_request_init(&buf, hostname, id);
    return rslave_writeRequest(rslave, &buf);
}

int rslave_readRequest(struct rslave_s *rslave, struct rslave_request_s *req)
{
    if (read(rslave->pipeToSlaves[0], req, sizeof(*req)) != sizeof(*req))
        return -1;
    return 0;
}

int rslave_writeResult(struct rslave_s *rslave, struct rslave_result_s *result)
{
    if (write(rslave->pipeFromSlaves[1], result, sizeof(*result)) != sizeof(*result))
        return -1;
    return 0;
}

int rslave_readResult(struct rslave_s *rslave, struct rslave_result_s *result)
{
    if (read(rslave->pipeFromSlaves[0], result, sizeof(*result)) != sizeof(*result))
        return -1;
    return 0;
}

void be_a_dnsslave(struct rslave_s *rslave);
void be_a_dnsslave(struct rslave_s *rslave)
{
    struct rslave_request_s req;
    while (rslave_readRequest(rslave, &req) == 0) {
        struct rslave_result_s result;
        struct hostent *h;
        /* fprintf(stderr, "Calling gethostbyname on %s\n", req.hname); */
        h = gethostbyname(req.hname);
        memset(&result, 0, sizeof(result));
        result.id = req.id;
        result.err = h_errno;
        if (h && (h->h_length == sizeof(result.addr))) {
            memcpy(result.addr, h->h_addr_list[0], (unsigned) h->h_length);
            result.err = 0;
        }
        if (rslave_writeResult(rslave, &result))
            break;
    }
    exit(0);
}

/*--------------------------------------------------------------------------
 Initialize an rslave_s and fork slave processes.
 Returns 0 on success, -1 on error.
--------------------------------------------------------------------------*/

int rslave_init(struct rslave_s *rslave)
{
    int err;
    int i;
    int nslaves = rslave_NSLAVES;

    memset(rslave, 0, sizeof(*rslave));
    err = pipe(rslave->pipeToSlaves);
    if (err == -1)
    return -1;
    err = pipe(rslave->pipeFromSlaves);
    if (err == -1)
    return -1;

    for (i=0; i<nslaves; i++) {
        pid_t childpid;
        childpid = fork();
        switch (childpid) {
        case -1:
            return -1;
            break;

        case 0:    /* child */
            close(rslave->pipeToSlaves[1]);
            close(rslave->pipeFromSlaves[0]);
            be_a_dnsslave(rslave);
            break;
        default: /* parent */
            rslave->pids[i] = childpid;        /* Save pid so we can kill it later */
            break;
        }
    }
    close(rslave->pipeToSlaves[0]);
    close(rslave->pipeFromSlaves[1]);

    rslave->nslaves = nslaves;
    return 0;
}

/* TODO: add rslave_shutdown() that kills all the slaves by iterating through rslave->pids[] */
