/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
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
 *
 * Author: Thomas Kho
 */

#ifndef _DISTCC_STATS_H
#define _DISTCC_STATS_H

#ifdef __cplusplus
extern "C" {
#endif

enum stats_e { STATS_TCP_ACCEPT, STATS_REJ_BAD_REQ, STATS_REJ_OVERLOAD,
                STATS_COMPILE_OK, STATS_COMPILE_ERROR, STATS_COMPILE_TIMEOUT,
                STATS_CLI_DISCONN, STATS_OTHER, STATS_ENUM_MAX };

extern const char *stats_text[20];

int  dcc_stats_init(void);
void dcc_stats_init_kid(void);
int  dcc_stats_server(int listen_fd);
void dcc_stats_event(enum stats_e e);
void dcc_stats_compile_ok(char *compiler, char *filename, struct timeval start,
     struct timeval stop, int time_usec);

#ifdef __cplusplus
}
#endif

#endif /* _DISTCC_STATS_H */
