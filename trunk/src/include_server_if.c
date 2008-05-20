/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4 fill-column: 78 -*-
 * Copyright 2007 Google Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

// Author: Manos Renieris

#include <string.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "distcc.h"
#include "trace.h"
#include "rpc.h"
#include "clinet.h"
#include "exitcode.h"
#include "util.h"
#include "include_server_if.h"

/* The include server puts all files in its own special directory,
 * which is n path components long, where n = INCLUDE_SERVER_DIR_DEPTH
 */
#define INCLUDE_SERVER_DIR_DEPTH 3

/** Talks to the include server, over the AF_UNIX socket specified
 * in env variable INCLUDE_SERVER_PORT. If all goes well,
 * it returns the array of files in @p files and returns 0;
 * if anything goes wrong, it returns a non-zero value.
 */

int dcc_talk_to_include_server(char **argv, char ***files)
{
    char *include_server_port;
    int fd;
    struct sockaddr_un sa;

    int ret;
    char *stub;

    /* for testing purposes, if INCLUDE_SERVER_STUB is set,
       use its value rather than the include server */
    stub = getenv("INCLUDE_SERVER_STUB");
    if (stub != NULL) {
        ret = dcc_tokenize_string(stub, files);
        rs_log_warning("INCLUDE_SERVER_STUB is set to '%s'; "
                       "ignoring include server",
                       dcc_argv_tostr(*files));
        return ret;
    }

    include_server_port = getenv("INCLUDE_SERVER_PORT");
    if (include_server_port == NULL) {
        rs_log_warning("INCLUDE_SERVER_PORT not set");
        return 1;
    }

    if (strlen(include_server_port) >= ((int)sizeof(sa.sun_path) - 1)) {
        rs_log_warning("$INCLUDE_SERVER_PORT is longer than %d characters",
                       (sizeof(sa.sun_path) - 1));
        return 1;
    }

    strcpy(sa.sun_path, include_server_port);
    sa.sun_family = AF_UNIX;

    if (dcc_connect_by_addr((struct sockaddr *) &sa, sizeof(sa), &fd))
        return 1;

    /* the following code uses dcc_r_arg to receive an array of strings
     * which are NOT command line arguments. TODO: implement dcc_r_argv
     * on top a generic array-of-strings function */
    if (dcc_x_cwd(fd) ||
        dcc_x_argv(fd, argv) ||
        dcc_r_argv(fd, files)) {
        rs_log_warning("failed to talk to include server '%s'",
                       include_server_port);
        dcc_close(fd);
        /* We are failing anyway, so we can ignore
           the return value of dcc_close() */
        return 1;
    }

    if (dcc_close(fd)) {
        return 1;
    }

    if (dcc_argv_len(*files) == 0) {
        rs_log_warning("include server gave up analyzing");
        return 1;
    }
    return 0;
}

/* The include server puts all files in its own special directory,
 * which is n path components long, where n = INCLUDE_SERVER_DIR_DEPTH
 * The original file should drop those components.
 * Also, we need to strip the .lzo and .lzo.abs suffixes.
 */
int dcc_get_original_fname(const char *fname, char **original_fname)
{
    int i;
    char *work, *alloced_work, *extension;

    alloced_work = work = strdup(fname);
    if (work == NULL)
        return EXIT_OUT_OF_MEMORY;

    /* Since all names are supposed to be absolute, they start with
     * a slash. We are trying to drop INCLUDE_SERVER_DIR_DEPTH path
     * components, so we start right after the first slash, and we look
     * for a slash, and then we skip that slash and look for a slash, etc.
     */

    for (i = 0; i < INCLUDE_SERVER_DIR_DEPTH; ++i) {
        work = strchr(work + 1, '/');
        if (work == NULL) {
            return 1;
        }
    }

    /* This code removes an abs extension if it's there, and
       then a .lzo extension if it's there. As a result
       a .lzo.abs extension is removed, but not a .abs.lzo
       extension.
     */
    extension = dcc_find_extension(work);
    if (extension && (strcmp(extension, ".abs") == 0)) {
        *extension = '\0';
    }
    extension = dcc_find_extension(work);
    if (extension && (strcmp(extension, ".lzo") == 0)) {
        *extension = '\0';
    }

    *original_fname = strdup(work);
    if (*original_fname == NULL) {
        free(alloced_work);
        return EXIT_OUT_OF_MEMORY;
    }
    free(alloced_work);
    return 0;
}
