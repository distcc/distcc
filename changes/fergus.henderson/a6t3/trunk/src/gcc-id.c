/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * Copyright (C) 2007 Lennart Poettering
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

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "distcc.h"
#include "hosts.h"
#include "zeroconf.h"
#include "trace.h"

static char *strip_bad_chars(char *s) {
    char *k;

    for (k = s; *k; k++) {

        if (*k >= 'a' && *k <= 'z')
            continue;

        if (*k >= '0' && *k <= '9')
            continue;

        if (*k >= 'A' && *k <= 'Z')
            *k = tolower(*k);
        else
            *k = '-';
    }

    return s;
}

static char* read_string_from_popen(const char *cmdline, char *s, size_t nbytes) {
    FILE *p = NULL;
    char *ret = NULL;

    errno = 0;
    if (!(p = popen(cmdline, "r"))) {
        rs_log_crit("Failed to read string from C compiler: %s\n", errno ? strerror(errno) : "failure");
        goto fail;
    }

    if (!fgets(s, (int) nbytes, p)) {
        rs_log_crit("Failed to read string from C compiler.\n");
        goto fail;
    }

    s[nbytes-1] = 0;
    s[strcspn(s, " \t\n\r")] = 0;

    ret = s;

fail:

    if (p)
        pclose(p);

    return ret;
}

char* dcc_get_gcc_version(char *s, size_t nbytes) {
    return read_string_from_popen("cc -dumpversion", s, nbytes);
}

char* dcc_get_gcc_machine(char *s, size_t nbytes) {
    return read_string_from_popen("cc -dumpmachine", s, nbytes);
}

char* dcc_make_dnssd_subtype(char *stype, size_t nbytes, const char *v, const char *m) {
    char version[64], machine[64];

    strncpy(version, v, sizeof(version)-1);
    version[sizeof(version)-1] = 0;
    strncpy(machine, m, sizeof(machine)-1);
    machine[sizeof(machine)-1] = 0;

    strip_bad_chars(version);
    strip_bad_chars(machine);

    snprintf(stype, nbytes, "_%s--%s._sub." DCC_DNS_SERVICE_TYPE, machine, version);
    stype[nbytes-1] = 0;

    return stype;
}
