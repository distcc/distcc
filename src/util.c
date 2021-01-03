/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003 by Martin Pool <mbp@samba.org>
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

#include <config.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <netinet/in.h>

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

#include <sys/param.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"
#include "snprintf.h"

            /* I will make a man more precious than fine
             * gold; even a man than the golden wedge of
             * Ophir.
             *        -- Isaiah 13:12 */


void dcc_exit(int exitcode)
{
    struct rusage self_ru, children_ru;

    if (getrusage(RUSAGE_SELF, &self_ru)) {
        rs_log_warning("getrusage(RUSAGE_SELF) failed: %s", strerror(errno));
        memset(&self_ru, 0, sizeof self_ru);
    }
    if (getrusage(RUSAGE_CHILDREN, &children_ru)) {
        rs_log_warning("getrusage(RUSAGE_CHILDREN) failed: %s", strerror(errno));
        memset(&children_ru, 0, sizeof children_ru);
    }

    /* NB fields must match up for microseconds */
    rs_log(RS_LOG_INFO,
           "exit: code %d; self: %d.%06d user %d.%06d sys; children: %d.%06d user %d.%06d sys",
           exitcode,
           (int) self_ru.ru_utime.tv_sec, (int) self_ru.ru_utime.tv_usec,
           (int) self_ru.ru_stime.tv_sec, (int) self_ru.ru_stime.tv_usec,
           (int) children_ru.ru_utime.tv_sec, (int) children_ru.ru_utime.tv_usec,
           (int) children_ru.ru_stime.tv_sec, (int)  children_ru.ru_stime.tv_usec);

    exit(exitcode);
}


int str_endswith(const char *tail, const char *tiger)
{
    size_t len_tail = strlen(tail);
    size_t len_tiger = strlen(tiger);

    if (len_tail > len_tiger)
        return 0;

    return !strcmp(tiger + len_tiger - len_tail, tail);
}


int str_startswith(const char *head, const char *worm)
{
    return !strncmp(head, worm, strlen(head));
}



/**
 * Skim through NULL-terminated @p argv, looking for @p s.
 **/
int argv_contains(char **argv, const char *s)
{
    while (*argv) {
        if (!strcmp(*argv, s))
            return 1;
        argv++;
    }
    return 0;
}


/**
 * Redirect a file descriptor into (or out of) a file.
 *
 * Used, for example, to catch compiler error messages into a
 * temporary file.
 **/
int dcc_redirect_fd(int fd, const char *fname, int mode)
{
    int newfd;

    /* ignore errors */
    close(fd);

    newfd = open(fname, mode, 0666);
    if (newfd == -1) {
        rs_log_crit("failed to reopen fd%d onto %s: %s",
                    fd, fname, strerror(errno));
        return EXIT_IO_ERROR;
    } else if (newfd != fd) {
        rs_log_crit("oops, reopened fd%d onto fd%d?", fd, newfd);
        return EXIT_IO_ERROR;
    }

    return 0;
}



char *dcc_gethostname(void)
{
    static char myname[100] = "\0";

    if (!myname[0]) {
        if (gethostname(myname, sizeof myname - 1) == -1)
            strcpy(myname, "UNKNOWN");
    }

    return myname;
}


/**
 * Look up a boolean environment option, which must be either "0" or
 * "1".  The default, if it's not set or is empty, is @p default.
 **/
int dcc_getenv_bool(const char *name, int default_value)
{
    const char *e;

    e = getenv(name);
    if (!e || !*e)
        return default_value;
    if (!strcmp(e, "1"))
        return 1;
    else if (!strcmp(e, "0"))
        return 0;
    else
        return default_value;
}


#define IS_LEGAL_DOMAIN_CHAR(c) (isalnum((uint8_t)c) || ((c) == '-') || ((c) == '.'))

/* Copy domain part of hostname to static buffer.
 * If hostname has no domain part, returns -1.
 * If domain lookup fails, returns -1.
 * Otherwise places pointer to domain in *domain_name and returns 0.
 *
 * This should yield the same result as the linux command
 * 'dnsdomainname' or 'hostname -d'.
 **/
