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

#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "types.h"
#include "distcc.h"
#include "rpc.h"
#include "trace.h"
#include "exitcode.h"
#include "snprintf.h"
#include "util.h"

const char *dcc_state_prefix = "binstate_";


static struct dcc_task_state *my_state = NULL;
static struct dcc_task_state local_state, remote_state;

static struct dcc_task_state *direct_my_state(const enum dcc_host target);

/**
 * @file
 *
 * This file provides a way for distcc processes to make little notes
 * about what they're up to that can be read by a monitor process.
 *
 * State is stored as follows.
 *
 * Within our temporary directory, we create a subdirectory called "state".
 *
 * Each process creates a file named "binstate%d", for its pid.  We
 * always rewrite this file from the beginning.
 *
 * Inside each of these, we store a binary struct in the native host
 * encoding.  Ugly, but quick and easy both in code and CPU time.
 *
 * Any process reading these files needs to handle the fact that they may be
 * truncated or otherwise incorrect.
 *
 * When the process exits, it removes its state file.  If you didn't
 * notice it already, it's too late now.
 *
 * In addition, if the process identified by the file no longer
 * exists, then the file must be orphaned by a process that suddenly
 * terminated.  The file is ignored and can be deleted by the first
 * process that notices it.
 *
 * The reader interface for these files is in mon.c
 *
 * These files are considered a private format, and they may change
 * between distcc releases.  The only supported way to read them is
 * through mon.c.
 **/


/**
 * Return newly allocated buffer holding the name of this process's state file.
 *
 * (This can't reliably be static because we might fork...)
 **/
static int dcc_get_state_filename(char **fname)
{
    int ret;
    char *dir;

    if ((ret = dcc_get_state_dir(&dir)))
        return ret;

    if (asprintf(fname, "%s/%s%ld",
                 dir, dcc_state_prefix, (long) getpid()) == -1) {
        return EXIT_OUT_OF_MEMORY;
    }

    return 0;
}


const char *dcc_get_phase_name(enum dcc_phase phase)
{
    switch (phase) {
    case DCC_PHASE_STARTUP:
        return "Startup";
    case DCC_PHASE_BLOCKED:
        return "Blocked";
    case DCC_PHASE_COMPILE:
        return "Compile";
    case DCC_PHASE_CPP:
        return "Preprocess";
    case DCC_PHASE_CONNECT:
        return "Connect";
    case DCC_PHASE_SEND:
        return "Send";
    case DCC_PHASE_RECEIVE:
        return "Receive";
    case DCC_PHASE_DONE:
        return "Done";
    default:
        return "Unknown";
    }
}


/**
 * Get a file descriptor for writing to this process's state file.
 * file.
 **/
static int dcc_open_state(int *p_fd,
                          const char *fname)
{
    int fd;

    fd = open(fname, O_CREAT|O_WRONLY|O_TRUNC|O_BINARY, 0666);
    if (fd == -1) {
        rs_log_error("failed to open %s: %s", fname, strerror(errno));
        return EXIT_IO_ERROR;
    }

    *p_fd = fd;
    return 0;
}


/**
 * Remove the state file for this process.
 *
 * This can be called from atexit().
 **/
void dcc_remove_state_file (void)
{
    char *fname;
    int ret;

    if ((ret = dcc_get_state_filename(&fname)))
        return;

    if (unlink(fname) == -1) {
        /* It's OK if we never created it */
        if (errno != ENOENT) {
            rs_log_warning("failed to unlink %s: %s", fname, strerror(errno));
            ret = EXIT_IO_ERROR;
        }
    }

    free(fname);

    (void) ret;
}


static int dcc_write_state(int fd)
{
    int ret;

    /* Write out as one big blob.  fd is positioned at the start of
     * the file. */

    if ((ret = dcc_writex(fd, my_state, sizeof *my_state)))
        return ret;

    return 0;
}


/**
 * Record the state of this process.
 *
 * The filename is trimmed down to its basename.
 *
 * If the source_file or host are NULL, then are left unchanged from
 * their previous value.
 **/
int dcc_note_state(enum dcc_phase state,
                   const char *source_file,
                   const char *host, enum dcc_host target)
{
    int fd;
    int ret;
    char *fname;
    struct timeval tv;


	if (!direct_my_state(target))
		return -1;

    my_state->struct_size = sizeof *my_state;
    my_state->magic = DCC_STATE_MAGIC;
    my_state->cpid = (unsigned long) getpid();

    if ((ret = dcc_get_state_filename(&fname)))
        return ret;

    source_file = dcc_find_basename(source_file);
    if (source_file) {
        strlcpy(my_state->file, source_file, sizeof my_state->file);
    }

    if (host) {
        strlcpy(my_state->host, host, sizeof my_state->host);
    }

    if (gettimeofday(&tv, NULL) == -1) {
        rs_log_error("gettimeofday failed: %s", strerror(errno));
        return EXIT_DISTCC_FAILED;
    }
    my_state->curr_phase = state;

    rs_trace("note state %d, file \"%s\", host \"%s\"",
             state,
             source_file ? source_file : "(NULL)",
             host ? host : "(NULL)");

    if ((ret = dcc_open_state(&fd, fname))) {
        free(fname);
        return ret;
    }

    if ((ret = dcc_write_state(fd))) {
        dcc_close(fd);
        free(fname);
        return ret;
    }

    dcc_close(fd);
    free(fname);

    return 0;
}


void dcc_note_state_slot(int slot, enum dcc_host target)
{
	if (direct_my_state(target))
		my_state->slot = slot;
}


/**
	Point 'my_state' to the local or remote host state information, depending on target.

	Return 'my_state' pointer.
**/
static struct dcc_task_state *direct_my_state(const enum dcc_host target)
{
	switch (target)
	{
		case DCC_LOCAL:
			my_state = &local_state;
			break;

		case DCC_REMOTE:
			my_state = &remote_state;
			break;

		case DCC_UNKNOWN:
			break;
	}

	if (!my_state)
		rs_log_error("my_state == NULL");

	return my_state;
}
