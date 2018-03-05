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


            /* 18 Their bows also shall dash the young men
             * to pieces; and they shall have no pity on
             * the fruit of the womb; their eyes shall not
             * spare children.
             *        -- Isaiah 13 */

/**
 * @file
 *
 * Run compilers or preprocessors.
 *
 * The whole server is run in a separate process group and normally in a
 * separate session.  (It is not a separate session in --no-detach debug
 * mode.)  This allows us to cleanly kill off all children and all compilers
 * when the parent is terminated.
 *
 * @todo On Cygwin, fork() must be emulated and therefore will be
 * slow.  It would be faster to just use their spawn() call, rather
 * than fork/exec.
 **/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifdef __CYGWIN__
    /* #define NOGDI */
    #define RC_INVOKED
    #define NOWINRES
    #include <windows.h>
#endif

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"
#include "exec.h"
#include "lock.h"
#include "hosts.h"
#include "dopt.h"

const int timeout_null_fd = -1;
int dcc_job_lifetime = 0;

static void dcc_inside_child(char **argv,
                             const char *stdin_file,
                             const char *stdout_file,
                             const char *stderr_file) NORETURN;


static void dcc_execvp(char **argv) NORETURN;

void dcc_note_execution(struct dcc_hostdef *host, char **argv)
{
    char *astr;

    astr = dcc_argv_tostr(argv);
    rs_log(RS_LOG_INFO|RS_LOG_NONAME, "exec on %s: %s",
           host->hostdef_string, astr);
    free(astr);
}


/**
 * Redirect stdin/out/err.  Filenames may be NULL to leave them untouched.
 *
 * This is called when running a job remotely, but *not* when running
 * it locally, because people might e.g. want cpp to read from stdin.
 **/
int dcc_redirect_fds(const char *stdin_file,
                     const char *stdout_file,
                     const char *stderr_file)
{
    int ret;

    if (stdin_file)
        if ((ret = dcc_redirect_fd(STDIN_FILENO, stdin_file, O_RDONLY)))
            return ret;

    if (stdout_file) {
        if ((ret = dcc_redirect_fd(STDOUT_FILENO, stdout_file,
                                   O_WRONLY | O_CREAT | O_TRUNC)))
            return ret;
    }

    if (stderr_file) {
        /* Open in append mode, because the server will dump its own error
         * messages into the compiler's error file.  */
        if ((ret = dcc_redirect_fd(STDERR_FILENO, stderr_file,
                                   O_WRONLY | O_CREAT | O_APPEND)))
            return ret;
    }

    return 0;
}


