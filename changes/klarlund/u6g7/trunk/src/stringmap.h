/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4 fill-column: 78 -*-
 *
 * Copyright (C) 2005, 2006, 2007 by Google
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef STRINGMAP_H
#define STRINGMAP_H

typedef struct {
    /* the strings, and what they map to */
    struct {
        char *key;
        char *value;
    } *map;

    /* number of elements in map */
    int n;

    /* if nonzero, ignore all but this many trailing words,
         * where words are separated by the '/' char
     * Example:
     *     comparison    num=1    num=2    num=3
     *    a/b/z =? 1/y/z    match    no    no
     *    a/b/z =? 1/b/z    match    match    no
     */
    int numFinalWordsToMatch;
} stringmap_t;

stringmap_t *stringmap_load(const char *filename, int numFinalWordsToMatch);
const char *stringmap_lookup(const stringmap_t *map, const char *string);

#endif
