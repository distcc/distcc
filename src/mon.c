/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003, 2004 by Martin Pool <mbp@samba.org>
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
#include <sys/socket.h>

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
#include "distcc.h"
#include "rpc.h"
#include "trace.h"
#include "exitcode.h"
#include "snprintf.h"
#include "mon.h"
#include "util.h"


/**
 * @file
 *
 * Common routines for monitoring compiler state.
 *
 * Every time the client wants an update, it can call dcc_mon_poll(),
 * which returns a newly allocated list of all running processes.
 *
 * The list is returned sorted by hostname and then by slot, so tasks
 * will be more stable from one call to the next.
 **/


/* TODO: Shouldn't fail if the directory doesn't exist at the moment
 * it's called.
 */


/*
 * State files older than this are assumed to be leftovers from
 * compilers that died off.  It's possible for a remote compilation to
 * take a very long time, and combined with the check that the process
 * exists we can allow this to be reasonably large.
 */
const int dcc_phase_max_age = 60;


/**
 * Check if the state file @p fd is too old to be believed -- probably
 * because it was left over from a client that was killed.
 *
 * If so, close @p fd, unlink the file, and return EXIT_GONE.
 *
 * fd is closed on failure.
 **/
static int dcc_mon_kill_old(int fd,
                            char *fullpath)
{
    struct stat st;
    time_t now;

    /* Check if the file is old. */
    if (fstat(fd, &st) == -1) {
        dcc_close(fd);
        rs_log_warning("error statting %s: %s", fullpath, strerror(errno));
        return EXIT_IO_ERROR;
    }
    time(&now);

    /* Time you hear the siren / it's already too late */
    if (now - st.st_mtime > dcc_phase_max_age) {
        dcc_close(fd);          /* close first for windoze */
        rs_trace("unlink %s", fullpath);
        if (unlink(fullpath) == -1) {
            rs_log_warning("unlink %s failed: %s", fullpath, strerror(errno));
            return EXIT_IO_ERROR;
        }
        return EXIT_GONE;
    }

    return 0;
}


static int dcc_mon_read_state(int fd, char *fullpath,
                              struct dcc_task_state *lp)
{
    int nread;

    /* Don't use dcc_readx(), because not being able to read it is not
     * a big deal. */
    nread = read(fd, lp, sizeof *lp);
    if (nread == -1) {
        rs_trace("failed to read state from %s: %s",
                 fullpath, strerror(errno));
        return EXIT_IO_ERROR;
    } else if (nread == 0) {
        /* empty file; just bad timing. */
        return EXIT_IO_ERROR;
    } else if (nread != sizeof *lp) {
        rs_trace("short read getting state from %s",
                 fullpath);
        return EXIT_IO_ERROR;
    }

    /* sanity-check some fields */

    if (lp->magic != DCC_STATE_MAGIC) {
        rs_log_warning("wrong magic number: %s",
                       fullpath);
        return EXIT_IO_ERROR;
    }

    if (lp->struct_size != sizeof (struct dcc_task_state)) {
        rs_log_warning("wrong structure size: %s: version mismatch?",
                       fullpath);
        return EXIT_IO_ERROR;
    }

    lp->file[sizeof lp->file - 1] = '\0';
    lp->host[sizeof lp->host - 1] = '\0';
    if (lp->curr_phase > DCC_PHASE_DONE) {
        lp->curr_phase = DCC_PHASE_COMPILE;
    }

    lp->next = 0;

    return 0;
}


/**
 * Check that the process named by the file still exists; if not,
 * return EXIT_GONE.
 **/
static int dcc_mon_check_orphans(struct dcc_task_state *monl)
{
    /* signal 0 just checks if it exists */
    if (!kill(monl->cpid, 0)) {
        return 0;               /* it's here */
    } else if (errno == EPERM) {
        /* It's here, but it's not ours.  Assume it's still a real
         * distcc process. */
        return 0;
    } else if (errno == ESRCH) {
        return EXIT_GONE;       /* no such pid */
    } else {
        rs_log_warning("kill %ld, 0 failed: %s", (long) monl->cpid,
                       strerror(errno));
        return EXIT_GONE;
    }
}


/**
 * Read state.  If loaded successfully, store a pointer to the newly
 * allocated structure into *ppl.
 */
static int dcc_mon_load_state(int fd,
                              char *fullpath,
                              struct dcc_task_state **ppl)
{
    int ret;
    struct dcc_task_state *tl;

