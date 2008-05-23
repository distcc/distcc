/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
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
#ifndef rslave_H
#define rslave_H

/* don't blame me, I was in a hurry */

#include <unistd.h>    /* for pid_t */

/* maximum length of hostname */
#define rslave_HOSTLEN 200

/* Number of DNS slave processes.  Emperically I've found that I need
 * for up to about 50 servers, you need 1 for main lookup plus two spares for retries;
 * for up to about 150 servers, you need 2 for main lookup plus a few spares for retries.
 * Six seems like it should be enough for most sites.
 */
#define rslave_NSLAVES 6

struct rslave_s {
    int nslaves;
    int pipeToSlaves[2];
    int pipeFromSlaves[2];
    pid_t pids[rslave_NSLAVES];
};

struct rslave_request_s {
    int id;
    char hname[rslave_HOSTLEN+1];
};
typedef struct rslave_request_s rslave_request_t;

struct rslave_result_s {
    int id;
    int err;
    unsigned char addr[4];
};
typedef struct rslave_result_s rslave_result_t;

int rslave_init(struct rslave_s *rslave);
int rslave_gethostbyname(struct rslave_s *rslave, const char *hostname, int id);
void rslave_request_init(struct rslave_request_s *buf, const char *hostname, int id);
int rslave_readRequest(struct rslave_s *rslave, struct rslave_request_s *req);
int rslave_writeRequest(struct rslave_s *rslave, const struct rslave_request_s *req);
int rslave_writeResult(struct rslave_s *rslave, struct rslave_result_s *result);
int rslave_readResult(struct rslave_s *rslave, struct rslave_result_s *result);
int rslave_getfd_fromSlaves(struct rslave_s *rslave);
int rslave_getfd_toSlaves(struct rslave_s *rslave);

#endif
