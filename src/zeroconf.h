/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * Copyright (C) 2007 Lennart Poettering
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

#ifndef foozeroconfhfoo
#define foozeroconfhfoo

#include <inttypes.h>

int dcc_zeroconf_add_hosts(struct dcc_hostdef **re_list, int *ret_nhosts, int slots, struct dcc_hostdef **ret_prev);

void *dcc_zeroconf_register(uint16_t port, int n_cpus, int n_jobs);
int dcc_zeroconf_unregister(void*);

char* dcc_get_gcc_version(char *s, size_t nbytes);
char* dcc_get_gcc_machine(char *s, size_t nbytes);

char* dcc_make_dnssd_subtype(char *stype, size_t nbytes, const char *v, const char *m);

#define DCC_DNS_SERVICE_TYPE "_distcc._tcp"

#endif
