/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
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
 * @file
 *
 * Declarations for distcc host selection stuff.
 **/

/**
 * A simple linked list of host definitions.  All strings are mallocd.
 **/
struct dcc_hostdef {
    enum {
        DCC_MODE_TCP = 1,
        DCC_MODE_SSH,
        DCC_MODE_LOCAL
    } mode;
    char * user;
    char * hostname;
    int port;
    char * ssh_command;

    /** Mark the host as up == 1, by default, or down == 0, if !hostname */
    int is_up;

    /** Number of tasks that can be dispatched concurrently to this machine. */
    int         n_slots;

    /** The full name of this host, taken verbatim from the host
     * definition. **/
    char * hostdef_string;

    enum dcc_protover protover;

    /** The kind of compression to use for this host */
    enum dcc_compress compr;

    /** Where are we doing preprocessing? */
    enum dcc_cpp_where cpp_where;

#ifdef HAVE_GSSAPI
    /* Are we authenticating with this host? */
    int authenticate;
    char * auth_name;
#endif

    struct dcc_hostdef *next;
};

/** Static definition of localhost **/
extern struct dcc_hostdef *dcc_hostdef_local;
extern struct dcc_hostdef *dcc_hostdef_local_cpp;

/* hosts.c */
int dcc_get_hostlist(struct dcc_hostdef **ret_list,
                     int *ret_nhosts);

int dcc_free_hostdef(struct dcc_hostdef *host);

int dcc_get_features_from_protover(enum dcc_protover protover,
                                   enum dcc_compress *compr,
                                   enum dcc_cpp_where *cpp_where);

int dcc_get_protover_from_features(enum dcc_compress compr,
                                   enum dcc_cpp_where cpp_where,
                                   enum dcc_protover *protover);

/* hostfile.c */
int dcc_parse_hosts_file(const char *fname,
                         struct dcc_hostdef **ret_list,
                         int *ret_nhosts);