int dcc_get_dns_domain(const char **domain_name)
{
#if 0 /* Too expensive */

    static char host_name[1024];
    struct hostent *h;
    int ret;

    ret = gethostname(host_name, sizeof(host_name));
    if (ret != 0)
        return -1;

    h = gethostbyname(host_name);
    if (h == NULL) {
        rs_log_error("failed to look up self \"%s\": %s", host_name,
                     hstrerror(h_errno));
        return -1;
    }

    strncpy(host_name, h->h_name, sizeof(host_name) - 1);
    host_name[sizeof(host_name) - 1] = '\0';
    *domain_name = strchr(h->h_name, '.');

#else  /* cheaper */
    const char *envh, *envh2;
    int i;
    const int MAXDOMAINLEN = 512;

    /* Kludge for speed: Try to retrieve FQDN from environment.
     * This can save many milliseconds on a network that's busy and lossy
     * (glibc retries DNS operations very slowly).
     */

    /* Solaris, BSD tend to put it in HOST.
     * (Some flavors of Linux put the non-qualified hostname in HOST,
     *  so ignore this if it doesn't have a dot in it.)
     */
    envh = getenv("HOST");
    if (envh && !strchr(envh, '.'))
        envh = NULL;

    /* Some flavors of Linux put the FQDN in HOSTNAME when
     * logged in interactively, but not when ssh'd in noninteractively.
     * Ubuntu's bash puts it in HOSTNAME but doesn't export it!
     */
    envh2 = getenv("HOSTNAME");
    if (envh2 && !strchr(envh2, '.'))
        envh2 = NULL;

    /* Pick the 'better' of the two.  Longer is usually better. */
    if (envh2 && (!envh || (strlen(envh) < strlen(envh2))))
        envh = envh2;

    /* If the above didn't work out, fall back to the real way. */
    if (!envh || !strchr(envh, '.')) {
        static char host_name[1024];
        struct hostent *h;
        int ret;

        ret = gethostname(host_name, sizeof(host_name));
        if (ret != 0)
            return -1;

        /* If hostname has a dot in it, assume it's the DNS address */
        if (!strchr(host_name, '.')) {
            /* Otherwise ask DNS what our full hostname is */
            h = gethostbyname(host_name);
            if (h == NULL) {
                rs_log_error("failed to look up self \"%s\": %s", host_name,
                             hstrerror(h_errno));
                return -1;
            }
            strncpy(host_name, h->h_name, sizeof(host_name) - 1);
            host_name[sizeof(host_name) - 1] = '\0';
        }
        envh = host_name;
    }

    /* validate to avoid possible errors from bad chars or huge value */
    for (i=0; envh[i] != '\0'; i++) {
        if (i > MAXDOMAINLEN || !IS_LEGAL_DOMAIN_CHAR(envh[i])) {
            rs_log_error("HOST/HOSTNAME present in environment but illegal: '%s'", envh);
            return -1;
        }
    }
    *domain_name = strchr(envh, '.');
#endif

    if (*domain_name == NULL)
        return -1;

    (*domain_name)++;
    /* Return 0 on success, or -1 if the domain name is illegal, e.g. empty */
    return ((*domain_name)[0] == '\0') ? -1 : 0;
}



/**
 * Set the `FD_CLOEXEC' flag of DESC if VALUE is nonzero,
 * or clear the flag if VALUE is 0.
 *
 * From the GNU C Library examples.
 *
 * @returns 0 on success, or -1 on error with `errno' set.
 **/
int set_cloexec_flag (int desc, int value)
{
    int oldflags = fcntl (desc, F_GETFD, 0);
    /* If reading the flags failed, return error indication now. */
    if (oldflags < 0)
        return oldflags;
    /* Set just the flag we want to set. */
    if (value != 0)
        oldflags |= FD_CLOEXEC;
    else
        oldflags &= ~FD_CLOEXEC;
    /* Store modified flag word in the descriptor. */
    return fcntl (desc, F_SETFD, oldflags);
}


/**
 * Ignore or unignore SIGPIPE.
 *
 * The server and child ignore it, because distcc code wants to see
 * EPIPE errors if something goes wrong.  However, for invoked
 * children it is set back to the default value, because they may not
 * handle the error properly.
 **/
int dcc_ignore_sigpipe(int val)
{
    if (signal(SIGPIPE, val ? SIG_IGN : SIG_DFL) == SIG_ERR) {
        rs_log_warning("signal(SIGPIPE, %s) failed: %s",
                       val ? "ignore" : "default",
                       strerror(errno));
        return EXIT_DISTCC_FAILED;
    }
    return 0;
}