#ifdef __CYGWIN__
/* Execute a process WITHOUT console window and correctly redirect output. */
static DWORD dcc_execvp_cyg(char **argv, const char *input_file,
    const char *output_file, const char *error_file)
{
    STARTUPINFO    m_siStartInfo;
    PROCESS_INFORMATION m_piProcInfo;
    char cmdline[MAX_PATH+1]={0};
    HANDLE stdin_hndl=INVALID_HANDLE_VALUE;
    HANDLE stdout_hndl=INVALID_HANDLE_VALUE;
    HANDLE stderr_hndl=INVALID_HANDLE_VALUE;
    char **ptr;
    DWORD exit_code;
    BOOL bRet=0;

    ZeroMemory(&m_siStartInfo, sizeof(STARTUPINFO));
    ZeroMemory( &m_piProcInfo, sizeof(PROCESS_INFORMATION) );

    /* Open files for IO redirection */
    if (input_file && strcmp(input_file,"/dev/null")!=0)
    {
        if ((stdin_hndl = CreateFile(input_file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY,NULL)) == INVALID_HANDLE_VALUE) {
            exit_code = GetLastError();
            goto cleanup;
        }
    } else
        stdin_hndl = GetStdHandle(STD_INPUT_HANDLE);

    if (output_file && strcmp(output_file,"/dev/null")!=0)
    {
        if ((stdout_hndl = CreateFile(output_file,GENERIC_WRITE,FILE_SHARE_READ,NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,NULL)) == INVALID_HANDLE_VALUE) {
            exit_code = GetLastError();
            goto cleanup;
        }
    } else
        stdout_hndl = GetStdHandle(STD_OUTPUT_HANDLE);

    if (error_file && strcmp(error_file,"/dev/null")!=0)
    {
        if ((stderr_hndl = CreateFile(error_file, GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,NULL)) == INVALID_HANDLE_VALUE) {
            exit_code = GetLastError();
            goto cleanup;
        }
        /* Seek to the end of file (ignore return code) */
        SetFilePointer(stderr_hndl,0,NULL,FILE_END);

    } else
        stderr_hndl = GetStdHandle(STD_ERROR_HANDLE);

    /* Ensure handles can be inherited */
    SetHandleInformation(stdin_hndl,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT);
    SetHandleInformation(stdout_hndl,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT);
    SetHandleInformation(stderr_hndl,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT);

    /*Set up members of STARTUPINFO structure.*/
    m_siStartInfo.cb = sizeof(STARTUPINFO);
    m_siStartInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    m_siStartInfo.wShowWindow = SW_HIDE;
    m_siStartInfo.hStdInput = stdin_hndl;
    m_siStartInfo.hStdOutput = stdout_hndl;
    m_siStartInfo.hStdError = stderr_hndl;

    /* Create command line */
    for (ptr=argv;*ptr!=NULL;ptr++)
    {
        strcat(cmdline, *ptr);
        strcat(cmdline, " ");
    }

    /* Create the child process.  */
    bRet = CreateProcess(NULL,
        cmdline,        /* application name */
        NULL,           /* process security attributes */
        NULL,           /* primary thread security attributes */
        TRUE,           /* handles are inherited */
        CREATE_NEW_CONSOLE, /* creation flags */
        NULL,           /* use parent's environment */
        NULL,           /* use parent's current directory */
        &m_siStartInfo,  /* STARTUPINFO pointer */
        &m_piProcInfo);  /* receives PROCESS_INFORMATION */
    if (!bRet) {
        exit_code = GetLastError();
        goto cleanup;
    }

    WaitForSingleObject(m_piProcInfo.hProcess, (DWORD)(-1L));
    /* return termination code and exit code*/
    GetExitCodeProcess(m_piProcInfo.hProcess, &exit_code);
    CloseHandle(m_piProcInfo.hProcess);

    /* We can get here only if process creation failed */
    cleanup:
    if (stdin_hndl != INVALID_HANDLE_VALUE) CloseHandle(stdin_hndl);
    if (stdout_hndl != INVALID_HANDLE_VALUE) CloseHandle(stdout_hndl);
    if (stderr_hndl != INVALID_HANDLE_VALUE) CloseHandle(stderr_hndl);

    if (bRet)
        ExitProcess(exit_code); /* Return cmdline's exit-code to parent process */
    else
        return exit_code;       /* Return failure reason to calling fn */
}
#endif

/**
 * Replace this program with another in the same process.
 *
 * Does not return, either execs the compiler in place, or exits with
 * a message.
 **/
static void dcc_execvp(char **argv)
{
    char *slash;

    execvp(argv[0], argv);

    /* If we're still running, the program was not found on the path.  One
     * thing that might have happened here is that the client sent an absolute
     * compiler path, but the compiler's located somewhere else on the server.
     * In the absence of anything better to do, we search the path for its
     * basename.
     *
     * Actually this code is called on both the client and server, which might
     * cause unintnded behaviour in contrived cases, like giving a full path
     * to a file that doesn't exist.  I don't think that's a problem. */

    slash = strrchr(argv[0], '/');
    if (slash)
        execvp(slash + 1, argv);

    /* shouldn't be reached */
    rs_log_error("failed to exec %s: %s", argv[0], strerror(errno));

    dcc_exit(EXIT_COMPILER_MISSING); /* a generalization, i know */
}



