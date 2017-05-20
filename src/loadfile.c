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


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"



/**
 * Load a whole file into a new string in a malloc'd memory buffer.
 *
 * Files larger than a certain reasonableness limit are not loaded, because
 * this is only used for reasonably short text files.
 *
 * Files that do not exist cause EXIT_NO_SUCH_FILE, but no error message.
 * (This suits our case of loading configuration files.  It could be made
 * optional.)
 **/
int dcc_load_file_string(const char *filename,
                         char **retbuf)
{
    int fd;
    int ret;
    ssize_t read_bytes;
    struct stat sb;
    char *buf;

    /* Open the file */
    if ((fd = open(filename, O_RDONLY)) == -1) {
        if (errno == ENOENT)
            return EXIT_NO_SUCH_FILE;
        else {
            rs_log_warning("failed to open %s: %s", filename,
                           strerror(errno));
            return EXIT_IO_ERROR;
        }
    }

    /* Find out how big the file is */
    if (fstat(fd, &sb) == -1) {
        rs_log_error("fstat %s failed: %s", filename, strerror(errno));
        ret = EXIT_IO_ERROR;
        goto out_close;
    }

    if (sb.st_size > 1<<20) {
        rs_log_error("%s is too large to load (%ld bytes)", filename,
                     (long) sb.st_size);
        ret = EXIT_OUT_OF_MEMORY;
        goto out_close;
    }

    /* Allocate a buffer, allowing space for a nul. */
    if ((*retbuf = buf = malloc((size_t) sb.st_size + 1)) == NULL) {
        rs_log_error("failed to allocate %ld byte file buffer", (long) sb.st_size);
        ret = EXIT_OUT_OF_MEMORY;
        goto out_close;
    }

    /* Read everything */
    if ((read_bytes = read(fd, buf, (size_t) sb.st_size)) == -1) {
        rs_log_error("failed to read %s: %s", filename, strerror(errno));
        ret = EXIT_IO_ERROR;
        goto out_free;
    }

    /* Null-terminate.  It's OK if we read a bit less than we expected to. */
    buf[read_bytes] = '\0';
    ret = 0;

    out_close:
    dcc_close(fd);
    return ret;

    out_free:
    free(*retbuf);
    dcc_close(fd);
    return ret;
}
