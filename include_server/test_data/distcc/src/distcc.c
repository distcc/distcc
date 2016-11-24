/* -*- c-file-style: "java"; indent-tabs-mode: nil -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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


			/* 4: The noise of a multitude in the
			 * mountains, like as of a great people; a
			 * tumultuous noise of the kingdoms of nations
			 * gathered together: the LORD of hosts
			 * mustereth the host of the battle.
			 *		-- Isaiah 13 */



#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "util.h"
#include "hosts.h"
#include "bulk.h"
#include "implicit.h"
#include "compile.h"
#include "emaillog.h"


/* Name of this program, for trace.c */
const char *rs_program_name = "distcc";


/**
 * @file
 *
 * Entry point for the distcc client.
 *
 * There are three methods of use for distcc: explicit (distcc gcc -c
 * foo.c), implicit (distcc -c foo.c) and masqueraded (gcc -c foo.c,
 * where gcc is really a link to distcc).
 *
 * Detecting these is relatively easy by examining the first one or
 * two words of the command.  We also need to make sure that when we
 * go to run the compiler, we run the one intended by the user.
 *
 * In particular, for masqueraded mode, we want to make sure that we
 * don't invoke distcc recursively.
 **/

static void dcc_show_usage(void)
{
    dcc_show_version("distcc");
    printf(
"Usage:\n"
"   distcc [COMPILER] [compile options] -o OBJECT -c SOURCE\n"
"   distcc --help\n"
"\n"
"Options:\n"
"   COMPILER                   defaults to \"cc\"\n"
"   --help                     explain usage and exit\n"
"   --version                  show version and exit\n"
"\n"
"Environment variables:\n"
"   See the manual page for a complete list.\n"
"   DISTCC_VERBOSE=1           give debug messages\n"
"   DISTCC_LOG                 send messages to file, not stderr\n"
"   DISTCC_SSH                 command to run to open SSH connections\n"
"   DISTCC_DIR                 directory for host list and locks\n"
"\n"
"Server specification:\n"
"A list of servers is taken from the environment variable $DISTCC_HOSTS, or\n"
"$DISTCC_DIR/hosts, or ~/.distcc/hosts, or %s/distcc/hosts.\n"
"Each host can be given in any of these forms, see the manual for details:\n"
"\n"
"   localhost                  run in place\n"
"   HOST                       TCP connection, port %d\n"
"   HOST:PORT                  TCP connection, specified port\n"
"   @HOST                      SSH connection\n"
"   USER@HOST                  SSH connection to specified host\n"
"   --randomize                Randomize the server list before execution\n"
"\n"
"distcc distributes compilation jobs across volunteer machines running\n"
"distccd.  Jobs that cannot be distributed, such as linking or \n"
"preprocessing are run locally.  distcc should be used with make's -jN\n"
"option to execute in parallel on several machines.\n",
    SYSCONFDIR,
    DISTCC_DEFAULT_PORT);
}


static RETSIGTYPE dcc_client_signalled (int whichsig)
{
    signal(whichsig, SIG_DFL);

#ifdef HAVE_STRSIGNAL
    rs_log_info("%s", strsignal(whichsig));
#else
    rs_log_info("terminated by signal %d", whichsig);
#endif

    dcc_cleanup_tempfiles_from_signal_handler();

    raise(whichsig);

}


static void dcc_client_catch_signals(void)
{
    signal(SIGTERM, &dcc_client_signalled);
    signal(SIGINT, &dcc_client_signalled);
    signal(SIGHUP, &dcc_client_signalled);
}



/**
 * distcc client entry point.
 *
 * This is typically called by make in place of the real compiler.
 *
 * Performs basic setup and checks for distcc arguments, and then kicks off
 * dcc_build_somewhere().
 **/
int main(int argc, char **argv)
{
    int status, sg_level, tweaked_path = 0;
    char **compiler_args;
    char *compiler_name;
    int ret;

    dcc_client_catch_signals();
    atexit(dcc_cleanup_tempfiles);
    atexit(dcc_remove_state_file);

    dcc_set_trace_from_env();
    dcc_setup_log_email();

    dcc_trace_version();

    compiler_name = (char *) dcc_find_basename(argv[0]);

    /* Ignore SIGPIPE; we consistently check error codes and will
     * see the EPIPE. */
    dcc_ignore_sigpipe(1);

    sg_level = dcc_recursion_safeguard();

    rs_trace("compiler name is \"%s\"", compiler_name);

    if (strstr(compiler_name, "distcc") != NULL) {
        /* Either "distcc -c hello.c" or "distcc gcc -c hello.c" */
        if (argc <= 1 || !strcmp(argv[1], "--help")) {
            dcc_show_usage();
            ret = 0;
            goto out;
        }
        if (!strcmp(argv[1], "--version")) {
            dcc_show_version("distcc");
            ret = 0;
            goto out;
        }

        dcc_find_compiler(argv, &compiler_args);
        /* compiler_args is now respectively either "cc -c hello.c" or
         * "gcc -c hello.c" */

#if 0
        /* I don't think we need to call this: if we reached this
         * line, our invocation name is something like 'distcc', and
         * that's never a problem for masquerading loops. */
        if ((ret = dcc_trim_path(compiler_name)) != 0)
            goto out;
#endif
    } else {
        /* Invoked as "cc -c hello.c", with masqueraded path */
        if ((ret = dcc_support_masquerade(argv, compiler_name,
                                          &tweaked_path)) != 0)
            goto out;

        dcc_copy_argv(argv, &compiler_args, 0);
        compiler_args[0] = compiler_name;
    }

    if (sg_level - tweaked_path > 0) {
        rs_log_crit("distcc seems to have invoked itself recursively!");
        ret = EXIT_RECURSION;
        goto out;
    }

    ret = dcc_build_somewhere_timed(compiler_args, sg_level, &status);

    out:
    dcc_maybe_send_email();
    dcc_exit(ret);
}
