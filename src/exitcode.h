/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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

#ifndef _DISTCC_EXITCODE_H
#define _DISTCC_EXITCODE_H

/**
 * @file
 *
 * Common exit codes.
 **/

/**
 * Common exit codes for both client and server.
 *
 * These need to be in [1,255] so that they can be used as exit() codes.  They
 * are fairly high so that they're not confused with the real error code from
 * gcc.
 *
 * WARNING: ANY CHANGES HERE NEED TO BE DUPLICATED IN THE MAN PAGE
 * (../man/distcc.1).
 **/
enum dcc_exitcode {
    EXIT_DISTCC_FAILED            = 100, /**< General failure */
    EXIT_BAD_ARGUMENTS            = 101,
    EXIT_BIND_FAILED              = 102,
    EXIT_CONNECT_FAILED           = 103,
    EXIT_COMPILER_CRASHED         = 104,
    EXIT_OUT_OF_MEMORY            = 105,
    EXIT_BAD_HOSTSPEC             = 106,
    EXIT_IO_ERROR                 = 107,
    EXIT_TRUNCATED                = 108,
    EXIT_PROTOCOL_ERROR           = 109,
    EXIT_COMPILER_MISSING         = 110, /**< Compiler executable not found */
    EXIT_RECURSION                = 111, /**< distcc called itself */
    EXIT_SETUID_FAILED            = 112, /**< Failed to discard privileges */
    EXIT_ACCESS_DENIED            = 113, /**< Network access denied */
    EXIT_BUSY                     = 114, /**< In use by another process. */
    EXIT_NO_SUCH_FILE             = 115,
    EXIT_NO_HOSTS                 = 116,
    EXIT_GONE                     = 117, /**< No longer relevant */
    EXIT_TIMEOUT                  = 118,
#ifdef HAVE_GSSAPI
    EXIT_GSSAPI_FAILED            = 119, /**< GSS-API - Catchall error code for GSS-API related errors. */
#endif
    EXIT_LOCAL_CPP                = 120
};


#endif /* _DISTCC_EXITCODE_H */
