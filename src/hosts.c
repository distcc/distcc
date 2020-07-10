/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004, 2009 by Martin Pool <mbp@samba.org>
 * Copyright 2004 Google Inc.
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

/*  dcc_randomize_host_list() and friends:
 *   Author: Josh Hyman <joshh@google.com>
 */


                /* The lyf so short, the craft so long to lerne.
                 * -- Chaucer */



/**
 * @file
 *
 * Routines to parse <tt>$DISTCC_HOSTS</tt>.  Actual decisions about
 * where to run a job are in where.c.
 *
 * The grammar of this variable is, informally:
 *
  DISTCC_HOSTS = HOSTSPEC ...
  HOSTSPEC = LOCAL_HOST | SSH_HOST | TCP_HOST | OLDSTYLE_TCP_HOST
                        | GLOBAL_OPTION
  LOCAL_HOST = localhost[/LIMIT]
  SSH_HOST = [USER]@HOSTID[/LIMIT][:COMMAND][OPTIONS]
  TCP_HOST = HOSTID[:PORT][/LIMIT][OPTIONS]
  OLDSTYLE_TCP_HOST = HOSTID[/LIMIT][:PORT][OPTIONS]
  HOSTID = HOSTNAME | IPV4
  OPTIONS = ,OPTION[OPTIONS]
  OPTION = lzo | cpp
  GLOBAL_OPTION = --randomize
 *
 * Any amount of whitespace may be present between hosts.
 *
 * The command specified for SSH defines the location of the remote
 * server, e.g. "/usr/local/bin/distccd".  This is provided as a
 * convenience who have trouble getting their PATH set correctly for
 * sshd to find distccd, and should not normally be needed.
 *
 * If you need to specify special options for ssh, they should be put
 * in ~/.ssh/config and referenced by the hostname.
 *
 * The TCP port defaults to 3632 and should not normally need to be
 * overridden.
 *
 * IPv6 literals are not supported yet.  They will need to be
 * surrounded by square brackets because they may contain a colon,
 * which would otherwise be ambiguous.  This is consistent with other
 * URL-like schemes.
 */


/*
       Alexandre Oliva writes

        I take this opportunity to plead people to consider such issues when
        proposing additional syntax for DISTCC_HOSTS: if it was possible to
        handle DISTCC_HOSTS as a single shell word (perhaps after turning
        blanks into say commas), without the risk of any shell active
        characters such as {, }, ~, $, quotes getting in the way, outputting
        distcc commands that override DISTCC_HOSTS would be far
        simpler.

  TODO: Perhaps entries in the host list that "look like files" (start
    with '/' or '~') should be read in as files?  This could even be
    recursive.
*/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "hosts.h"
#include "exitcode.h"
#include "snprintf.h"
#include "config.h"
#ifdef HAVE_AVAHI
#include "zeroconf.h"
#define ZEROCONF_MAGIC "+zeroconf"
#endif

const int dcc_default_port = DISTCC_DEFAULT_PORT;

/***
 * A simple container which would hold a host -> rand int pair
 ***/
struct rand_container {
    struct dcc_hostdef *host;
    int rand;
};

int dcc_randomize_host_list(struct dcc_hostdef **host_list, int length);

int dcc_compare_container(const void *a, const void *b);


#ifndef HAVE_STRNDUP
/**
 * Copy at most @p size characters from @p src, plus a terminating nul.
 *
 * Really this needs to be in util.c, but it's only used here.
 **/
char *strndup(const char *src, size_t size);
char *strndup(const char *src, size_t size)
{
    char *dst;

    dst = malloc(size + 1);
    if (dst == NULL)
        return NULL;
    strncpy(dst, src, size);
    dst[size] = '\0';

    return dst;
}
#endif

/**
 * Get a list of hosts to use.
 *
 * Hosts are taken from DISTCC_HOSTS, if that exists.  Otherwise, they are
 * taken from $DISTCC_DIR/hosts, if that exists.  Otherwise, they are taken
 * from ${sysconfdir}/distcc/hosts, if that exists.  Otherwise, we fail.
 **/
