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

#pragma once

#include <config.h>

/* access.c */

#ifndef ENABLE_RFC2553
typedef struct in_addr dcc_address_t;
#else
typedef struct dcc_address {
    sa_family_t family;
    union {
        struct in_addr inet;
        struct in6_addr inet6;
    } addr;
} dcc_address_t;
#endif

int dcc_parse_mask(const char *mask_spec,
                   dcc_address_t *value,
                   dcc_address_t *mask);

int dcc_check_address(const struct sockaddr *client,
                      const dcc_address_t *value,
                      const dcc_address_t *mask);

struct dcc_allow_list {
    dcc_address_t addr, mask;
    struct dcc_allow_list *next;
};