    tl = calloc(1, sizeof *tl);
    if (!tl) {
        rs_log_crit("failed to allocate dcc_task_state");
        return EXIT_OUT_OF_MEMORY;
    }

    ret = dcc_mon_read_state(fd, fullpath, tl);
    if (ret) {
        dcc_task_state_free(tl);
        *ppl = NULL;
        return ret;
    }

    if (tl->curr_phase != DCC_PHASE_DONE) {
        ret = dcc_mon_check_orphans(tl);
        if (ret) {
            dcc_task_state_free(tl);
            *ppl = NULL;
            return ret;
        }
    }

    *ppl = tl;

    return ret;
}


/* Free the whole list */
int dcc_task_state_free(struct dcc_task_state *lp)
{
    struct dcc_task_state *next;

    while (lp) {
        next = lp->next;        /* save from clobbering */

        free(lp);

        /* nothing dynamically allocated in them anymore */
        lp = next;
    }

    return 0;
}


/**
 * Read in @p filename from inside @p dirname, and try to parse it as
 * a status file.
 *
 * If a new entry is read, a pointer to it is returned in @p lp.
 **/
static int dcc_mon_do_file(char *dirname, char *filename,
                           struct dcc_task_state **lp)
{
    int fd;
    char *fullpath;
    int ret;

    *lp = NULL;

    /* Is this a file we want to see */
    if (!str_startswith(dcc_state_prefix, filename)) {
/*         rs_trace("skipped"); */
        return 0;
    }

    checked_asprintf(&fullpath, "%s/%s", dirname, filename);
    if (fullpath == NULL) {
      return EXIT_OUT_OF_MEMORY;
    }
    rs_trace("process %s", fullpath);

    /* Remember that the file might disappear at any time, so open it
     * now so that we can hang on. */
    if ((fd = open(fullpath, O_RDONLY|O_BINARY, 0)) == -1) {
        if (errno == ENOENT) {
            rs_trace("%s disappeared", fullpath);
            ret = 0;
            goto out_free;
        } else { /* hm */
            rs_log_warning("failed to open %s: %s",
                           fullpath, strerror(errno));
            ret = EXIT_IO_ERROR;
            goto out_free;
        }
    }

    if ((ret = dcc_mon_kill_old(fd, fullpath))) {
        /* closes fd on failure */
        goto out_free;
    }

    ret = dcc_mon_load_state(fd, fullpath, lp);

    dcc_close(fd);

    out_free:
    free(fullpath);
    return ret;           /* ok */
}


/**
 * Insert @p new into the list at the appropriate sorted position.
 **/
static void dcc_mon_insert_sorted(struct dcc_task_state **list,
                                  struct dcc_task_state *new)
{
    int s;
    struct dcc_task_state *i;

    for (; (i = *list) != NULL; list = &i->next) {
        /* Should we go before *list?  If the hostname comes first, or
         * the name is the same and the slot is lower. */
        s = strcmp(i->host, new->host);

        if (s > 0) {
            /* new's host is earlier */
            break;
        } else if (s == 0) {
            /* same host; compare slots */
            if (new->slot < i->slot)
                break;
        }
    }

    /* OK, insert it before the current contents of *list, which may
     * be NULL */
    *list = new;
    new->next = i;
}


/**
 * Read through the state directory and return information about all
 * processes we find there.
 *
 * This function has to handle any files in there that happen to be
 * corrupt -- that can easily happen if e.g. a client crashes or is
 * interrupted, or is even just in the middle of writing its file.
 **/
int dcc_mon_poll(struct dcc_task_state **p_list)
{
    int ret;
    char *dirname;
    DIR *d;
    struct dirent *de;

    *p_list = NULL;

    if ((ret = dcc_get_state_dir(&dirname)))
        return ret;

    if ((d = opendir(dirname)) == NULL) {
        rs_log_error("failed to opendir %s: %s", dirname, strerror(errno));
        ret = EXIT_IO_ERROR;
        return ret;
    }

    while ((de = readdir(d)) != NULL) {
        struct dcc_task_state *pthis;
        if (dcc_mon_do_file(dirname, de->d_name, &pthis) == 0
            && pthis) {
            /* We can succeed without getting a new entry back, but it
             * turns out that this time we did get one.  So insert it
             * into the right point on the list. */
            dcc_mon_insert_sorted(p_list, pthis);
        }
    }

    closedir(d);

    return 0;
}