int dcc_get_hostlist(struct dcc_hostdef **ret_list,
                     int *ret_nhosts)
{
    char *env;
    char *path, *top;
    int ret;

    *ret_list = NULL;
    *ret_nhosts = 0;

    if ((env = getenv("DISTCC_HOSTS")) != NULL) {
        rs_trace("read hosts from environment");
        return dcc_parse_hosts(env, "$DISTCC_HOSTS", ret_list, ret_nhosts, NULL);
    }

    /* $DISTCC_DIR or ~/.distcc */
    if ((ret = dcc_get_top_dir(&top)) == 0) {
        /* if we failed to get it, just warn */

        checked_asprintf(&path, "%s/hosts", top);
        if (path != NULL && access(path, R_OK) == 0) {
            ret = dcc_parse_hosts_file(path, ret_list, ret_nhosts);
            free(path);
            return ret;
        } else {
            rs_trace("not reading %s: %s", path, strerror(errno));
            free(path);
        }
    }

    checked_asprintf(&path, "%s/distcc/hosts", SYSCONFDIR);
    if (path != NULL && access(path, R_OK) == 0) {
        ret = dcc_parse_hosts_file(path, ret_list, ret_nhosts);
        free(path);
        return ret;
    } else {
        rs_trace("not reading %s: %s", path, strerror(errno));
        free(path);
    }

    /* FIXME: Clearer message? */
    rs_log_warning("no hostlist is set; can't distribute work");

    return EXIT_BAD_HOSTSPEC;
}


/**
 * Parse an optionally present multiplier.
 *
 * *psrc is the current parse cursor; it is advanced over what is read.
 *
 * If a multiplier is present, *psrc points to a substring starting with '/'.
 * The host definition is updated to the numeric value following.  Otherwise
 * the hostdef is unchanged.
 **/
static int dcc_parse_multiplier(const char **psrc, struct dcc_hostdef *hostdef)
{
    const char *token = *psrc;

    if ((*psrc)[0] == '/' || (*psrc)[0] == '=') {
        int val;
        (*psrc)++;
        val = atoi(*psrc);
        if (val == 0) {
            rs_log_error("bad multiplier \"%s\" in host specification", token);
            return EXIT_BAD_HOSTSPEC;
        }
        while (isdigit((uint8_t)**psrc))
            (*psrc)++;
        hostdef->n_slots = val;
    }
    return 0;
}


/**
 * Parse an optionally present option string.
 *
 * At the moment the only two options we have is "lzo" for compression,
 * and "cpp" if the server supports doing the preprocessing there, also.
 **/
static int dcc_parse_options(const char **psrc,
                             struct dcc_hostdef *host)
{
    const char *started = *psrc, *p = *psrc;

    host->compr = DCC_COMPRESS_NONE;
    host->cpp_where = DCC_CPP_ON_CLIENT;
#ifdef HAVE_GSSAPI
    host->authenticate = 0;
    host->auth_name = NULL;
#endif

    while (p[0] == ',') {
        p++;
        if (str_startswith("lzo", p)) {
            rs_trace("got LZO option");
            host->compr = DCC_COMPRESS_LZO1X;
            p += 3;
        } else if (str_startswith("down", p)) {
            /* if "hostid,down", mark it down, and strip down from hostname */
            host->is_up = 0;
            p += 4;
        } else if (str_startswith("cpp", p)) {
            rs_trace("got CPP option");
            host->cpp_where = DCC_CPP_ON_SERVER;
            p += 3;
#ifdef HAVE_GSSAPI
        } else if (str_startswith("auth", p)) {
            rs_trace("got GSSAPI option");
            host->authenticate = 1;
            p += 4;
            if (p[0] == '=') {
                p++;
                int ret;
                if ((ret = dcc_dup_part(&p, &host->auth_name, "/: \t\n\r\f,")))
	                return ret;

                if (host->auth_name) {
                    rs_trace("using \"%s\" server name instead of fqdn "
                             "lookup for GSS-API auth", host->auth_name);
                }
            }
#endif
        } else {
            rs_log_error("unrecognized option in host specification: %s",
                         started);
            return EXIT_BAD_HOSTSPEC;
        }
    }
    if (dcc_get_protover_from_features(host->compr, host->cpp_where,
                                       &host->protover) == -1) {
        rs_log_error("invalid host options: %s", started);
        return EXIT_BAD_HOSTSPEC;
    }

