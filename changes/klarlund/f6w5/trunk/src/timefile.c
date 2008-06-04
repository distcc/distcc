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


/**
 * @file
 *
 * @brief Track timeouts by setting the mtime of a file.
 *
 * distcc needs to set timeouts to backoff from unreachable hosts.  As a very
 * simple and robust way of keeping track of this, we simply touch a file in
 * our state directory, whenever we fail to connect.  Future invocations can
 * look at how recently the host failed when deciding whether to use it again.
 **/


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/file.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"
#include "snprintf.h"
#include "lock.h"
#include "timefile.h"


/**
 * Record the current time against the specified function and host.
 **/
int dcc_mark_timefile(const char *lockname,
                      const struct dcc_hostdef *host)
{
    char *filename;
    int fd;
    int ret;

    if ((ret = dcc_make_lock_filename(lockname, host, 0, &filename)))
        return ret;

    if ((ret = dcc_open_lockfile(filename, &fd))) {
        free(filename);
        return ret;
    }

    /* Merely opening it with O_WRONLY is not necessarily enough to set its
     * mtime to the current time. */
    if (write(fd, "x", 1) != 1) {
        rs_log_error("write to %s failed: %s", lockname, strerror(errno));
        dcc_close(fd);
        return EXIT_IO_ERROR;
    }

    dcc_close(fd);

    rs_trace("mark %s", filename);

    free(filename);

    return 0;
}



/**
 * Remove the specified timestamp.
 **/
int dcc_remove_timefile(const char *lockname,
                        const struct dcc_hostdef *host)
{
    char *filename;
    int ret = 0;

    if ((ret = dcc_make_lock_filename(lockname, host, 0, &filename)))
        return ret;

    if (unlink(filename) == 0) {
        rs_trace("remove %s", filename);
    } else {
        if (errno == ENOENT) {
            /* it's ok if somebody else already removed it */
        } else {
            rs_log_error("unlink %s failed: %s", filename, strerror(errno));
            ret = EXIT_IO_ERROR;
        }
    }

    free(filename);

    return 0;
}



/**
 * Return the mtime for a timestamp file.
 *
 * If the timestamp doesn't exist then we count it as time zero.
 **/
int dcc_check_timefile(const char *lockname,
                       const struct dcc_hostdef *host,
                       time_t *mtime)
{
    char *filename;
    struct stat sb;
    int ret;

    if ((ret = dcc_make_lock_filename(lockname, host, 0, &filename)))
        return ret;

    if (stat(filename, &sb) == -1) {
        *mtime = (time_t) 0;
        if (errno == ENOENT) {
            /* just no record for this file; that's fine. */
            free(filename);
            return 0;
        } else {
            rs_log_error("stat %s failed: %s", filename, strerror(errno));
            free(filename);
            return EXIT_IO_ERROR;
        }
    }

    *mtime = sb.st_mtime;

    free(filename);

    return 0;
}