/**
 * Search through the $PATH looking for a directory containing a file called
 * @p compiler_name, which is a symbolic link containing the string "distcc".
 *
 * Trim the path to just after the *last* such directory.
 *
 * If we find a distcc masquerade dir on the PATH, remove all the dirs up
 * to that point.
 **/
int dcc_trim_path(const char *compiler_name)
{
    const char *envpath, *newpath, *p, *n;
    char linkbuf[MAXPATHLEN], *buf;
    struct stat sb;
    size_t len;

    if (!(envpath = getenv("PATH"))) {
        rs_trace("PATH seems not to be defined");
        return 0;
    }

    rs_trace("original PATH %s", envpath);
    rs_trace("looking for \"%s\"", compiler_name);

    /* Allocate a buffer that will let us append "/cc" onto any PATH
     * element, even if there is only one item in the PATH. */
    if (!(buf = malloc(strlen(envpath)+1+strlen(compiler_name)+1))) {
        rs_log_error("failed to allocate buffer for PATH munging");
        return EXIT_OUT_OF_MEMORY;
    }

    for (n = p = envpath, newpath = NULL; *n; p = n) {
        n = strchr(p, ':');
        if (n)
            len = n++ - p;
        else {
            len = strlen(p);
            n = p + len;
        }
        strncpy(buf, p, len);

        sprintf(buf + len, "/%s", compiler_name);
        if (lstat(buf, &sb) == -1)
            continue;           /* ENOENT, EACCESS, etc */
        if (!S_ISLNK(sb.st_mode))
            break;
        if ((len = readlink(buf, linkbuf, sizeof linkbuf)) <= 0)
            continue;
        linkbuf[len] = '\0';
        if (strstr(linkbuf, "distcc")) {
            /* Set newpath to the part of the PATH past our match. */
            newpath = n;
        }
    }

    if (newpath) {
        int ret = dcc_set_path(newpath);
        if (ret)
            return ret;
    } else
        rs_trace("not modifying PATH");

    free(buf);
    return 0;
}

/* Set the PATH environment variable to the indicated value. */
int dcc_set_path(const char *newpath)
{
    char *buf;

    if (asprintf(&buf, "PATH=%s", newpath) <= 0 || !buf) {
        rs_log_error("failed to allocate buffer for new PATH");
        return EXIT_OUT_OF_MEMORY;
    }
    rs_trace("setting %s", buf);
    if (putenv(buf) < 0) {
        rs_log_error("putenv PATH failed");
        return EXIT_FAILURE;
    }
    /* We must leave "buf" allocated. */
    return 0;
}

/* Return the supplied path with the current-working directory prefixed (if
 * needed) and all "dir/.." references removed.  Supply path_len if you want
 * to use only a substring of the path string, otherwise make it 0. */
char *dcc_abspath(const char *path, int path_len)
{
    static char buf[MAXPATHLEN];
    unsigned len;
    char *p, *slash;

    if (*path == '/')
        len = 0;
    else {
        char *ret;
#ifdef HAVE_GETCWD
        ret = getcwd(buf, sizeof buf);
        if (ret == NULL) {
          rs_log_crit("getcwd failed: %s", strerror(errno));
        }
#else
        ret = getwd(buf);
        if (ret == NULL) {
          rs_log_crit("getwd failed: %s", strerror(errno));
        }
#endif
        len = strlen(buf);
        if (len >= sizeof buf) {
            rs_log_crit("getwd overflowed in dcc_abspath()");
        }
        buf[len++] = '/';
    }
    if (path_len <= 0)
        path_len = strlen(path);
    if (path_len >= 2 && *path == '.' && path[1] == '/') {
        path += 2;
        path_len -= 2;
    }
    if (len + (unsigned)path_len >= sizeof buf) {
        rs_log_error("path overflowed in dcc_abspath()");
        exit(EXIT_OUT_OF_MEMORY);
    }
    strncpy(buf + len, path, path_len);
    buf[len + path_len] = '\0';
    for (p = buf+len-(len > 0); (p = strstr(p, "/../")) != NULL; p = slash) {
        *p = '\0';
        if (!(slash = strrchr(buf, '/')))
            slash = p;
        strcpy(slash, p+3);
    }
    return buf;
}

/* Return -1 if a < b, 0 if a == b, and 1 if a > b */
int dcc_timecmp(struct timeval a, struct timeval b) {
    if (a.tv_sec < b.tv_sec) {
        return -1;
    } else if (a.tv_sec > b.tv_sec) {
        return 1;
    } else if (a.tv_usec < b.tv_usec) {
        /* at this point their tv_sec must be the same */
        return -1;
    } else if (a.tv_usec > b.tv_usec) {
        return 1;
    } else {
        /* they must be equal */
        return 0;
    }
}