    *psrc = p;

    return 0;
}

static int dcc_parse_ssh_host(struct dcc_hostdef *hostdef,
                              const char *token_start)
{
    int ret;
    const char *token = token_start;

    /* Everything up to '@' is the username */
    if ((ret = dcc_dup_part(&token, &hostdef->user, "@")) != 0)
        return ret;

    if (token[0] != '@') {
        rs_log_error("expected '@' to start ssh token");
        return EXIT_BAD_HOSTSPEC;
    }

    token++;

    if ((ret = dcc_dup_part(&token, &hostdef->hostname, "/: \t\n\r\f,")) != 0)
        return ret;

    if (!hostdef->hostname) {
        rs_log_error("hostname is required in SSH host specification \"%s\"",
                     token_start);
        return EXIT_BAD_HOSTSPEC;
    }

    if ((ret = dcc_parse_multiplier(&token, hostdef)) != 0)
        return ret;

    if (token[0] == ':') {
        token++;
        if ((ret = dcc_dup_part(&token, &hostdef->ssh_command, " \t\n\r\f,")))
            return ret;
    }

    if ((ret = dcc_parse_options(&token, hostdef)))
        return ret;

    hostdef->mode = DCC_MODE_SSH;
    return 0;
}


static int dcc_parse_tcp_host(struct dcc_hostdef *hostdef,
                              const char * const token_start)
{
    int ret;
    const char *token = token_start;

    if (token[0] == '[') {
	/* Skip the leading bracket, which is not part of the address */
	token++;

	/* We have an IPv6 Address */
	if ((ret = dcc_dup_part(&token, &hostdef->hostname, "/] \t\n\r\f,")))
	    return ret;
	if(token[0] != ']') {
	    rs_log_error("IPv6 Hostname requires closing ']'");
	    return EXIT_BAD_HOSTSPEC;
	}
	token++;

    } else {
	/* Parse IPv4 address */
	if ((ret = dcc_dup_part(&token, &hostdef->hostname, "/: \t\n\r\f,")))
	    return ret;
    }

    if (!hostdef->hostname) {
        rs_log_error("hostname is required in tcp host specification \"%s\"",
                     token_start);
        return EXIT_BAD_HOSTSPEC;
    }

    if ((ret = dcc_parse_multiplier(&token, hostdef)) != 0)
        return ret;

    hostdef->port = dcc_default_port;
    if (token[0] == ':') {
        char *tail;

        token++;

        hostdef->port = strtol(token, &tail, 10);
        if (*tail != '\0' && !isspace((uint8_t)*tail) && *tail != '/' && *tail != ',') {
            rs_log_error("invalid tcp port specification in \"%s\"", token);
            return EXIT_BAD_HOSTSPEC;
        } else {
            token = tail;
        }
    }

    if ((ret = dcc_parse_multiplier(&token, hostdef)) != 0)
        return ret;

    if ((ret = dcc_parse_options(&token, hostdef)))
        return ret;

    hostdef->mode = DCC_MODE_TCP;
    return 0;
}


static int dcc_parse_localhost(struct dcc_hostdef *hostdef,
                               const char * token_start)
{
    const char *token = token_start + strlen("localhost");

    hostdef->mode = DCC_MODE_LOCAL;
    hostdef->hostname = strdup("localhost");

    /* Run only two tasks on localhost by default.
     *
     * It might be nice to run more if there are more CPUs, but determining
     * the number of CPUs on Linux is a bit expensive since it requires
     * examining mtab and /proc/stat.  Anyone lucky enough to have a >2 CPU
     * machine can specify a number in the host list.
     */
    hostdef->n_slots = 2;

    return dcc_parse_multiplier(&token, hostdef);
}

/** Given a host with its protover fields set, set
 *  its feature fields appropriately. Returns 0 if the protocol
 *  is known, non-zero otherwise.
 */
int dcc_get_features_from_protover(enum dcc_protover protover,
                                   enum dcc_compress *compr,
                                   enum dcc_cpp_where *cpp_where)
{
    if (protover > 1) {
        *compr = DCC_COMPRESS_LZO1X;
    } else {
        *compr = DCC_COMPRESS_NONE;
    }
    if (protover > 2) {
        *cpp_where = DCC_CPP_ON_SERVER;
    } else {
        *cpp_where = DCC_CPP_ON_CLIENT;
    }

