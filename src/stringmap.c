/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * Copyright 2005 Google Inc.
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
#include <string.h>
#include <limits.h>
#include <assert.h>

#include "stringmap.h"

#ifndef NULL
#define NULL 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Load the given list of strings into the key/value map.
 * The key for each string is the numFinalWordsToMatch of the string;
 * the value for each string is the entire string.
 * FIXME: doesn't work for utf-8 strings, since it scans raw chars for /
 */
stringmap_t *stringmap_load(const char *filename, int numFinalWordsToMatch)
{
    stringmap_t *result;
    FILE *fp;
    char buf[2*PATH_MAX];
    int n;

    result = calloc(1, sizeof(*result));
    if (!result)
        return NULL;
    result->numFinalWordsToMatch = numFinalWordsToMatch;
    fp = fopen(filename, "r");
    if (!fp) {
        free(result);
        return NULL;
    }
    n=0;
    while (fgets(buf, sizeof(buf), fp))
        n++;
    result->n = n;
    result->map = malloc(n * sizeof(result->map[0]));

    rewind(fp);
    n=0;
    while (fgets(buf, sizeof(buf), fp)) {
        int pos, w;

        int len = strlen(buf);
        /* strip trailing \n */
        if (len > 0 && buf[len-1] == '\n') {
            buf[len-1] = 0;
            len--;
        }
        /* set pos to the start of the significant part of the string */
        for (pos=len-1, w=0; pos>0; pos--) {
            if (buf[pos] == '/') {
                w++;
                if (w >= numFinalWordsToMatch) {
                    pos++;
                    break;
                }
            }
        }

        result->map[n].value = strdup(buf);
        result->map[n].key = strdup(buf+pos);
        n++;
    } fclose(fp);
    return result;
}

const char *stringmap_lookup(const stringmap_t *map, const char *string)
{
    int i, w;
    int len = strlen(string);
    int pos;
    for (pos=len-1, w=0; pos>0; pos--) {
        if (string[pos] == '/') {
            w++;
            if (w >= map->numFinalWordsToMatch) {
                pos++;
                break;
            }
        }
    }
    for (i=0; i<map->n; i++) {
        /*printf("Comparing %s and %s\n", map->map[i].key, string+pos);*/
        if (!strcmp(map->map[i].key, string+pos))
            return map->map[i].value;
    }
    return NULL;
}

#if 0

void dumpMap(stringmap_t *sm)
{
    int i;
    printf("map has %d elements, and numFinalWordsToMatch is %d\n", sm->n, sm->numFinalWordsToMatch);
    for (i=0; i < sm->n; i++) {
        printf("row %d: key %s, value %s\n", i, sm->map[i].key, sm->map[i].value);
    }
}

#define verifyMap(sm, a, b) { \
    const char *c = stringmap_lookup(sm, a); \
    if (!b) \
        assert(!c); \
    else { \
        assert(c); \
        assert(!strcmp(b, c)); } }

int main(int argc, char **argv)
{
    FILE *fp;
    stringmap_t *sm;

    fp = fopen("stringmap_test.dat", "w");
    fprintf(fp, "/foo/bar/bletch\n");
    fclose(fp);
    sm = stringmap_load("stringmap_test.dat", 1);
    dumpMap(sm);
    verifyMap(sm, "/bar/bletch", "/foo/bar/bletch");
    verifyMap(sm, "bletch", "/foo/bar/bletch");
    verifyMap(sm, "/whatever/bletch", "/foo/bar/bletch");
    verifyMap(sm, "baz", NULL);
    verifyMap(sm, "/foo/bar/bletch", "/foo/bar/bletch");

    fp = fopen("stringmap_test.dat", "w");
    fprintf(fp, "/usr/bin/gcc\n");
    fprintf(fp, "/usr/bin/cc\n");
    fclose(fp);
    sm = stringmap_load("stringmap_test.dat", 1);
    dumpMap(sm);
    verifyMap(sm, "/usr/bin/gcc", "/usr/bin/gcc");
    verifyMap(sm, "/usr/bin/cc", "/usr/bin/cc");
    verifyMap(sm, "gcc", "/usr/bin/gcc");
    verifyMap(sm, "cc", "/usr/bin/cc");
    verifyMap(sm, "g77", NULL);

    fp = fopen("stringmap_test.dat", "w");
    fprintf(fp, "/usr/bin/i686-blah-blah/gcc\n");
    fprintf(fp, "/usr/bin/i386-blah-blah/gcc\n");
    fclose(fp);
    sm = stringmap_load("stringmap_test.dat", 2);
    dumpMap(sm);
    verifyMap(sm, "/usr/bin/i686-blah-blah/gcc",
                      "/usr/bin/i686-blah-blah/gcc");
    verifyMap(sm, "/usr/bin/i386-blah-blah/gcc",
                      "/usr/bin/i386-blah-blah/gcc");
    verifyMap(sm, "i686-blah-blah/gcc", "/usr/bin/i686-blah-blah/gcc");
    verifyMap(sm, "i386-blah-blah/gcc", "/usr/bin/i386-blah-blah/gcc");
    verifyMap(sm, "gcc", NULL);
    verifyMap(sm, "g77", NULL);

        return 0;
}

#endif
