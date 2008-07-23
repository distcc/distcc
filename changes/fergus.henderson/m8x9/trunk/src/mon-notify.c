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

#include <sys/types.h>
#include <sys/stat.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <dirent.h>

#include "types.h"
#include "rpc.h"
#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "snprintf.h"
#include "mon.h"
#include "util.h"
#include "netutil.h"




static volatile int pipe_fd[2];


static void dcc_mon_siginfo_handler (int UNUSED (whatsig))
{
    /* Just ignore any errors.  If people aren't listening or the pipe
     * is full, too bad. */
    write (pipe_fd[1], "*", 1);
}


/*
 * Try to setup dnotify on the state directory.  This returns the
 * descriptor of a pipe in @p dummy_fd.  Every time the state changes,
 * a single byte is written to this pipe.  A caller who select()s on
 * the pipe will therefore be woken every time there is a change.
 *
 * If we can do dnotify, create the dummy pipe and turn it on.
 *
 * @fixme One problem here is that if the state directory is deleted
 * and recreated, then we'll never notice and find the new one.  I
 * don't know of any good fix, other than perhaps polling every so
 * often.  So just don't do that.
 *
 * @fixme If this function is called repeatedly it will leak FDs.
 *
 * @todo Reimplement this on top of kevent for BSD.
 */
int dcc_mon_setup_notify (int *dummy_fd)
{
#ifdef F_NOTIFY
    char *state_dir;
    int ret;
    int fd;

    if (signal (SIGIO, dcc_mon_siginfo_handler) == SIG_ERR) {
        rs_log_error ("signal(SIGINFO) failed: %s", strerror(errno));
        return EXIT_IO_ERROR;
    }

    if (pipe ((int *) pipe_fd) == -1) {
        rs_log_error ("pipe failed: %s", strerror (errno));
        return EXIT_IO_ERROR;
    }

    *dummy_fd = pipe_fd[0];     /* read end */

    dcc_set_nonblocking (pipe_fd[0]);
    dcc_set_nonblocking (pipe_fd[1]);

    if ((ret = dcc_get_state_dir (&state_dir)))
        return ret;

    if ((fd = open (state_dir, O_RDONLY)) == -1) {
        rs_log_error ("failed to open %s: %s", state_dir, strerror (errno));
        free (state_dir);
        return EXIT_IO_ERROR;
    }

    /* CAUTION!  Signals can start arriving immediately.  Be ready. */

    if (fcntl (fd, F_NOTIFY, DN_RENAME|DN_DELETE|DN_MULTISHOT) == -1) {
        rs_log_warning ("setting F_NOTIFY failed: %s",
                        strerror (errno));
        free (state_dir);
        return EXIT_IO_ERROR;
    }

    return 0;
#else /* F_NOTIFY */
    return EXIT_IO_ERROR;
#endif /* F_NOTIFY */
}