/**
 * Called inside the newly-spawned child process to execute a command.
 * Either executes it, or returns an appropriate error.
 *
 * This routine also takes a lock on localhost so that it's counted
 * against the process load.  That lock will go away when the process
 * exits.
 *
 * In this current version locks are taken without regard to load limitation
 * on the current machine.  The main impact of this is that cpp running on
 * localhost will cause jobs to be preferentially distributed away from
 * localhost, but it should never cause the machine to deadlock waiting for
 * localhost slots.
 *
 * @param what Type of process to be run here (cpp, cc, ...)
 **/
static void dcc_inside_child(char **argv,
                             const char *stdin_file,
                             const char *stdout_file,
                             const char *stderr_file)
{
    int ret;

    if ((ret = dcc_ignore_sigpipe(0)))
        goto fail;              /* set handler back to default */

    /* Ignore failure */
    dcc_increment_safeguard();

#ifdef __CYGWIN__
    /* This will execute compiler and CORRECTLY redirect output if compiler is
     * a native Windows application.  If this never returns, it means the
     * compiler-execute succeeded.  We use a hack to decide if it's a windows
     * application: if argv[0] starts with "<letter>:" or with "\\", then it's
     * a windows path and we try dcc_execvp_cyg.  If not, we assume it's a
     * cygwin app and fall through to the unix-style forking, below.  If we
     * guess wrong, dcc_execvp_cyg will probably fail with error 3
     * (windows-exe for "path not found"), so again we'll fall through to the
     * unix-fork case.  Otherwise we just fail in a generic way.
     * TODO(csilvers): Figure out the right way to deal with this.  Running
     *                 cygwin apps via dcc_execvp_cyg segfaults (and takes a
     *                 long time to do it too), so I want to avoid that if
     *                 possible.  I don't know enough about cygwin or
     *                 cygwin/windows interactions to know the right thing to
     *                 do here.  Until distcc has cl.exe support, this may
     *                 all be a moot point anyway.
     */
    if (argv[0] && ((argv[0][0] != '\0' && argv[0][1] == ':') ||
                    (argv[0][0] == '\\' && argv[0][1] == '\\'))) {
        DWORD status;
        status = dcc_execvp_cyg(argv, stdin_file, stdout_file, stderr_file);
        if (status != 3) {
            ret = EXIT_DISTCC_FAILED;
            goto fail;
        }
    }
#endif

    /* do this last, so that any errors from previous operations are
     * visible */
    if ((ret = dcc_redirect_fds(stdin_file, stdout_file, stderr_file)))
        goto fail;

    dcc_execvp(argv);

    ret = EXIT_DISTCC_FAILED;

    fail:
    dcc_exit(ret);
}


int dcc_new_pgrp(void)
{
    /* If we're a session group leader, then we are not able to call
     * setpgid().  However, setsid will implicitly have put us into a new
     * process group, so we don't have to do anything. */

    /* Does everyone have getpgrp()?  It's in POSIX.1.  We used to call
     * getpgid(0), but that is not available on BSD/OS. */
    if (getpgrp() == getpid()) {
        rs_trace("already a process group leader");
        return 0;
    }

    if (setpgid(0, 0) == 0) {
        rs_trace("entered process group");
        return 0;
    } else {
        rs_trace("setpgid(0, 0) failed: %s", strerror(errno));
        return EXIT_DISTCC_FAILED;
    }
}


/**
 * Run @p argv in a child asynchronously.
 *
 * stdin, stdout and stderr are redirected as shown, unless those
 * filenames are NULL.  In that case they are left alone.
 *
 * @warning When called on the daemon, where stdin/stdout may refer to random
 * network sockets, all of the standard file descriptors must be redirected!
 **/