/* Return the current number of running processes. */
int dcc_getcurrentload(void) {
#if defined(linux)
  double stats[3];
  int running;
  int total;
  int last_pid;
  int retval;

  FILE *f = fopen("/proc/loadavg", "r");
  if (NULL == f)
      return -1;

  retval = fscanf(f, "%lf %lf %lf %d/%d %d", &stats[0], &stats[1], &stats[2],
                  &running, &total, &last_pid);
  fclose(f);

  if (6 != retval)
      return -1;

  return running;
#else
  return -1;
#endif
}

/**
 *  Wrapper for getloadavg() that tries to return all 3 samples, and reports
 *  -1 for those samples that are not available.
 *
 *  Averages are over the last 1, 5, and 15 minutes, respectively.
 **/
void dcc_getloadavg(double loadavg[3]) {
  int num;
  int i;

#if defined(HAVE_GETLOADAVG)
  num = getloadavg(loadavg, 3);
#else
  num = 0;
#endif

  /* If getloadavg() didn't return 3 we want to fill
   * in the invalid elements with -1 */
  if (num < 0)
      num = 0;

  for (i=num; i < 3; ++i)
      loadavg[i] = -1;
}


/**
 * Duplicate the part of the string @p psrc up to a character in @p sep
 * (or end of string), storing the result in @p pdst.  @p psrc is updated to
 * point to the terminator.  (If the terminator is not found it will
 * therefore point to \0.
 *
 * If there is no more string, then @p pdst is instead set to NULL, no
 * memory is allocated, and @p psrc is not advanced.
 **/
int dcc_dup_part(const char **psrc, char **pdst, const char *sep)
{
    size_t len;

    len = strcspn(*psrc, sep);
    if (len == 0) {
        *pdst = NULL;
    } else {
        if (!(*pdst = malloc(len + 1))) {
            rs_log_error("failed to allocate string duplicate: %d", (int) len);
            return EXIT_OUT_OF_MEMORY;
        }
        strncpy(*pdst, *psrc, len);
        (*pdst)[len] = '\0';
        (*psrc) += len;
    }

    return 0;
}



int dcc_remove_if_exists(const char *fname)
{
    if (unlink(fname) && errno != ENOENT) {
        rs_log_warning("failed to unlink %s: %s", fname,
                       strerror(errno));
        return EXIT_IO_ERROR;
    }
    return 0;
}

int dcc_which(const char *command, char **out)
{
    char *loc = NULL, *_loc, *path, *t;
    int ret;

    path = getenv("PATH");
    if (!path)
        return -ENOENT;
    do {
        if (strstr(path, "distcc"))
            continue;
        /* emulate strchrnul() */
        t = strchr(path, ':');
        if (!t)
            t = path + strlen(path);
        _loc = realloc(loc, t - path + 1 + strlen(command) + 1);
        if (!_loc) {
            free(loc);
            return -ENOMEM;
        }
        loc = _loc;
        strncpy(loc, path, t - path);
        loc[t - path] = '\0';
        strcat(loc, "/");
        strcat(loc, command);
        ret = access(loc, X_OK);
        if (ret < 0)
            continue;
        *out = loc;
        return 0;
    } while ((path = strchr(path, ':') + 1));
    return -ENOENT;
}

/* Returns the number of processes in state D, the max non-cc/c++ RSS in kb and
 * the max RSS program's name */
