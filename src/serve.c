/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool
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

                /* He who waits until circumstances completely favour *
                 * his undertaking will never accomplish anything.    *
                 *              -- Martin Luther                      */


/**
 * @file
 *
 * Actually serve remote requests.  Called from daemon.c.
 *
 * @todo Make sure wait statuses are packed in a consistent format
 * (exit<<8 | signal).  Is there any platform that doesn't do this?
 *
 * @todo The server should catch signals, and terminate the compiler process
 * group before handling them.
 *
 * @todo It might be nice to detect that the client has dropped the
 * connection, and then kill the compiler immediately.  However, we probably
 * won't notice that until we try to do IO.  SIGPIPE won't help because it's
 * not triggered until we try to do IO.  I don't think it matters a lot,
 * though, because the client's not very likely to do that.  The main case is
 * probably somebody getting bored and interrupting compilation.
 *
 * What might help is to select() on the network socket while we're waiting
 * for the child to complete, allowing SIGCHLD to interrupt the select() when
 * the child completes.  However I'm not sure if it's really worth the trouble
 * of doing that just to handle a fairly marginal case.
 **/



#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "stats.h"
#include "rpc.h"
#include "exitcode.h"
#include "snprintf.h"
#include "dopt.h"
#include "bulk.h"
#include "exec.h"
#include "srvnet.h"
#include "hosts.h"
#include "daemon.h"
#include "stringmap.h"
#include "dotd.h"
#include "fix_debug_info.h"
#ifdef HAVE_GSSAPI
#include "auth.h"

/* Global security context in case confidentiality/integrity */
/* type services are needed in the future. */
extern gss_ctx_id_t distccd_ctx_handle;

/* Simple boolean, with a non-zero value indicating that the */
/* --auth option was specified. */
int dcc_auth_enabled = 0;
#endif

/**
 * We copy all serious distccd messages to this file, as well as sending the
 * compiler errors there, so they're visible to the client.
 **/
static int dcc_compile_log_fd = -1;

static int dcc_run_job(int in_fd, int out_fd);


/**
 * Copy all server messages to the error file, so that they can be
 * echoed back to the client if necessary.
 **/