int dcc_spawn_child(char **argv, pid_t *pidptr,
                    const char *stdin_file,
                    const char *stdout_file,
                    const char *stderr_file)
{
    pid_t pid;

    dcc_trace_argv("forking to execute", argv);

    pid = fork();
    if (pid == -1) {
        rs_log_error("failed to fork: %s", strerror(errno));
        return EXIT_OUT_OF_MEMORY; /* probably */
    } else if (pid == 0) {
        /* If this is a remote compile,
         * put the child in a new group, so we can
         * kill it and all its descendents without killing distccd
         * FIXME: if you kill distccd while it's compiling, and
         * the compiler has an infinite loop bug, the new group
         * will run forever until you kill it.
         */
        if (stdout_file != NULL) {
            if (dcc_new_pgrp() != 0)
                rs_trace("Unable to start a new group\n");
        }
        dcc_inside_child(argv, stdin_file, stdout_file, stderr_file);
        /* !! NEVER RETURN FROM HERE !! */
    } else {
        *pidptr = pid;
        rs_trace("child started as pid%d", (int) pid);
        return 0;
    }
}


void dcc_reset_signal(int whichsig)
{
    struct sigaction act_dfl;

    memset(&act_dfl, 0, sizeof act_dfl);
    act_dfl.sa_handler = SIG_DFL;
    sigaction(whichsig, &act_dfl, NULL);
    /* might be called from signal handler, therefore no IO to log a
     * message */
}


static int sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{

    /* Prefer use waitpid to wait4 for non-blocking wait with WNOHANG option */
#ifdef HAVE_WAITPID
    /* Just doing getrusage(children) is not sufficient, because other
     * children may have exited previously. */
    memset(rusage, 0, sizeof *rusage);
    return waitpid(pid, status, options);
#elif HAVE_WAIT4
    return wait4(pid, status, options, rusage);
#else
#error Please port this
#endif
}


/**
 * Blocking wait for a child to exit.  This is used when waiting for
 * cpp, gcc, etc.
 *
 * This is not used by the daemon-parent; it has its own
 * implementation in dcc_reap_kids().  They could be unified, but the
 * parent only waits when it thinks a child has exited; the child
 * waits all the time.
 **/
int dcc_collect_child(const char *what, pid_t pid,
                      int *wait_status, int in_fd)
{
    struct rusage ru;
    pid_t ret_pid;

    int ret;
    int wait_timeout_sec;
    fd_set fds,readfds;

    wait_timeout_sec = dcc_job_lifetime;

    FD_ZERO(&readfds);
    if (in_fd != timeout_null_fd){
        FD_SET(in_fd,&readfds);
    }


    while (!dcc_job_lifetime || wait_timeout_sec-- >= 0) {

        /* If we're called with a socket, break out of the loop if the socket
         * disconnects. To do that, we need to block in select, not in
         * sys_wait4.  (Only waitpid uses WNOHANG to mean don't block ever,
         * so I've modified sys_wait4 above to preferentially call waitpid.)
         */
        int flags = (in_fd == timeout_null_fd) ? 0 : WNOHANG;
        ret_pid = sys_wait4(pid, wait_status, flags, &ru);

        if (ret_pid == -1) {
            if (errno == EINTR) {
                rs_trace("wait4 was interrupted; retrying");
            } else {
                rs_log_error("sys_wait4(pid=%d) borked: %s", (int) pid,
                             strerror(errno));
                return EXIT_DISTCC_FAILED;
            }
        } else if (ret_pid != 0) {
            /* This is not the main user-visible message; that comes from
             * critique_status(). */
            rs_trace("%s child %ld terminated with status %#x",
                     what, (long) ret_pid, *wait_status);
            rs_log_info("%s times: user %lld.%06lds, system %lld.%06lds, "
                        "%ld minflt, %ld majflt",
                        what,
                        (long long) ru.ru_utime.tv_sec, (long) ru.ru_utime.tv_usec,
                        (long long) ru.ru_stime.tv_sec, (long) ru.ru_stime.tv_usec,
                        ru.ru_minflt, ru.ru_majflt);

            return 0;
        }

        /* check timeout */
        if (in_fd != timeout_null_fd) {
            struct timeval timeout;

            /* If client disconnects, the socket will become readable,
             * and a read should return -1 and set errno to EPIPE.
             */
            fds = readfds;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            ret = select(in_fd+1,&fds,NULL,NULL,&timeout);
            if (ret == 1) {
                char buf;
                int nread = read(in_fd, &buf, 1);
                if ((nread == -1) && (errno == EWOULDBLOCK)) {
                    /* spurious wakeup, ignore */
                    ;
                } else if (nread == 0) {
                    rs_log_error("Client fd disconnected, killing job");
                    /* If killpg fails, it might means the child process is not
                     * in a new group, so, just kill the child process */
                    if (killpg(pid,SIGTERM)!=0)
                        kill(pid, SIGTERM);
                    return EXIT_IO_ERROR;
                } else if (nread == 1) {
                    rs_log_error("Bug!  Read from fd succeeded when checking "
                                 "whether client disconnected!");
                } else {
                    rs_log_error("Bug!  nread %d, errno %d checking whether "
                                 "client disconnected!", nread, errno);
                }
            }
        } else {
            poll(NULL, 0, 1000);
        }
    }
    /* If timeout, also kill the child process */
    if (killpg(pid, SIGTERM) != 0)
        kill(pid, SIGTERM);
    rs_log_error("Compilation takes too long, timeout.");

    return EXIT_TIMEOUT;
}



