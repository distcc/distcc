/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2001-2004 by Martin Pool
 * Copyright (C) 1996-2001 by Andrew Tridgell
 * Copyright (C) 1996 by Paul Mackerras
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
 * ssh.c -- Open a connection a server over ssh or something similar.
 *
 * The ssh connection always opens immediately from distcc's point of view,
 * because the local socket/pipe to the child is ready.  If the remote
 * connection failed or is slow, distcc will only know when it tries to read
 * or write.  (And in fact the first page or more written will go out
 * immediately too...)
 *
 * This file always uses nonblocking ssh, which has proven in rsync to be the
 * better solution for ssh.  It may cause trouble with ancient proprietary rsh
 * implementations which can't handle their input being in nonblocking mode.
 * rsync has a configuration option for that, but I don't support it here,
 * because there's no point using rsh, you might as well use the native
 * protocol.
 */


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "util.h"
#include "exec.h"
#include "snprintf.h"
#include "netutil.h"

const char *dcc_default_ssh = "ssh";




/**
 * Create a file descriptor pair - like pipe() but use socketpair if
 * possible (because of blocking issues on pipes).
 *
 * Always set non-blocking.
 */
static int fd_pair(int fd[2])
{
    int ret;

#if HAVE_SOCKETPAIR
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
#else
    ret = pipe(fd);
#endif

    if (ret == 0) {
        dcc_set_nonblocking(fd[0]);
        dcc_set_nonblocking(fd[1]);
    }

    return ret;
}


/**
 * Create a child connected to use on stdin/stdout.
 *
 * This is derived from CVS code
 *
 * Note that in the child STDIN is set to blocking and STDOUT is set to
 * non-blocking. This is necessary as rsh relies on stdin being blocking and
 * ssh relies on stdout being non-blocking
 **/
static int dcc_run_piped_cmd(char **argv,
                             int *f_in,
                             int *f_out,
                             pid_t * child_pid)
{
    pid_t pid;
    int to_child_pipe[2];
    int from_child_pipe[2];

    dcc_trace_argv("execute", argv);

    if (fd_pair(to_child_pipe) < 0) {
        rs_log_error("fd_pair: %s", strerror(errno));
        return EXIT_IO_ERROR;
    }

    if (fd_pair(from_child_pipe) < 0) {
        dcc_close(to_child_pipe[0]);
        dcc_close(to_child_pipe[1]);
        rs_log_error("fd_pair: %s", strerror(errno));
        return EXIT_IO_ERROR;
    }

    *child_pid = pid = fork();
    if (pid == -1) {
        rs_log_error("fork failed: %s", strerror(errno));
        dcc_close(to_child_pipe[0]);
        dcc_close(to_child_pipe[1]);
        dcc_close(from_child_pipe[0]);
        dcc_close(from_child_pipe[1]);
        return EXIT_IO_ERROR;
    }

    if (pid == 0) {
        if (dup2(to_child_pipe[0], STDIN_FILENO) < 0 ||
            close(to_child_pipe[1]) < 0 ||
            close(from_child_pipe[0]) < 0 ||
            dup2(from_child_pipe[1], STDOUT_FILENO) < 0) {
            rs_log_error("dup/close: %s", strerror(errno));
            return EXIT_IO_ERROR;
        }
        if (to_child_pipe[0] != STDIN_FILENO)
            close(to_child_pipe[0]);
        if (from_child_pipe[1] != STDOUT_FILENO)
            close(from_child_pipe[1]);
        dcc_set_blocking(STDIN_FILENO);

        execvp(argv[0], (char **) argv);
        rs_log_error("failed to exec %s: %s", argv[0], strerror(errno));
        return EXIT_IO_ERROR;
    }

    if (dcc_close(from_child_pipe[1]) || dcc_close(to_child_pipe[0])) {
        rs_log_error("failed to close pipes");
        return EXIT_IO_ERROR;
    }

    *f_in = from_child_pipe[0];
    *f_out = to_child_pipe[1];

    return 0;
}



/**
 * Open a connection to a remote machine over ssh.
 *
 * Based on code in rsync, but rewritten.
 *
 * @note The tunnel command is always opened directly using execvp(), not
 * through a shell.  So you cannot pass shell operators like redirections, and
 * at the moment you cannot specify additional options.  Perhaps it would be
 * nice for us to parse it into an argv[] string by splitting on
 * wildcards/quotes, but at the moment this seems redundant.  It can be done
 * adequately using .ssh/config I think.
 *
 * @note the ssh command does need to be tokenized as we have hundreds of
 * users and a corporate requirement that keeps us from modifying the
 * system ssh config files. We can at the same time set command-line options
 * through the tool in use one level above this. - prw 08/09/2016
 *
 **/
int dcc_ssh_connect(char *ssh_cmd,
                    char *user,
                    char *machine,
                    char *path,
                    int *f_in, int *f_out,
                    pid_t *ssh_pid)
{
    pid_t ret;
    const int max_ssh_args = 12;
    char *ssh_args[max_ssh_args];
    char *child_argv[11+max_ssh_args];
    int i,j;
    int num_ssh_args = 0;
    char *ssh_cmd_in;

    /* We need to cast away constness.  I promise the strings in the argv[]
     * will not be modified. */

    if (!ssh_cmd && (ssh_cmd_in = getenv("DISTCC_SSH"))) {
        ssh_cmd = strtok(ssh_cmd_in, " ");
        char *token = strtok(NULL, " ");
        while (token != NULL) {
            ssh_args[num_ssh_args++] = token;
            token = strtok(NULL, " ");
            if (num_ssh_args == max_ssh_args)
                break;
        }
    }
    if (!ssh_cmd)
        ssh_cmd = (char *) dcc_default_ssh;

    if (!machine) {
        rs_log_crit("no machine defined!");
        return EXIT_DISTCC_FAILED;
    }
    if (!path)
        path = (char *) "distccd";

    i = 0;
    child_argv[i++] = ssh_cmd;
    for (j=0; j<num_ssh_args; ) {
        child_argv[i++] = ssh_args[j++];
    }

    if (user) {
        child_argv[i++] = (char *) "-l";
        child_argv[i++] = user;
    }
    child_argv[i++] = machine;
    child_argv[i++] = path;
    child_argv[i++] = (char *) "--inetd";
    child_argv[i++] = (char *) "--enable-tcp-insecure";
    child_argv[i++] = NULL;

    rs_trace("connecting to %s using %s", machine, ssh_cmd);

    /* TODO: If we're verbose, perhaps make the server verbose too, and send
     * its log to our stderr? */
    /*     child_argv[i++] = (char *) "--log-stderr"; */

    ret = dcc_run_piped_cmd(child_argv, f_in, f_out, ssh_pid);

    return ret;
}
