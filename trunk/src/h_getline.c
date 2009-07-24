/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 * Copyright 2009 Google Inc.
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

/* Author: Fergus Henderson */

/*
 * h_getline.cc:
 * Helper for tests of getline().
 */

#include <config.h>
#include "distcc.h"
#include "util.h"
#include <stdlib.h>

const char *rs_program_name = "h_getline";

int main(int argc, char** argv) {
    char *line;
    size_t n;
    int c, i;

    if (argc > 1) {
        n = atoi(argv[1]);
        line = malloc(n);
    } else {
        n = 0;
        line = NULL;
    }

    printf("original n = %lu, ", (unsigned long) n);
    long result = getline(&line, &n, stdin);
    if (result > (long) n) {
        fprintf(stderr, "ERROR: return value > buffer size\n");
        exit(1);
    }
    printf("returned %ld, ", (long) result);
    printf("n = %lu, ", (unsigned long) n);
    printf("line = '");
    for (i = 0; i < result; i++) {
        putchar(line[i]);
    }
    printf("', rest = '");
    while ((c = getchar()) != EOF)
        putchar(c);
    printf("'\n");
    return 0;
}
