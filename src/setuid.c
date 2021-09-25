/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <string.h>

#include "distcc.h"
#include "trace.h"
#include "daemon.h"
#include "exitcode.h"

#ifdef __linux__
#include <sys/prctl.h>
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS     38
#endif
#endif

const char *opt_user = "distcc";


/**
 * @file
 *
 * Functions for setting the daemon's persona.
 *
 * It is better to create separate userids for daemons rather than to just use
 * "nobody".
 *
 * Personas may be specified either as a name or an ID.
 **/

/**
 * Try to find an appropriate uid,gid to change to.
 *
 * In order, we try "distcc" or the user on the command line, or "nobody", or
 * failing that the traditional value for nobody of 65534.
 */
static int dcc_preferred_user(uid_t *puid, gid_t *pgid)
{
    struct passwd *pw;

    if ((pw = getpwnam(opt_user))) {
        *puid = pw->pw_uid;
        *pgid = pw->pw_gid;
        return 0;               /* cool */
    }
    /* Note getpwnam() does not set errno */
    rs_log_warning("no such user as \"%s\"", opt_user);
    /* try something else */

    if ((pw = getpwnam("nobody"))) {
        *puid = pw->pw_uid;
        *pgid = pw->pw_gid;
        return 0;               /* cool */
    }

    /* just use traditional value */
    *puid = *pgid = 65534;
    return 0;
}


/**
 * Make sure that distccd never runs as root, by discarding privileges if we
 * have them.
 *
 * This used to also check gid!=0, but on BSD that is group wheel and is
 * apparently common for daemons or users.
 *
 * This is run before dissociating from the calling terminal so any errors go
 * to stdout.
 **/
int dcc_discard_root(void)
{
    uid_t uid;
    gid_t gid;
    int ret;

    if (getuid() != 0  &&  geteuid() != 0) {
        /* Already not root.  No worries. */
        return 0;
    }

    if ((ret = dcc_preferred_user(&uid, &gid)) != 0)
        return ret;

    /* GNU C Library Manual says that when run by root, setgid() and setuid()
     * permanently discard privileges: both the real and effective uid are
     * set. */

    if (setgid(gid)) {
        rs_log_error("setgid(%d) failed: %s", (int) gid, strerror(errno));
        return EXIT_SETUID_FAILED;
    }

#ifdef HAVE_SETGROUPS
    /* Get rid of any supplementary groups this process might have
     * inherited. */
    /* XXX: OS X Jaguar broke setgroups so that setting it to 0 fails. */
    if (setgroups(1, &gid)) {
        rs_log_error("setgroups failed: %s", strerror(errno));
        return EXIT_SETUID_FAILED;
    }
#endif

    if (setuid(uid)) {
        rs_log_error("setuid(%d) failed: %s", (int) uid, strerror(errno));
        return EXIT_SETUID_FAILED;
    }

    if (getuid() == 0  ||  geteuid() == 0) {
        rs_log_crit("still have root privileges after trying to discard them!");
        return EXIT_SETUID_FAILED;
    }

#ifdef HAVE_LINUX
    /* On Linux changing the effective user or group ID clears the process's
     * "dumpable" flag, which makes all files in the /proc/self/ directory
     * owned by root and therefore unmodifiable by the process itself.
     * Set the flag again here so we can, e.g., change oom_score_adj. */
    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0)
        rs_log_warning("failed to restore dumpable process flag: %s", strerror(errno));
#endif

#ifdef __linux__
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0)
        rs_trace("successfully set no_new_privs");
#endif

    rs_trace("discarded root privileges, changed to uid=%d gid=%d", (int) uid, (int) gid);
    return 0;
}