static int dcc_add_log_to_file(const char *err_fname)
{
    if (dcc_compile_log_fd != -1) {
        rs_log_crit("compile log already open?");
        return 0;               /* continue? */
    }

    dcc_compile_log_fd = open(err_fname, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (dcc_compile_log_fd == -1) {
        rs_log_error("failed to open %s: %s", err_fname, strerror(errno));
        return EXIT_IO_ERROR;
    }

    /* Only send fairly serious errors back */
    rs_add_logger(rs_logger_file, RS_LOG_WARNING, NULL, dcc_compile_log_fd);

    return 0;
}



static int dcc_remove_log_to_file(void)
{
    if (dcc_compile_log_fd == -1) {
        rs_log_warning("compile log not open?");
        return 0;               /* continue? */
    }

    /* must exactly match call in dcc_add_log_to_file */
    rs_remove_logger(rs_logger_file, RS_LOG_WARNING, NULL,
                     dcc_compile_log_fd);

    dcc_close(dcc_compile_log_fd);

    dcc_compile_log_fd = -1;

    return 0;
}



/* Read and execute a job to/from socket.  This is the common entry point no
 * matter what mode the daemon is running in: preforked, nonforked, or
 * ssh/inetd.
 */
int dcc_service_job(int in_fd,
                    int out_fd,
                    struct sockaddr *cli_addr,
                    int cli_len)
{
    int ret;

    dcc_job_summary_clear();

    /* Log client name and check access if appropriate.  For ssh connections
     * the client comes from a unix-domain socket and that's always
     * allowed. */
    if ((ret = dcc_check_client(cli_addr, cli_len, opt_allowed)) != 0)
        goto out;

#ifdef HAVE_GSSAPI
    /* If requested perform authentication. */
    if (dcc_auth_enabled) {
	    rs_log_info("Performing authentication.");

        if ((ret = dcc_gssapi_check_client(in_fd, out_fd)) != 0) {
            goto out;
        }
    } else {
	    rs_log_info("No authentication requested.");
    }

    /* Context deleted here as we no longer need it.  However, we have it available */
    /* in case we want to use confidentiality/integrity type services in the future. */
    if (dcc_auth_enabled) {
        dcc_gssapi_delete_ctx(&distccd_ctx_handle);
    }
#endif

    ret = dcc_run_job(in_fd, out_fd);

    dcc_job_summary();

out:
    return ret;
}


static int dcc_input_tmpnam(char * orig_input,
                            char **tmpnam_ret)
{
    const char *input_exten;

    rs_trace("input file %s", orig_input);
    input_exten = dcc_find_extension(orig_input);
    if (input_exten)
        input_exten = dcc_preproc_exten(input_exten);
    if (!input_exten)           /* previous line might return NULL */
        input_exten = ".tmp";
    return dcc_make_tmpnam("distccd", input_exten, tmpnam_ret);
}



/**
 * Check argv0 against a list of allowed commands, and possibly map it to a new value.
 * If *compiler_name is changed, the original value is free'd, and a new value is malloc'd.
 *
 * If the environment variable DISTCC_CMDLIST is set,
 * load a list of supported commands from the file named by DISTCC_CMDLIST, and
 * refuse to serve any command whose last DISTCC_CMDLIST_NUMWORDS last words
 * don't match those of a command in that list.
 * Each line of the file is simply a filename.
 * This is chiefly useful for those few installations which have so many
 * compilers available such that the compiler must be specified with an absolute pathname.
 *
 * Example: if the compilers are installed in a different location on
 * this server, e.g. if they've been copied from a shared NFS directory onto a
 * local hard drive, you might have lines like
 *   /local/tools/blort/sh4-linux/gcc-3.3.3-glibc-2.2.5/bin/sh4-linux-gcc
 *   /local/tools/blort/sh4-linux/gcc-2.95.3-glibc-2.2.5/bin/sh4-linux-gcc
 * and set DISTCC_CMDLIST_NUMWORDS=3; that way e.g. any of the commands
 *   /local/tools/gcc-3.3.3-glibc-2.2.5/bin/sh4-linux-gcc
 *   /shared/tools/gcc-3.3.3-glibc-2.2.5/bin/sh4-linux-gcc
 *   /zounds/gcc-3.3.3-glibc-2.2.5/bin/sh4-linux-gcc
 * will invoke
 *   /local/tools/blort/sh4-linux/gcc-3.3.3-glibc-2.2.5/bin/sh4-linux-gcc
 *
 * Returns 0 (which will abort the compile) if compiler not in list.
 * (This is because the list is intended to be complete,
 * and any attempt to use a command not in the list indicates a confused user.
 * FIXME: should probably give user the option of changing this
 * behavior at runtime, so normal command lookup can continue even if command
 * not found in table.)
 **/
static int dcc_remap_compiler(char **compiler_name)
{
    static int cmdlist_checked=0;
    static stringmap_t *map=0;
    const char *newname;

    /* load file if not already */
    if (!cmdlist_checked) {
        char *filename;
        cmdlist_checked = 1;
        filename = getenv("DISTCC_CMDLIST");
        if (filename) {
            const char *nw = getenv("DISTCC_CMDLIST_NUMWORDS");
            int numFinalWordsToMatch=1;
            if (nw)
                numFinalWordsToMatch = atoi(nw);
            map = stringmap_load(filename, numFinalWordsToMatch);
            if (map) {
                rs_trace("stringmap_load(%s, %d) found %d commands", filename, numFinalWordsToMatch, map->n);
            } else {
                rs_log_error("stringmap_load(%s, %d) failed: %s", filename, numFinalWordsToMatch, strerror(errno));
                return EXIT_IO_ERROR;
            }
        }
    }

    if (!map)
        return 1;    /* no list of allowed names, so ok */

    /* Find what this compiler maps to */
    newname = stringmap_lookup(map, *compiler_name);
    if (!newname) {
        rs_log_warning("lookup of %s in DISTCC_CMDLIST failed", *compiler_name);
        return 0;    /* not in list, so forbidden.  FIXME: make failure an option */
    }

    /* If mapping is not the identity mapping, replace the original name */
    if (strcmp(newname, *compiler_name)) {
        rs_trace("changed compiler from %s to %s", *compiler_name, newname);
        free(*compiler_name);
        *compiler_name = strdup(newname);
    }
    return 1;
}


/**
 * Find the absolute path for the first occurrence of @p compiler_name on the
 * PATH.  Print a warning if it looks like a symlink to distcc.
 *
 * We want to guard against somebody accidentally running the server with a
 * masqueraded compiler on its $PATH.  The worst that's likely to happen here
 * is wasting some time running a distcc or ccache client that does nothing,
 * so it's not a big deal.  (This could be easy to do if it's on the default
 * PATH and they start the daemon from the command line.)
 *
 * At the moment we don't look for the compiler too.
 **/
static int dcc_check_compiler_masq(char *compiler_name)
{
    const char *envpath, *p, *n;
    char *buf = NULL;
    struct stat sb;
    int len;
    char linkbuf[MAXPATHLEN];

    if (compiler_name[0] == '/')
        return 0;

    if (!(envpath = getenv("PATH"))) {
        rs_trace("PATH seems not to be defined");
        return 0;
    }

    for (n = p = envpath; *n; p = n) {
        n = strchr(p, ':');
        if (n)
            len = n++ - p;
        else {
            len = strlen(p);
            n = p + len;
        }
        if (asprintf(&buf, "%.*s/%s", len, p, compiler_name) == -1) {
            rs_log_crit("asprintf failed");
            return EXIT_DISTCC_FAILED;
        }

        if (lstat(buf, &sb) == -1)
            continue;           /* ENOENT, EACCESS, etc */
        if (!S_ISLNK(sb.st_mode)) {
            rs_trace("%s is not a symlink", buf);
            break;              /* found it */
        }
        if ((len = readlink(buf, linkbuf, sizeof linkbuf)) <= 0)
            continue;
        linkbuf[len] = '\0';

        if (strstr(linkbuf, "distcc")) {
            rs_log_warning("%s on distccd's path is %s and really a link to %s",
                           compiler_name, buf, linkbuf);
            break;              /* but use it anyhow */
        } else {
            rs_trace("%s is a safe symlink to %s", buf, linkbuf);
            break;              /* found it */
        }
    }

    free(buf);
    return 0;
}

/**
 * Make sure there is a masquerade to distcc in LIBDIR/distcc in order to
 * execute a binary of the same name.
 *
 * Before this it was possible to execute arbitrary command after connecting
 * to distcc, which is quite a security risk when combined with any local root
 * privilege escalation exploit. See CVE 2004-2687
 *
 * https://nvd.nist.gov/vuln/detail/CVE-2004-2687
 * https://github.com/distcc/distcc/issues/155
 **/
static int dcc_check_compiler_whitelist(char *_compiler_name)
{
    char *compiler_name = _compiler_name;

    /* Support QtCreator by treating /usr/bin and /bin absolute paths as non-absolute
     * see https://github.com/distcc/distcc/issues/279
     */
    const char *creator_paths[] = { "/bin/", "/usr/bin/", NULL };
    int i;
    for (i = 0 ; creator_paths[i] ; ++i) {
        size_t len = strlen(creator_paths[i]);
        // /bin and /usr/bin are absolute paths (= compare from the string start)
        // use strncasecmp() to support case-insensitive / (= on Mac).
        if (strncasecmp(_compiler_name, creator_paths[i], len) == 0) {
            compiler_name = _compiler_name + len;
            // stop at the first hit
            break;
        }
    }

    if (strchr(compiler_name, '/')) {
        rs_log_crit("compiler name <%s> cannot be an absolute path (or must set DISTCC_CMDLIST or pass --enable-tcp-insecure)", _compiler_name);
        return EXIT_BAD_ARGUMENTS;
    }

#ifdef HAVE_FSTATAT
    int dirfd = open(LIBDIR "/distcc", O_RDONLY);

    if (dirfd < 0 || faccessat(dirfd, compiler_name, X_OK, 0) < 0) {
        char *compiler_path = NULL;
        if (asprintf(&compiler_path, "/usr/lib/distcc/%s", compiler_name) >= 0) {
            if (access(compiler_path, X_OK) < 0) {
                free(compiler_path);
                close(dirfd);
                rs_log_crit("%s not in %s or %s whitelist.", compiler_name, LIBDIR "/distcc", "/usr/lib/distcc");
                return EXIT_BAD_ARGUMENTS;           /* ENOENT, EACCESS, etc */
            }
            free(compiler_path);
        }
    }

    close(dirfd);

    rs_trace("%s in" LIBDIR "/distcc whitelist", compiler_name);
    return 0;
#else
    // make do with access():
    char *compiler_path;
    int ret = 0;
    if (asprintf(&compiler_path, "%s/distcc/%s", LIBDIR, compiler_name) >= 0) {
        if (access(compiler_path, X_OK) < 0) {
            free(compiler_path);
            /* check /usr/lib/distcc too */
            if (asprintf(&compiler_path, "/usr/lib/distcc/%s", compiler_name) >= 0) {
                if (access(compiler_path, X_OK) < 0) {
                    rs_log_crit("%s not in %s or %s whitelist.", compiler_name, LIBDIR "/distcc", "/usr/lib/distcc");
                    ret = EXIT_BAD_ARGUMENTS;           /* ENOENT, EACCESS, etc */
                }
                free(compiler_path);
            }
        } else {
            free(compiler_path);
	}
        rs_trace("%s in" LIBDIR "/distcc whitelist", compiler_name);
    } else {
        rs_log_crit("Couldn't check if %s is in %s whitelist.", compiler_name, LIBDIR "/distcc");
        ret = EXIT_DISTCC_FAILED;
    }
    return ret;
#endif
}

static const char *include_options[] = {
    "-I",
    "-include",
    "-imacros",
    "-idirafter",
    "-iprefix",
    "-iwithprefix",
    "-iwithprefixbefore",
    "-isystem",
    "-iquote",
    NULL
};


/**
 * Prepend @p root_dir string to source file if absolute.
 **/
static int tweak_input_argument_for_server(char **argv,
                                           const char *root_dir)
{
    unsigned i;
    /* Look for the source file and act if absolute. Note: dcc_scan_args
     * rejects compilations with more than one source file. */
    for (i=0; argv[i]; i++)
        if (dcc_is_source(argv[i]) && argv[i][0]=='/') {
            unsigned j = 0;
            char *prefixed_name;
            while (argv[i][j] == '/') j++;
            if (asprintf(&prefixed_name, "%s/%s",
                         root_dir,
                         argv[i] + j) == -1) {
                rs_log_crit("asprintf failed");
                return EXIT_OUT_OF_MEMORY;
            }
            rs_trace("changed input from \"%s\" to \"%s\"", argv[i],
                     prefixed_name);
            free(argv[i]);
            argv[i] = prefixed_name;
            dcc_trace_argv("command after", argv);
            return 0;
        }
    return 0;
}


/**
 * Prepend @p root_dir to arguments of include options that are absolute.
 **/
static int tweak_include_arguments_for_server(char **argv,
                                              const char *root_dir)
{
    int index_of_first_filename_char = 0;
    const char *include_option;
    unsigned int i, j;
    for (i = 0; argv[i]; ++i) {
        for (j = 0; include_options[j]; ++j) {
            if (str_startswith(include_options[j], argv[i])) {
                if (strcmp(argv[i], include_options[j]) == 0) {
                    /* "-I foo" , change the next argument */
                    ++i;
                    include_option = "";
                    index_of_first_filename_char = 0;
                } else {
                    /* "-Ifoo", change this argument */
                    include_option = include_options[j];
                    index_of_first_filename_char = strlen(include_option);
                }
                if (argv[i] != NULL) {  /* in case of a dangling -I */
                    if (argv[i][index_of_first_filename_char] == '/') {
                        char *buf;
                        checked_asprintf(&buf, "%s%s%s",
                                 include_option,
                                 root_dir,
                                 argv[i] + index_of_first_filename_char);
                        if (buf == NULL) {
                            return EXIT_OUT_OF_MEMORY;
                        }
                        free(argv[i]);
                        argv[i] = buf;
                    }
                }
                break;  /* from the inner loop; go look at the next argument */
            }
        }
    }
    return 0;
}

/* The -MT command line flag does not work as advertised for distcc:
 * it augments, rather than replace, the list of targets in the dotd file.
 * The behavior we want though, is the replacing behavior.
 * So here we delete the "-MT target" arguments, and we return the target,
 * for use in the .d rewriting in dotd.c.
 */
static int dcc_convert_mt_to_dotd_target(char **argv, char **dotd_target)
{
    int i;
    *dotd_target = NULL;

    for (i = 0; argv[i]; ++i) {
        if (strcmp(argv[i], "-MT") == 0) {
            break;
        }
    }

    /* if we reached the end without finding -MT, fine. */
    if (argv[i] == NULL)
        return 0;

    /* if we find -MT but only at the very end, that's an error. */
    if (argv[i+1] == NULL) {
        rs_trace("found -MT at the end of the command line");
        return 1;
    }

    /* the dotd_target is the argument of -MT */
    *dotd_target = argv[i+1];

    /* copy the next-next argument on top of this. */
    for (; argv[i+2]; ++i) {
        argv[i] = argv[i+2];
    }

    /* and then put the terminal null in. */
    argv[i] = argv[i+2];

    return 0;
}


/**
 * Add -MMD and -MF to get a .d file.
 * Find what the dotd target should be (if any).
 * Prepend @p root_dir to every command
 * line argument that refers to a file/dir by an absolute name.
 **/
static int tweak_arguments_for_server(char **argv,
                                      const char *root_dir,
                                      const char *deps_fname,
                                      char **dotd_target,
                                      char ***tweaked_argv)
{
    int ret;
    *dotd_target = 0;
    if ((ret = dcc_copy_argv(argv, tweaked_argv, 3)))
      return 1;

    if ((ret = dcc_convert_mt_to_dotd_target(*tweaked_argv, dotd_target)))
      return 1;

    if (!dcc_argv_search(*tweaked_argv, "-MD") && !dcc_argv_search(*tweaked_argv, "-MMD")) {
        dcc_argv_append(*tweaked_argv, strdup("-MMD"));
    }
    dcc_argv_append(*tweaked_argv, strdup("-MF"));
    dcc_argv_append(*tweaked_argv, strdup(deps_fname));

    tweak_include_arguments_for_server(*tweaked_argv, root_dir);
    tweak_input_argument_for_server(*tweaked_argv, root_dir);
    return 0;
}


/**
 * Read the client working directory from in_fd socket,
 * and set up the server side directory corresponding to that.
 * Inputs:
 *   @p in_fd: the file descriptor for the socket.
 * Outputs:
 *   @p temp_dir: a temporary directory on the server,
 *                corresponding to the client's root directory (/),
 *   @p client_side_cwd: the current directory on the client
 *   @p server_side_cwd: the corresponding directory on the server;
 *                server_side_cwd = temp_dir + client_side_cwd
 **/
static int make_temp_dir_and_chdir_for_cpp(int in_fd,
        char **temp_dir, char **client_side_cwd, char **server_side_cwd)
{

        int ret = 0;

        if ((ret = dcc_get_new_tmpdir(temp_dir)))
            return ret;
        if ((ret = dcc_r_cwd(in_fd, client_side_cwd)))
            return ret;

        checked_asprintf(server_side_cwd, "%s%s", *temp_dir, *client_side_cwd);
        if (*server_side_cwd == NULL) {
            ret = EXIT_OUT_OF_MEMORY;
        } else if ((ret = dcc_mk_tmp_ancestor_dirs(*server_side_cwd))) {
            ; /* leave ret the way it is */
        } else if ((ret = dcc_mk_tmpdir(*server_side_cwd))) {
            ; /* leave ret the way it is */
        } else if (chdir(*server_side_cwd) == -1) {
            ret = EXIT_IO_ERROR;
        }
        return ret;
}


/**
 * Read a request, run the compiler, and send a response.
 **/
static int dcc_run_job(int in_fd,
                       int out_fd)
{
    char **argv = NULL;
    char **tweaked_argv = NULL;
    int status = 0;
    char *temp_i = NULL, *temp_o = NULL;
    char *err_fname = NULL, *out_fname = NULL, *deps_fname = NULL;
    char *temp_dir = NULL; /* for receiving multiple files */
    int ret = 0, compile_ret = 0;
    char *orig_input = NULL, *orig_output = NULL;
    char *orig_input_tmp, *orig_output_tmp;
    char *dotd_target = NULL;
    pid_t cc_pid;
    enum dcc_protover protover;
    enum dcc_compress compr;
    struct timeval start, end;
    int time_ms;
    char *time_str;
    int job_result = -1;
    enum dcc_cpp_where cpp_where;
    char *server_cwd = NULL;
    char *client_cwd = NULL;
    int changed_directory = 0;

    gettimeofday(&start, NULL);

    if ((ret = dcc_make_tmpnam("distcc", ".deps", &deps_fname)))
        goto out_cleanup;
    if ((ret = dcc_make_tmpnam("distcc", ".stderr", &err_fname)))
        goto out_cleanup;
    if ((ret = dcc_make_tmpnam("distcc", ".stdout", &out_fname)))
        goto out_cleanup;

    dcc_remove_if_exists(deps_fname);
    dcc_remove_if_exists(err_fname);
    dcc_remove_if_exists(out_fname);

    /* Capture any messages relating to this compilation to the same file as
     * compiler errors so that they can all be sent back to the client. */
    dcc_add_log_to_file(err_fname);

    /* Ignore SIGPIPE; we consistently check error codes and will see the
     * EPIPE.  Note that it is set back to the default behaviour when spawning
     * a child, to handle cases like the assembler dying while its being fed
     * from the compiler */
    dcc_ignore_sigpipe(1);

    /* Allow output to accumulate into big packets. */
    tcp_cork_sock(out_fd, 1);

    if ((ret = dcc_r_request_header(in_fd, &protover)))
        goto out_cleanup;

    dcc_get_features_from_protover(protover, &compr, &cpp_where);

    if (cpp_where == DCC_CPP_ON_SERVER) {
        if ((ret = make_temp_dir_and_chdir_for_cpp(in_fd,
                          &temp_dir, &client_cwd, &server_cwd)))
            goto out_cleanup;
        changed_directory = 1;
    }

    if ((ret = dcc_r_argv(in_fd, "ARGC", "ARGV", &argv))
        || (ret = dcc_scan_args(argv, &orig_input_tmp, &orig_output_tmp,
                                &tweaked_argv)))
        goto out_cleanup;

    /* The orig_input_tmp and orig_output_tmp values returned by dcc_scan_args()
     * are aliased with some element of tweaked_argv.  We need to copy them,
     * because the calls to dcc_set_input() and dcc_set_output() below will
     * free those elements. */
    orig_input = strdup(orig_input_tmp);
    orig_output = strdup(orig_output_tmp);
    if (orig_input == NULL || orig_output == NULL) {
      ret = EXIT_OUT_OF_MEMORY;
      goto out_cleanup;
    }

    /* Our new argv is what dcc_scan_args put into tweaked_argv */
    /* Put tweaked_argv into argv, and free old argv */
    dcc_free_argv(argv);
    argv = tweaked_argv;
    tweaked_argv = NULL;

    rs_trace("output file %s", orig_output);
    if ((ret = dcc_make_tmpnam("distccd", ".o", &temp_o)))
        goto out_cleanup;

    /* if the protocol is multi-file, then we need to do the following
     * in a loop.
     */
    if (cpp_where == DCC_CPP_ON_SERVER) {
        if (dcc_r_many_files(in_fd, temp_dir, compr)
            || dcc_set_output(argv, temp_o)
            || tweak_arguments_for_server(argv, temp_dir, deps_fname,
                                          &dotd_target, &tweaked_argv))
            goto out_cleanup;
        /* Repeat the switcharoo trick a few lines above. */
        dcc_free_argv(argv);
        argv = tweaked_argv;
        tweaked_argv = NULL;
    } else {
        if ((ret = dcc_input_tmpnam(orig_input, &temp_i)))
            goto out_cleanup;
        if ((ret = dcc_r_token_file(in_fd, "DOTI", temp_i, compr))
            || (ret = dcc_set_input(argv, temp_i))
            || (ret = dcc_set_output(argv, temp_o)))
            goto out_cleanup;
    }

    if (!dcc_remap_compiler(&argv[0]))
        goto out_cleanup;

    if ((ret = dcc_check_compiler_masq(argv[0])))
        goto out_cleanup;

    if (!opt_enable_tcp_insecure &&
        !getenv("DISTCC_CMDLIST") &&
        dcc_check_compiler_whitelist(argv[0]))
        goto out_cleanup;

    /* unsafe compiler options. See  https://youtu.be/bSkpMdDe4g4?t=53m12s
       on securing https://godbolt.org/ */
    char *a;
    int i;
    for (i = 0; (a = argv[i]); i++)
        if (strncmp(a, "-fplugin=", strlen("-fplugin=")) == 0 ||
            strncmp(a, "-specs=", strlen("-specs=")) == 0) {
            rs_log_warning("-fplugin= and/or -specs= passed, which are insecure and not supported.");
            goto out_cleanup;
    }

    if ((compile_ret = dcc_spawn_child(argv, &cc_pid,
                                       "/dev/null", out_fname, err_fname))
        || (compile_ret = dcc_collect_child("cc", cc_pid, &status, in_fd))) {
        /* We didn't get around to finding a wait status from the actual
         * compiler */
        status = W_EXITCODE(compile_ret, 0);
    }

    if ((ret = dcc_x_result_header(out_fd, protover))
        || (ret = dcc_x_cc_status(out_fd, status))
        || (ret = dcc_x_file(out_fd, err_fname, "SERR", compr, NULL))
        || (ret = dcc_x_file(out_fd, out_fname, "SOUT", compr, NULL))) {
          /* We get a protocol derailment if we send DOTO 0 here */

        if (job_result == -1)
            job_result = STATS_COMPILE_ERROR;
    } else if (WIFSIGNALED(status) || WEXITSTATUS(status)) {
        /* Something went wrong, so send DOTO 0 */
        dcc_x_token_int(out_fd, "DOTO", 0);

        if (job_result == -1)
            job_result = STATS_COMPILE_ERROR;
    } else {
        if (cpp_where == DCC_CPP_ON_SERVER) {
          rs_trace("fixing up debug info");
          /*
           * We update the debugging information, replacing all occurrences
           * of temp_dir (the server temp directory that corresponds to the
           * client's root directory) with "/", to convert server path
           * names to client path names.  This is safe to do only because
           * temp_dir is of the form "/var/tmp/distccd-XXXXXX" where XXXXXX
           * is randomly chosen by mkdtemp(), which makes it inconceivably
           * unlikely that this pattern could occur in the debug info by
           * chance.
           */
          if ((ret = dcc_fix_debug_info(temp_o, "/", temp_dir)))
            goto out_cleanup;
        }
        if ((ret = dcc_x_file(out_fd, temp_o, "DOTO", compr, NULL)))
            goto out_cleanup;

        if (cpp_where == DCC_CPP_ON_SERVER) {
            char *cleaned_dotd;
            ret = dcc_cleanup_dotd(deps_fname,
                                   &cleaned_dotd,
                                   temp_dir,
                                   dotd_target ? dotd_target : orig_output,
                                   temp_o);
            if (ret) goto out_cleanup;
            ret = dcc_x_file(out_fd, cleaned_dotd, "DOTD", compr, NULL);
            free(cleaned_dotd);
        }

        job_result = STATS_COMPILE_OK;
    }

    if (compile_ret == EXIT_IO_ERROR) {
        job_result = STATS_CLI_DISCONN;
    } else if (compile_ret == EXIT_TIMEOUT) {
        job_result = STATS_COMPILE_TIMEOUT;
    }

    dcc_critique_status(status, argv[0], orig_input, dcc_hostdef_local,
                        0);
    tcp_cork_sock(out_fd, 0);

    rs_log(RS_LOG_INFO|RS_LOG_NONAME, "job complete");

out_cleanup:

    /* Restore the working directory, if needed. */
    if (changed_directory) {
      if (chdir(dcc_daemon_wd) != 0) {
        rs_log_warning("chdir(%s) failed: %s", dcc_daemon_wd, strerror(errno));
      }
    }

    switch (ret) {
    case EXIT_BUSY: /* overloaded */
        job_result = STATS_REJ_OVERLOAD;
        break;
    case EXIT_IO_ERROR: /* probably client disconnected */
        job_result = STATS_CLI_DISCONN;
        break;
    case EXIT_PROTOCOL_ERROR:
        job_result = STATS_REJ_BAD_REQ;
        break;
    default:
        if (job_result != STATS_COMPILE_ERROR
            && job_result != STATS_COMPILE_OK
        && job_result != STATS_CLI_DISCONN
        && job_result != STATS_COMPILE_TIMEOUT) {
            job_result = STATS_OTHER;
        }
    }

    gettimeofday(&end, NULL);
    time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;

    dcc_job_summary_append(" ");
    dcc_job_summary_append(stats_text[job_result]);

    if (job_result == STATS_COMPILE_OK) {
        /* special case, also log compiler, file and time */
        dcc_stats_compile_ok(argv[0], orig_input, start, end, time_ms);
    } else {
        dcc_stats_event(job_result);
    }

    checked_asprintf(&time_str, " exit:%d sig:%d core:%d ret:%d time:%dms ",
                     WEXITSTATUS(status), WTERMSIG(status), WCOREDUMP(status),
                     ret, time_ms);
    if (time_str != NULL) dcc_job_summary_append(time_str);
    free(time_str);

    /* append compiler and input file info */
    if (job_result == STATS_COMPILE_ERROR
        || job_result == STATS_COMPILE_OK) {
        dcc_job_summary_append(argv[0]);
        dcc_job_summary_append(" ");
        dcc_job_summary_append(orig_input);
    }

    dcc_remove_log_to_file();
    dcc_cleanup_tempfiles();

    free(orig_input);
    free(orig_output);

    if (argv)
        dcc_free_argv(argv);
    if (tweaked_argv)
        dcc_free_argv(tweaked_argv);

    free(temp_dir);
    free(temp_i);
    free(temp_o);

    free(deps_fname);
    free(err_fname);
    free(out_fname);

    free(client_cwd);
    free(server_cwd);

    return ret;
}