void dcc_get_proc_stats(int *num_D, int *max_RSS, char **max_RSS_name) {
#if defined(linux)
    DIR *proc = opendir("/proc");
    struct dirent *procsubdir;
    static int pagesize = -1;
    static char RSS_name[1024];
    char statfile[1024];
    FILE *f;
    char name[1024];
    char state;
    int pid;
    int rss_size;
    int l;
    char *c;
    int isCC;

    /* If this doesn't cut it for you, see how CVS does it:
     * http://savannah.nongnu.org/cgi-bin/viewcvs/cvs/ccvs/lib/getpagesize.h */
    if (pagesize == -1) {
#if HAVE_GETPAGESIZE
        pagesize = getpagesize();
#else
        pagesize = 8192;
#endif
    }

    *num_D = 0;
    *max_RSS = 0;
    *max_RSS_name = RSS_name;
    RSS_name[0] = 0;

    while ((procsubdir = readdir(proc)) != NULL) {
        if (sscanf(procsubdir->d_name, "%d", &pid) != 1)
            continue;

        strcpy(statfile, "/proc/");
        strcat(statfile, procsubdir->d_name);
        strcat(statfile, "/stat");

        f = fopen(statfile, "r");
        if (f == NULL)
            continue;

        if (fscanf(f, "%*d %s %c %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %d",
                        name, &state, &rss_size) != 3) {
            fclose(f);
            continue;
        }

        rss_size = (rss_size * pagesize) / 1024; /* get rss_size in KB */

        if (state == 'D') {
            (*num_D)++;
        }

        l = strlen(RSS_name);
        c = RSS_name;

        /* check for .*{++,cc} */
        isCC = (l >= 2) && ((c[l-1] == 'c' && c[l-2] == 'c')
                                || (c[l-1] == '+' && c[l-2] == '+'));
        if ((rss_size > *max_RSS) && !isCC) {
            *max_RSS = rss_size;
            strncpy(RSS_name, name, 1024);
        }

        fclose(f);
    }

    closedir(proc);
#else
    static char RSS_name[] = "none";
    *num_D = -1;
    *max_RSS = -1;
    *max_RSS_name = RSS_name;
#endif
}


/* Returns the number of sector read/writes since boot */
void dcc_get_disk_io_stats(int *n_reads, int *n_writes) {
#if defined(linux)
    int retval;
    int kernel26 = 1;
    FILE *f;
    int reads, writes, minor;
    char dev[100];
    char tmp[1024];

    *n_reads = 0;
    *n_writes = 0;

    f = fopen("/proc/diskstats", "r");
    if (f == NULL) {
        if (errno != ENOENT)
            return;

        /* /proc/diskstats does not exist. probably a 2.4 kernel, so try reading
         * /proc/partitions */
        f = fopen("/proc/partitions", "r");
        if (f == NULL)
            return;
        kernel26 = 0;
    }

    if (!kernel26) /* blast away 2 header lines in /proc/partitions */ {
        retval = fscanf(f,
            "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s");
        if (retval == EOF) {
            fclose(f);
            return;
        }
    }

    while (1) {
        if (kernel26)
            retval = fscanf(f, " %*d %d %s", &minor, dev);
        else
            retval = fscanf(f, " %*d %d %*d %s", &minor, dev);

        if (retval == EOF || retval != 2)
            break;

        if (minor % 64 == 0
                && ((dev[0] == 'h' && dev[1] == 'd' && dev[2] == 'a')
                    || (dev[0] == 's' && dev[1] == 'd' && dev[2] == 'a'))) {
            /* disk stats */
            retval = fscanf(f, " %*d %*d %d %*d %*d %*d %d %*d %*d %*d %*d",
                            &reads, &writes);
            if (retval == EOF || retval != 2)
                break;

            /* only add stats for disks, so we don't double count */
            *n_reads += reads;
            *n_writes += writes;
        } else {
#if 0
            /* individual partition stats */
            retval = fscanf(f, " %*d %d %*d %d", &reads, &writes);
            if (retval == EOF || retval != 2)
                break;
#endif
            /* assume the lines aren't longer that 1024 characters */
            if (fgets(tmp, 1024, f) == NULL)
              break;
        }
    }

    fclose(f);
#else
    *n_reads = 0;
    *n_writes = 0;
#endif
}


#ifndef HAVE_STRLCPY
/* like strncpy but does not 0 fill the buffer and always null
   terminates. bufsize is the size of the destination buffer */
 size_t strlcpy(char *d, const char *s, size_t bufsize)
{
    size_t len = strlen(s);
    size_t ret = len;
    if (bufsize <= 0) return 0;
    if (len >= bufsize) len = bufsize-1;
    memcpy(d, s, len);
    d[len] = 0;
    return ret;
}
#endif

#ifndef HAVE_STRSEP
static char* strsep(char** str, const char* delims)
{
    char* token;

    if (*str == NULL) {
        return NULL;
    }

    token = *str;
    while (**str != '\0') {
        if (strchr(delims, **str) != NULL) {
            **str = '\0';
            (*str)++;
            return token;
        }
        (*str)++;
    }
    *str = NULL;
    return token;
}
#endif

/* Given a string @p input, this function fills a
   a newly-allocated array of strings with copies of
   the input's whitespace-separated parts.
   Returns 0 on success, 1 on error.
 */
