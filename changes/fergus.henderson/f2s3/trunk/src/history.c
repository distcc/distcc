/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2004 by Martin Pool <mbp@samba.org>
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
#include <stdlib.h>

#include "distcc.h"
#include "mon.h"
#include "trace.h"

/* Number of previous states to retain for drawing history. */
const int dcc_max_history_queue = 200;


void
dcc_history_push(struct dcc_history *history,
                 enum dcc_phase new_state)
{
     history->now = (history->now + 1) % history->len;
     history->past_phases[history->now] = new_state;
}


struct dcc_history*
dcc_history_new(void)
{
     struct dcc_history *history;
     int i;

     history = malloc(sizeof *history);
     if (!history) {
          rs_log_crit("allocation failed!");
          return NULL;
     }
     history->len = dcc_max_history_queue;
     history->now = 0;
     history->past_phases = malloc(history->len * (sizeof *history->past_phases));
     if (!history->past_phases) {
          rs_log_crit("history allocation failed");
          return NULL;
     }

     for (i = 0; i < history->len; i++)
          history->past_phases[i] = DCC_PHASE_DONE;

     return history;
}
