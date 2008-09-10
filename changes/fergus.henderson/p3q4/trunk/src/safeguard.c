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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"


/**
 * @file
 * @brief Protect against unbounded recursion.
 *
 * It would be fairly easy for somebody to get confused in masquerade mode and
 * try to get distcc to invoke itself in a loop.  We can't always work out the
 * right thing to do but we can at least flag an error.
 *
 * This environment variable is set to guard against distcc accidentally
 * recursively invoking itself, thinking it's the real compiler.
 **/

static const char dcc_safeguard_name[] = "_DISTCC_SAFEGUARD";
static char dcc_safeguard_set[] = "_DISTCC_SAFEGUARD=1";
static int dcc_safeguard_level;

int dcc_recursion_safeguard(void)
{
    char *env = getenv(dcc_safeguard_name);

    if (env) {
        rs_trace("safeguard: %s", env);
        if (!(dcc_safeguard_level = atoi(env)))
            dcc_safeguard_level = 1;
    }
    else
        dcc_safeguard_level = 0;
    rs_trace("safeguard level=%d", dcc_safeguard_level);

    return dcc_safeguard_level;
}


int dcc_increment_safeguard(void)
{
    if (dcc_safeguard_level > 0)
    dcc_safeguard_set[sizeof dcc_safeguard_set-2] = dcc_safeguard_level+'1';
    rs_trace("setting safeguard: %s", dcc_safeguard_set);
    if ((putenv(strdup(dcc_safeguard_set)) == -1)) {
        rs_log_error("putenv failed");
        /* and continue */
    }

    return 0;
}