int dcc_tokenize_string(const char *input, char ***argv_ptr)
{
    size_t n_spaces = 0;
    char *for_count;
    char **ap;
    char *input_copy;

    /* First of all, make a copy of the input string;
     * this way, we can destroy the copy.
     */
    input_copy = strdup(input);
    if (input_copy == NULL)
        return 1;

    /* Count the spaces in the string. */
    for (for_count = input_copy; *for_count; for_count++)
        if (isspace((uint8_t)*for_count))
            n_spaces++;

    /* The maximum number of space-delimited strings we
     * can have is n_spaces + 1, and we need to add another 1 for
     * the null-termination.
     */
    *argv_ptr = malloc(sizeof(char*) * (n_spaces + 1 + 1));
    if (*argv_ptr == NULL) {
        free(input_copy);
        return 1;
    }

    ap = *argv_ptr;
    while((*ap = strsep(&input_copy, " \t\n")) != NULL) {

        /* If the field is empty, do nothing */
      if (**ap == '\0')
          continue;

      *ap = strdup(*ap);
      if (*ap == NULL) {
          char **p;
          for (p = *argv_ptr; *p; p++) {
            free(*p);
          }
          free(*argv_ptr);
          free(input_copy);
          return 1;
      }

      ap++;
    }
    free(input_copy);
    return 0;
}

#ifndef HAVE_GETLINE
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    static const int buffer_size_increment = 100;
    char *buffer;
    size_t size;
    size_t bytes_read;
    int c;
    char *new_buffer;

    if (lineptr == NULL || stream == NULL || n == NULL ||
            (*lineptr == NULL && *n != 0)) {
        /* Invalid parameters. */
        return -1;
    }

    buffer = *lineptr;
    size = *n;

    bytes_read = 0;
    do {
        /* Ensure that we have space for next character or '\0'. */
        if (bytes_read + 1 > size) {
            size += buffer_size_increment;
            new_buffer = realloc(buffer, size);
            if (new_buffer == NULL) {
                /* Out of memory. */
                *lineptr = buffer;
                *n = size - buffer_size_increment;
                return -1;
            }
            buffer = new_buffer;
        }
        if ((c = fgetc(stream)) == EOF)
            break;
        buffer[bytes_read++] = c;
    } while (c != '\n');
    buffer[bytes_read] = '\0';

    *lineptr = buffer;
    *n = size;

    /* We return -1 on EOF for compatibility with GNU getline(). */
    return bytes_read == 0 ? -1 : (ssize_t) bytes_read;
}
#endif

/* from old systemd

   Copyright 2010 Lennart Poettering

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation files
   (the "Software"), to deal in the Software without restriction,
   including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software,
   and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
 */
static int sd_is_socket_internal(int fd, int type, int listening) {
        struct stat st_fd;

        if (fd < 0 || type < 0)
                return -EINVAL;

        if (fstat(fd, &st_fd) < 0)
                return -errno;

        if (!S_ISSOCK(st_fd.st_mode))
                return 0;

        if (type != 0) {
                int other_type = 0;
                socklen_t l = sizeof(other_type);

                if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &other_type, &l) < 0)
                        return -errno;

                if (l != sizeof(other_type))
                        return -EINVAL;

                if (other_type != type)
                        return 0;
        }

        if (listening >= 0) {
                int accepting = 0;
                socklen_t l = sizeof(accepting);

                if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &l) < 0)
                        return -errno;

                if (l != sizeof(accepting))
                        return -EINVAL;

                if (!accepting != !listening)
                        return 0;
        }

        return 1;
}

union sockaddr_union {
        struct sockaddr sa;
        struct sockaddr_in in4;
        struct sockaddr_in6 in6;
        struct sockaddr_un un;
        struct sockaddr_storage storage;
};

int not_sd_is_socket(int fd, int family, int type, int listening) {
        int r;

        if (family < 0)
                return -EINVAL;

        r = sd_is_socket_internal(fd, type, listening);
        if (r <= 0)
                return r;

        if (family > 0) {
                union sockaddr_union sockaddr = {};
                socklen_t l = sizeof(sockaddr);

                if (getsockname(fd, &sockaddr.sa, &l) < 0)
                        return -errno;

                if ((size_t)l < sizeof(sa_family_t))
                        return -EINVAL;

                return sockaddr.sa.sa_family == family;
        }

        return 1;
}