    if (protover == 0 || protover > 3) {
        return 1;
    } else {
        return 0;
    }
}

/** Given a host with its feature fields set, set
 *  its protover appropriately. Return the protover,
 *  or -1 on error.
 */
int dcc_get_protover_from_features(enum dcc_compress compr,
                                   enum dcc_cpp_where cpp_where,
                                   enum dcc_protover *protover)
{
    *protover = -1;

    if (compr == DCC_COMPRESS_NONE && cpp_where == DCC_CPP_ON_CLIENT) {
        *protover = DCC_VER_1;
    }

    if (compr == DCC_COMPRESS_LZO1X && cpp_where == DCC_CPP_ON_SERVER) {
        *protover = DCC_VER_3;
    }

    if (compr == DCC_COMPRESS_LZO1X && cpp_where == DCC_CPP_ON_CLIENT) {
        *protover = DCC_VER_2;
    }

    if (compr == DCC_COMPRESS_NONE && cpp_where == DCC_CPP_ON_SERVER) {
        rs_log_error("pump mode (',cpp') requires compression (',lzo')");
    }

    return *protover;
}


/**
 * @p where is the host list, taken either from the environment or file.
 *
 * @return 0 if parsed successfully; nonzero if there were any errors,
 * or if no hosts were defined.
 **/
int dcc_parse_hosts(const char *where, const char *source_name,
                    struct dcc_hostdef **ret_list,
                    int *ret_nhosts, struct dcc_hostdef **ret_prev)
{
    int ret, flag_randomize = 0;
    struct dcc_hostdef *curr, *_prev;

    if (!ret_prev) {
        ret_prev = &_prev;
        _prev = NULL;
    }

    /* TODO: Check for '/' in places where it might cause trouble with
     * a lock file name. */

    /* A simple, hardcoded scanner.  Some of the GNU routines might be
     * useful here, but they won't work on less capable systems.
     *
     * We repeatedly attempt to extract a whitespace-delimited host
     * definition from the string until none remain.  Allocate an
     * entry; hook to previous entry.  We then determine if there is a
     * '@' in it, which tells us whether it is an SSH or TCP
     * definition.  We then duplicate the relevant subcomponents into
     * the relevant fields. */
    while (1) {
        int token_len;
        const char *token_start;
        int has_at;

        if (where[0] == '\0')
            break;              /* end of string */

        /* skip over comments */
        if (where[0] == '#') {
            do
                where++;
            while (where[0] != '\n' && where[0] != '\r' && where[0] != '\0');
            continue;
        }

        if (isspace((uint8_t)where[0])) {
            where++;            /* skip space */
            continue;
        }

        token_start = where;
        token_len = strcspn(where, " #\t\n\f\r");

        /* intercept keywords which are not actually hosts */
        if (!strncmp(token_start, "--randomize", 11)) {
            flag_randomize = 1;
            where = token_start + token_len;
            continue;
        }

        if(!strncmp(token_start, "--localslots_cpp", 16)) {
            const char *ptr;
            ptr = token_start + 16;
            if(dcc_parse_multiplier(&ptr, dcc_hostdef_local_cpp) == 0) {
                where = token_start + token_len;
                continue;
            }
        }

        if(!strncmp(token_start, "--localslots", 12)) {
            const char *ptr;
            ptr = token_start + 12;
            if(dcc_parse_multiplier(&ptr, dcc_hostdef_local) == 0) {
                where = token_start + token_len;
                continue;
            }
        }

#ifdef HAVE_AVAHI
        if (token_len == sizeof(ZEROCONF_MAGIC)-1 &&
            !strncmp(token_start, ZEROCONF_MAGIC, (unsigned) token_len)) {
            if ((ret = dcc_zeroconf_add_hosts(ret_list, ret_nhosts, 4, ret_prev) != 0))
                return ret;
            goto skip;
        }
#endif

        /* Allocate new list item */
        curr = calloc(1, sizeof(struct dcc_hostdef));
        if (!curr) {
            rs_log_crit("failed to allocate host definition");
            return EXIT_OUT_OF_MEMORY;
        }

        /* by default, mark the host up */
        curr->is_up = 1;

        /* Store verbatim hostname */
        if (!(curr->hostdef_string = strndup(token_start, (size_t) token_len))) {
            rs_log_crit("failed to allocate hostdef_string");
            return EXIT_OUT_OF_MEMORY;
        }

        /* Link into list */
        if (*ret_prev) {
            (*ret_prev)->next = curr;
        } else {
            *ret_list = curr;   /* first */
        }

        /* Default task limit.  A bit higher than the local limit to allow for
         * some files in transit. */
        curr->n_slots = 4;

        curr->protover = DCC_VER_1; /* default */
        curr->compr = DCC_COMPRESS_NONE;

        has_at = (memchr(token_start, '@', (size_t) token_len) != NULL);

        if (!strncmp(token_start, "localhost", 9)
            && (token_len == 9 || token_start[9] == '/')) {
            rs_trace("found localhost token \"%.*s\"", token_len, token_start);
            if ((ret = dcc_parse_localhost(curr, token_start)) != 0)
                return ret;
        } else if (has_at) {
            rs_trace("found ssh token \"%.*s\"", token_len, token_start);
            if ((ret = dcc_parse_ssh_host(curr, token_start)) != 0)
                return ret;
        } else {
            rs_trace("found tcp token \"%.*s\"", token_len, token_start);
            if ((ret = dcc_parse_tcp_host(curr, token_start)) != 0)
                return ret;
        }

        if (!curr->is_up) {
            rs_trace("host %s is down", curr->hostdef_string);
        }

        (*ret_nhosts)++;
        *ret_prev = curr;

#ifdef HAVE_AVAHI
        skip:
#endif

        /* continue to next token if any */
        where = token_start + token_len;
    }

    if (*ret_nhosts) {
        if (flag_randomize)
            if ((ret = dcc_randomize_host_list(ret_list, *ret_nhosts)) != 0)
                return ret;
        return 0;
    } else {
        rs_log_warning("%s contained no hosts; can't distribute work", source_name);
        return EXIT_BAD_HOSTSPEC;
    }
}


