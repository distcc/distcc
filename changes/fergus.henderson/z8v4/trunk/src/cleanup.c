/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool
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

        /* "I have not come through fire and death to bandy crooked
         * words with a serving-man until the lightning falls!"
         *      -- Gandalf (BBC LoTR radio play) */


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "distcc.h"
#include "exitcode.h"
#include "trace.h"
#include "util.h"

/**
 * A list of files that need to be cleaned up on exit.
 *
 * Volatile because it can be read from signal handlers.
 **/
char *volatile *volatile cleanups = 0;   /* Dynamically allocated array. */
volatile int cleanups_size = 0; /* The length of the array. */
volatile int n_cleanups = 0;    /* The number of entries used. */

static void dcc_cleanup_tempfiles_inner(int from_signal_handler);

void dcc_cleanup_tempfiles(void)
{
    dcc_cleanup_tempfiles_inner(0);
}

void dcc_cleanup_tempfiles_from_signal_handler(void)
{
    dcc_cleanup_tempfiles_inner(1);
}

/*
 * You can call this at any time, or hook it into atexit().  It is
 * safe to call repeatedly.
 *
 * If from_signal_handler (a boolean) is non-zero, it means that
 * we're being called from a signal handler, so we need to be
 * careful not to call malloc() or free() or any other functions
 * that might not be async-signal-safe.
 * (We do call the tracing functions, which is perhaps unsafe
 * because they call sprintf() if DISCC_VERBOSE=1 is enabled.
 * But in that case it is probably worth the very small risk
 * of a crash to get the full tracing output.)
 *
 * If $DISTCC_SAVE_TEMPS is set to "1", then files are not actually
 * deleted, which can be good for debugging.  However, we still need
 * to remove them from the list, otherwise it will eventually overflow
 * in prefork mode.
 */
static void dcc_cleanup_tempfiles_inner(int from_signal_handler)
{
    int i;
    int done = 0;
    int save = dcc_getenv_bool("DISTCC_SAVE_TEMPS", 0);

    /* do the unlinks from the last to the first file.
     * This way, directories get deleted after their files. */

     /* tempus fugit */
    for (i = n_cleanups - 1; i >= 0; i--) {
        if (save) {
            rs_trace("skip cleanup of %s", cleanups[i]);
        } else {
            /* Try removing it as a directory first, and
             * if that fails, try removing is as a file.
             * Report the error from removing-as-a-file
             * if both fail. */
            if ((rmdir(cleanups[i]) == -1) &&
                (unlink(cleanups[i]) == -1) &&
                (errno != ENOENT)) {
                rs_log_notice("cleanup %s failed: %s", cleanups[i],
                              strerror(errno));
            }
            done++;
        }
        n_cleanups = i;
        if (from_signal_handler) {
            /* It's not safe to call free() in this case.
             * Don't worry about the memory leak - we're about
             * to exit the process anyway. */
        } else {
            free(cleanups[i]);
        }
        cleanups[i] = NULL;
    }

    rs_trace("deleted %d temporary files", done);
}


/**
 * Add to the list of files to delete on exit.
 * If it runs out of memory, it returns non-zero.
 */
int dcc_add_cleanup(const char *filename)
{
    char *new_filename;
    int new_n_cleanups = n_cleanups + 1;

    /* Increase the size of the cleanups array, if needed.
     * We avoid using realloc() here, to ensure that 'cleanups' remains
     * valid at all times - we might get a signal in the middle here
     * that could call dcc_cleanup_tempfiles_from_signal_handler(). */
    if (new_n_cleanups > cleanups_size) {
        char **old_cleanups;
        int new_cleanups_size = (cleanups_size == 0 ? 10 : cleanups_size * 3);
        char **new_cleanups = malloc(new_cleanups_size * sizeof(char *));
        if (new_cleanups == NULL) {
            rs_log_crit("malloc failed - too many cleanups");
            return EXIT_OUT_OF_MEMORY;
        }
        memcpy(new_cleanups, (char **)cleanups, cleanups_size * sizeof(char *));
        old_cleanups = (char **)cleanups;
        cleanups = new_cleanups;           /* Atomic assignment. */
        cleanups_size = new_cleanups_size; /* Atomic assignment. */
        free(old_cleanups);
    }

    new_filename = strdup(filename);
    if (new_filename == NULL) {
        rs_log_crit("strdup failed - too many cleanups");
        return EXIT_OUT_OF_MEMORY;
    }

    cleanups[new_n_cleanups - 1] = new_filename; /* Atomic assignment. */
    n_cleanups = new_n_cleanups;                 /* Atomic assignment. */

    return 0;
}
