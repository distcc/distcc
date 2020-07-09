/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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


                /* "More computing sins are committed in the name of
                 * efficiency (without necessarily achieving it) than
                 * for any other single reason - including blind
                 * stupidity."  -- W.A. Wulf
                 */



#include <config.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "snprintf.h"
#include "exitcode.h"

#ifdef __CYGWIN32__
    #define NOGDI
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif


/**
 * @file
 *
 * Routines for naming, generating and removing temporary files.
 *
 * Temporary files are stored under $TMPDIR or /tmp.
 *
 * From 2.10 on, our lock and state files are not stored in there.
 *
 * It would be nice if we could use a standard function, but I don't
 * think any of them are adequate: we need to control the extension
 * and know the filename.  tmpfile() does not give back the filename.
 * tmpnam() is insecure.  mkstemp() does not allow us to set the
 * extension.
 *
 * It sucks that there is no standard function.  The implementation
 * below is inspired by the __gen_tempname() code in glibc; hopefully
 * it will be secure on all platforms.
 *
 * We need to touch the filename before running commands on it,
 * because we cannot be sure that the compiler will create it
 * securely.
 *
 * Even with all this, we are not necessarily secure in the presence
 * of a tmpreaper if the attacker can play timing tricks.  However,
 * since we are not setuid and since there is no completely safe way
 * to write tmpreapers, this is left alone for now.
 *
 * If you're really paranoid, you should just use per-user TMPDIRs.
 *
 * @sa http://www.dwheeler.com/secure-programs/Secure-Programs-HOWTO/avoid-race.html#TEMPORARY-FILES
 **/

/**
 * Create the directory @p path, and register it for deletion when this
 * compilation finished.  If it already exists as a directory
 * we succeed, but we don't register the directory for deletion.
 **/
int dcc_mk_tmpdir(const char *path)
{
    struct stat buf;
    int ret;

    if (stat(path, &buf) == -1) {
        if (mkdir(path, 0777) == -1) {
            return EXIT_IO_ERROR;
        }
        if ((ret = dcc_add_cleanup(path))) {
            /* bailing out */
            rmdir(path);
            return ret;
        }
    } else {
        /* we could stat the file successfully; if it's a directory,
         * all is well, but we should not it delete later, since we did
         * not make it.
         */
         if (S_ISDIR(buf.st_mode)) {
             return 0;
         } else {
             rs_log_error("mkdir '%s' failed: %s", path, strerror(errno));
             return EXIT_IO_ERROR;
         }
    }
    return 0;
}

/**
 * Create the directory @p path.  If it already exists as a directory
 * we succeed.
 **/
int dcc_mkdir(const char *path)
{
    if ((mkdir(path, 0777) == -1) && (errno != EEXIST)) {
        rs_log_error("mkdir '%s' failed: %s", path, strerror(errno));
        return EXIT_IO_ERROR;
    }

    return 0;
}


#ifndef HAVE_MKDTEMP
static char* mkdtemp(char *pattern)
{
    /* We could try this a few times if we wanted */
    char* path = mktemp(pattern);
    if (path == NULL)
        return NULL;
    if (mkdir(path, 0700) == 0)
        return path;
    return NULL;
}
#endif

/* This function creates a temporary directory, to be used for
 * all (input) files during one compilation.
 * The name of the directory is stored in @p tempdir, which is
 * malloc'ed here. The caller is responsible for freeing it.
 * The format of the directory name is <TMPTOP>/distccd_<randomnumber>
 * Returns the new temp dir in tempdir.
 */
int dcc_get_new_tmpdir(char **tempdir)
{
    int ret;
    const char *tmp_top;
    char *s;
    if ((ret = dcc_get_tmp_top(&tmp_top))) {
        return ret;
    }
    if (asprintf(&s, "%s/distccd_XXXXXX", tmp_top) == -1)
        return EXIT_OUT_OF_MEMORY;
    if ((*tempdir = mkdtemp(s)) == 0) {
        return EXIT_IO_ERROR;
    }
    if ((ret = dcc_add_cleanup(s))) {
        /* bailing out */
        rmdir(s);
        return ret;
    }
    return 0;
}

/* This function returns a directory-name, it does not end in a slash. */
int dcc_get_tmp_top(const char **p_ret)
{
#ifdef __CYGWIN32__
    int ret;

    char *s = malloc(MAXPATHLEN+1);
    int f,ln;
    GetTempPath(MAXPATHLEN+1,s);
    /* Convert slashes */
    for (f = 0, ln = strlen(s); f != ln; f++)
        if (s[f]=='\\') s[f]='/';
    /* Delete trailing slashes -- but leave one slash if s is all slashes */
    for (f = strlen(s)-1; f > 0 && s[f] == '/'; f--)
        s[f]='\0';

    if ((ret = dcc_add_cleanup(s))) {
        free(s);
        return ret;
    }
    *p_ret = s;
    return 0;
#else
    const char *d;

    d = getenv("TMPDIR");

    if (!d || d[0] == '\0') {
        *p_ret = "/tmp";
        return 0;
    } else {
        *p_ret = d;
        return 0;
    }
#endif
}

/**
 * Create the full @path. If it already exists as a directory
 * we succeed.
 */