int dcc_compare_container(const void *a, const void *b)
{
    struct rand_container *i, *j;
    i = (struct rand_container *) a;
    j = (struct rand_container *) b;

    if (i->rand == j->rand)
        return 0;
    else if (i->rand > j->rand)
        return 1;
    else
        return -1;
}

int dcc_randomize_host_list(struct dcc_hostdef **host_list, int length)
{
    int i;
    unsigned int rand_val;
    struct dcc_hostdef *curr;
    struct rand_container *c;

    c = malloc(length * sizeof(struct rand_container));
    if (!c) {
        rs_log_crit("failed to allocate host definition");
        return EXIT_OUT_OF_MEMORY;
    }
/*
{
#ifdef HAVE_GETTIMEOFDAY
    int ret;
    struct timeval tv;
    if ((ret = gettimeofday(&tv, NULL)) == 0)
        rand_val = (unsigned int) tv.tv_usec;
    else
#else
        rand_val = (unsigned int) time(NULL) ^ (unsigned int) getpid();
#endif
}
*/
    rand_val = (unsigned int) getpid();

    /* create pairs of hosts -> random numbers */
    srand(rand_val);
    curr = *host_list;
    for (i = 0; i < length; i++) {
        c[i].host = curr;
        c[i].rand = rand();
        curr = curr->next;
    }

    /* sort */
    qsort(c, length, sizeof(struct rand_container), &dcc_compare_container);

    /* reorder the list */
    for (i = 0; i < length; i++) {
        if (i != length - 1)
            c[i].host->next = c[i+1].host;
        else
            c[i].host->next = NULL;
    }

    /* move the start of the list */
    *host_list = c[0].host;

    free(c);
    return 0;
}

int dcc_free_hostdef(struct dcc_hostdef *host)
{
    /* ANSI C requires free() to accept NULL */

    free(host->user);
    free(host->hostname);
    free(host->ssh_command);
    free(host->hostdef_string);
    memset(host, 0xf1, sizeof *host);
    free(host);

    return 0;
}
