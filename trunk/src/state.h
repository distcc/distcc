/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003 by Martin Pool <mbp@samba.org>
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

#ifndef _DISTCC_STATE_H
#define _DISTCC_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

int dcc_get_state_dir (char **p);
int dcc_open_state_file (int *p_fd);


/* Note that these must be in the order in which they are encountered
 * for the state file to work properly.  It's OK if some are skipped
 * though. */
enum dcc_phase {
    DCC_PHASE_STARTUP,
    DCC_PHASE_BLOCKED,
    DCC_PHASE_CONNECT,
    DCC_PHASE_CPP,
    DCC_PHASE_SEND,
    DCC_PHASE_COMPILE,          /**< or unknown */
    DCC_PHASE_RECEIVE,
    DCC_PHASE_DONE              /**< MUST be last */
};

enum dcc_host {
	DCC_UNKNOWN,
	DCC_LOCAL,
	DCC_REMOTE
};

int dcc_note_state (enum dcc_phase state,
                    const char *file,
                    const char *host,
                    enum dcc_host);
void dcc_remove_state_file (void);


extern const char *dcc_state_prefix;


#define DCC_STATE_MAGIC 0x44494800 /* DIH\0 */

/**
 * State and history of a distcc process.  Used in memory and also in native
 * format for binary state files.
 *
 * This should be <4kB, so that it will normally be written out
 * atomically.
 **/
struct dcc_task_state {
    size_t struct_size;
    unsigned long magic;
    unsigned long cpid;          /**< Client pid */
    char file[128];             /**< Input filename  */
    char host[128];             /**< Destination host description */
    int slot;                   /**< Which CPU slot for this host */

    enum dcc_phase curr_phase;

    /** In memory, point to the next in a list of all tasks.  In the
     * file, undefined. */
    struct dcc_task_state *next;
};


const char *dcc_get_phase_name(enum dcc_phase);

void dcc_note_state_slot(int slot, enum dcc_host target);

#ifdef __cplusplus
}
#endif

#endif /* _DISTCC_STATE_H */