int dcc_mk_tmp_ancestor_dirs(const char *path)
{
    char *copy = 0;
    char *p;
    int ret;

    copy = strdup(path);
    if (copy == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }

    dcc_truncate_to_dirname(copy);
    if (copy[0] == '\0') {
        free(copy);
        return 0;
    }

    /* First, let's try and see if all parent directories
     * exist already */
    if ((ret = dcc_mk_tmpdir(copy)) == 0) {
        free(copy);
        return 0;
    }

    /* This is the "pessimistic" algorithm for making directories,
     * which assumes that most directories that it's asked to create
     * do not exist. It's expensive for very deep directories;
     * it tries to make all the directories from the root to that
     * dir. However, it only gets called if we tried to make a dir
     * in a directory and failed; which means we only get called
     * once per directory.
     */
    /* Body of this loop does not execute when *p=='\0';
     * therefore the very last component of the directory does not
     * get created here.
     */
    for (p = copy; *p != '\0'; ++p) {
        if (*p == '/' && p != copy) {
            *p = '\0';
            if ((ret = dcc_mk_tmpdir(copy))) {
                free(copy);
                return ret;
            }
            *p = '/';
        }
    }
    ret = dcc_mk_tmpdir(copy);
    free(copy);
    return ret;
}

/**
 * Return a static string holding DISTCC_DIR, or ~/.distcc.
 * The directory is created if it does not exist.
 **/
int dcc_get_top_dir(char **path_ret)
{
    char *env;
    static char *cached;
    int ret;

    if (cached) {
        *path_ret = cached;
        return 0;
    }

    if ((env = getenv("DISTCC_DIR"))) {
        if ((cached = strdup(env)) == NULL) {
            return EXIT_OUT_OF_MEMORY;
        } else {
            *path_ret = cached;
            return 0;
        }
    }

    if ((env = getenv("HOME")) == NULL) {
        rs_log_warning("HOME is not set; can't find distcc directory");
        return EXIT_BAD_ARGUMENTS;
    }

    if (asprintf(path_ret, "%s/.distcc", env) == -1) {
        rs_log_error("asprintf failed");
        return EXIT_OUT_OF_MEMORY;
    }

    ret = dcc_mkdir(*path_ret);
    if (ret == 0)
        cached = *path_ret;
    return ret;
}


/**
 * Return a subdirectory of the DISTCC_DIR of the given name, making
 * sure that the directory exists.
 **/
int dcc_get_subdir(const char *name,
                          char **dir_ret)
{
    int ret;
    char *topdir;

    if ((ret = dcc_get_top_dir(&topdir)))
        return ret;

    if (asprintf(dir_ret, "%s/%s", topdir, name) == -1) {
        rs_log_error("asprintf failed");
        return EXIT_OUT_OF_MEMORY;
    }

    return dcc_mkdir(*dir_ret);
}

int dcc_get_lock_dir(char **dir_ret)
{
    static char *cached;
    int ret;

    if (cached) {
        *dir_ret = cached;
        return 0;
    } else {
        ret = dcc_get_subdir("lock", dir_ret);
        if (ret == 0)
            cached = *dir_ret;
        return ret;
    }
}

int dcc_get_state_dir(char **dir_ret)
{
    static char *cached;
    int ret;

    if (cached) {
        *dir_ret = cached;
        return 0;
    } else {
        ret = dcc_get_subdir("state", dir_ret);
        if (ret == 0)
            cached = *dir_ret;
        return ret;
    }
}



/**
 * Create a file inside the temporary directory and register it for
 * later cleanup, and return its name.
 *
 * The file will be reopened later, possibly in a child.  But we know
 * that it exists with appropriately tight permissions.
 **/
int dcc_make_tmpnam(const char *prefix,
                    const char *suffix,
                    char **name_ret)
{
    char *s = NULL;
    const char *tempdir;
    int ret;
    unsigned long random_bits;
    int fd;

    if ((ret = dcc_get_tmp_top(&tempdir)))
        return ret;

    if (access(tempdir, W_OK|X_OK) == -1) {
        rs_log_error("can't use TMPDIR \"%s\": %s", tempdir, strerror(errno));
        return EXIT_IO_ERROR;
    }

    random_bits = (unsigned long) getpid() << 16;

# if HAVE_GETTIMEOFDAY
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        random_bits ^= tv.tv_usec << 16;
        random_bits ^= tv.tv_sec;
    }
# else
    random_bits ^= time(NULL);
# endif

#if 0
    random_bits = 0;            /* FOR TESTING */
#endif

    do {
        free(s);

        if (asprintf(&s, "%s/%s_%08lx%s",
                     tempdir,
                     prefix,
                     random_bits & 0xffffffffUL,
                     suffix) == -1)
            return EXIT_OUT_OF_MEMORY;

        /* Note that if the name already exists as a symlink, this
         * open call will fail.
         *
         * The permissions are tight because nobody but this process
         * and our children should do anything with it. */
        fd = open(s, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd == -1) {
            /* try again */
            rs_trace("failed to create %s: %s", s, strerror(errno));
            random_bits += 7777; /* fairly prime */
            continue;
        }

        if (close(fd) == -1) {  /* huh? */
            rs_log_warning("failed to close %s: %s", s, strerror(errno));
            return EXIT_IO_ERROR;
        }

        break;
    } while (1);

    if ((ret = dcc_add_cleanup(s))) {
        /* bailing out */
        unlink(s);
        free(s);
        return ret;
    }

    *name_ret = s;
    return 0;
}
