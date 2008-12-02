/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
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

#ifndef HAVE_IN_PORT_T
#define HAVE_IN_PORT_T
typedef int in_port_t;
#endif

#ifndef HAVE_IN_ADDR_T
/* Seems to be equivalent to ulong on FreeBSD 3.3, where it is missing.
 * http://www.freebsd.org/cgi/man.cgi?query=inet_aton&apropos=0&sektion=0&manpath=FreeBSD+3.3-RELEASE&format=html
 *
 * On Linux it is uint32.
 */
#define HAVE_IN_ADDR_T
typedef unsigned long in_addr_t;
#endif