/**
 * Analyze and report to the user on a command's exit code.
 *
 * @param command short human-readable description of the command (perhaps
 * argv[0])
 *
 * @returns 0 if the command succeeded; 128+SIGNAL if it stopped on a
 * signal; otherwise the command's exit code.
 **/
int dcc_critique_status(int status,
                        const char *command,
                        const char *input_fname,
                        struct dcc_hostdef *host,
                        int verbose)
{
    int logmode;

    /* verbose mode is only used for executions that the user is likely to
     * particularly need to know about */
    if (verbose)
        logmode = RS_LOG_ERR | RS_LOG_NONAME;
    else
        logmode = RS_LOG_INFO | RS_LOG_NONAME;

    if (input_fname == NULL)
        input_fname = "(null)";

    if (WIFSIGNALED(status)) {
#ifdef HAVE_STRSIGNAL
        rs_log(logmode,
               "%s %s on %s: %s%s",
               command, input_fname, host->hostdef_string,
               strsignal(WTERMSIG(status)),
               WCOREDUMP(status) ? " (core dumped)" : "");
#else
        rs_log(logmode,
               "%s %s on %s terminated by signal %d%s",
               command, input_fname, host->hostdef_string,
               WTERMSIG(status),
               WCOREDUMP(status) ? " (core dumped)" : "");
#endif
        /* Unix convention is to return 128+signal when a subprocess crashes. */
        return 128 + WTERMSIG(status);
    } else if (WEXITSTATUS(status) == 1) {
        /* Normal failure gives exit code 1, so handle that specially */
        rs_log(logmode, "%s %s on %s failed", command, input_fname, host->hostdef_string);
        return WEXITSTATUS(status);
    } else if (WEXITSTATUS(status)) {
        /* This is a tough call; we don't really want to clutter the client's
         * error stream, but if we don't say where the compilation failed then
         * people may find it hard to work things out. */

        rs_log(logmode,
               "%s %s on %s failed with exit code %d",
               command, input_fname, host->hostdef_string, WEXITSTATUS(status));
        return WEXITSTATUS(status);
    } else {
        rs_log(RS_LOG_INFO|RS_LOG_NONAME,
               "%s %s on %s completed ok", command, input_fname, host->hostdef_string);
        return 0;
    }
}
