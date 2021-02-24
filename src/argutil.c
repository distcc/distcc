/* -*- c-file-style: "java"; indent-tabs-mode: nil; fill-column: 78 -*-
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



/*
 * Utilities for dealing with argv[]-style strings.
 *
 * These rules might not yet be consistently applied in distcc, but they
 * should be in the future:
 *
 * For simplicity in managing memory we try to keep all argv structures
 * malloc'd, without any shared structure.  It is then possible to just free
 * the whole thing whenever we're finished with it.
 *
 * One exception is of course the argv used to invoke the program, which is
 * treated as read-only.
 */


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"


/**
 * Return true if argv contains needle as an argument.
 **/
int dcc_argv_search(char **a,
                    const char *needle)
{
    for (; *a; a++)
        if (!strcmp(*a, needle))
            return 1;
    return 0;
}


/**
 * Return true if argv contains argument starting with needle.
 */
int dcc_argv_startswith(char **a,
                        const char *needle)
{
    size_t needle_len = strlen(needle);
    for (; *a; a++)
        if (!strncmp(*a, needle, needle_len))
            return 1;
    return 0;
}

unsigned int dcc_argv_len(char **a)
{
    unsigned int i;

    for (i = 0; a[i]; i++)
        ;
    return i;
}


/* Free a malloc'd argv structure.  Only safe when the array and all its
 * components were malloc'd. */
void dcc_free_argv(char **argv)
{
    char **a;

    for (a = argv; *a != NULL; a++)
        free(*a);
    free(argv);
}


/* Copy an argv array, adding extra NULL elements to the end to allow for
 * adding more arguments later.
 */
int dcc_copy_argv(char **from, char ***out, int delta)
{
    char **b;
    int l, i, k;

    l = dcc_argv_len(from);
    b = malloc((l+1+delta) * (sizeof from[0]));
    if (b == NULL) {
        rs_log_error("failed to allocate copy of argv");
        return EXIT_OUT_OF_MEMORY;
    }
    for (i = 0; i < l; i++) {
        if ((b[i] = strdup(from[i])) == NULL) {
            rs_log_error("failed to duplicate element %d", i);
            for(k = 0; k < i; k++)
                free(b[k]);
            free(b);
            return EXIT_OUT_OF_MEMORY;
        }
    }
    b[l] = NULL;

    *out = b;

    return 0;
}



/**
 * Convert an argv array to printable form for debugging output.
 *
 * @note The result is not necessarily properly quoted for passing to
 * shells.
 *
 * @return newly-allocated string containing representation of
 * arguments.
 **/
char *dcc_argv_tostr(char **a)
{
    int l, i;
    char *s, *ss;

    /* calculate total length */
    for (l = 0, i = 0; a[i]; i++) {
        l += strlen(a[i]) + 3;  /* two quotes and space */
    }

    ss = s = malloc((size_t) l + 1);
    if (!s) {
        rs_log_crit("failed to allocate %d bytes", l+1);
        exit(EXIT_OUT_OF_MEMORY);
    }

    for (i = 0; a[i]; i++) {
        /* kind of half-assed quoting; won't handle strings containing
         * quotes properly, but good enough for debug messages for the
         * moment. */
        int needs_quotes = !*a[i] || (strpbrk(a[i], " \t\n\"\';") != NULL);
        if (i)
            *ss++ = ' ';
        if (needs_quotes)
            *ss++ = '"';
        strcpy(ss, a[i]);
        ss += strlen(a[i]);
        if (needs_quotes)
            *ss++ = '"';
    }
    *ss = '\0';

    return s;
}
