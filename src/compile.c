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



#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <ctype.h>

#ifdef HAVE_FNMATCH_H
  #include <fnmatch.h>
#endif

#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#include "distcc.h"
#include "trace.h"
#include "exitcode.h"
#include "util.h"
#include "hosts.h"
#include "bulk.h"
#include "implicit.h"
#include "exec.h"
#include "where.h"
#include "lock.h"
#include "timeval.h"
#include "compile.h"
#include "include_server_if.h"
#include "emaillog.h"
#include "dotd.h"


static const int max_discrepancies_before_demotion = 1;

static const char *const include_server_port_suffix = "/socket";
static const char *const discrepancy_suffix = "/discrepancy_counter";

/**
 * Return in @param filename the name of the file we use as unary counter of
 * discrepancies (a compilation failing on the server, but succeeding
 * locally. This function may return NULL in @param filename if the name cannot
 * be determined.
 **/ 
int ddc_discrepancy_filename(char **filename) 
{
    const char *include_server_port = getenv("INCLUDE_SERVER_PORT");
    *filename = NULL;
    if (include_server_port == NULL) {
        return 0;
    } else if (str_endswith(include_server_port_suffix,
                            include_server_port)) {
        /* We're going to make a longer string from include_server_port: one
         * that replaces include_server_port_suffix with discrepancy_suffix. */
        int delta = strlen(discrepancy_suffix) - 
            strlen(include_server_port_suffix);
        assert (delta > 0);
        *filename = malloc(strlen(include_server_port) + delta);
        if (!*filename) {
            rs_log_error("failed to allocate space for filename");
            return EXIT_OUT_OF_MEMORY;
        }
        strcpy(*filename, include_server_port);
        int slash_pos = strlen(include_server_port)
                        - strlen(include_server_port_suffix);
        /* Because include_server_port_suffix is a suffix of include_server_port
         * we expect to find a '/' at slash_pos in filename. */
        assert((*filename)[slash_pos] == '/');
        strcpy(*filename + slash_pos, discrepancy_suffix);
        return 0;
    } else 
        return 0;
}


/**
 * Return the length of the @param discrepancy_filename in newly allocated
 * memory; return 0 if it's not possible to determine the length (if
 * e.g. @param discrepancy_filename is NULL).
 **/
static int dcc_read_number_discrepancies(const char *discrepancy_filename) 
{
    if (!discrepancy_filename) return 0;
    struct stat stat_record;
    if (stat(discrepancy_filename, &stat_record) == 0) {
        size_t size = stat_record.st_size;
        /* Does size fit in an 'int'? */
        if ((((size_t) (int) size) == size) && 
            ((int) size) > 0)
            return ((int) size);
        else 
            return INT_MAX;
    } else 
        return 0;
}


/**
 *  Lengthen the file whose name is @param discrepancy_filename by one byte. Or,
 *  do nothing, if @param discrepancy_filename is NULL.
 **/
static int dcc_note_discrepancy(const char *discrepancy_filename) 
{
    FILE *discrepancy_file;
    if (!discrepancy_filename) return 0;
    if (!(discrepancy_file = fopen(discrepancy_filename, "a"))) {
        rs_log_error("failed to open discrepancy_filename file: %s: %s",
                     discrepancy_filename,
                     strerror(errno));
        return EXIT_IO_ERROR;
    }
    if (fputc('@', discrepancy_file) == EOF) {
        rs_log_error("failed to write to discrepancy_filename file: %s",
                     discrepancy_filename);
        fclose(discrepancy_file);
        return EXIT_IO_ERROR;
    }
    if (dcc_read_number_discrepancies(discrepancy_filename) ==
        max_discrepancies_before_demotion) {
        /* Give up on using distcc-pump. Print this warning just once. */
        rs_log_warning("now using plain distcc, possibly due to "
                       "inconsistent file system changes during build");
    }
    fclose(discrepancy_file);
    return 0;
}


/**
 * In some cases, it is ill-advised to preprocess on the server. Check for such
 * situations. If they occur, then change protocol version.
 **/
static void dcc_perhaps_adjust_cpp_where_and_protover(
    char *input_fname,
    struct dcc_hostdef *host,
    char *discrepancy_filename)
{
    /* It's unfortunate that the variable that controls preprocessing is in the
       "host" datastructure. See elaborate complaint in dcc_build_somewhere. */

    /* Check whether there has been too much trouble running distcc-pump during
       this build. */
    if (dcc_read_number_discrepancies(discrepancy_filename) >=
        max_discrepancies_before_demotion) {
        /* Give up on using distcc-pump */
        host->cpp_where = DCC_CPP_ON_CLIENT;
        dcc_get_protover_from_features(host->compr, 
                                       host->cpp_where, 
                                       &host->protover);
    }

    /* Don't do anything silly for already preprocessed files. */
    if (dcc_is_preprocessed(input_fname)) {
        /* Don't subject input file to include analysis. */
        rs_log_warning("cannot use distcc_pump on already preprocessed file"
                       " (such as emitted by ccache)");
        host->cpp_where = DCC_CPP_ON_CLIENT;
        dcc_get_protover_from_features(host->compr, 
                                       host->cpp_where, 
                                       &host->protover);
    }
    /* Environment variables CPATH and two friends are hidden ways of passing
     * -I's. Beware! */
    if (getenv("CPATH") || getenv("C_INCLUDE_PATH") 
        || getenv("CPLUS_INCLUDE_PATH")) {
        rs_log_warning("cannot use distcc_pump with any of environment"
                       " variables CPATH, C_INCLUDE_PATH or CPLUS_INCLUDE_PATH"
                       " set, preprocessing locally");
        host->cpp_where = DCC_CPP_ON_CLIENT;
        dcc_get_protover_from_features(host->compr, 
                                       host->cpp_where, 
                                       &host->protover);
    }
}



/**
 * Do a time analysis of dependencies in dotd file.  First, if @param dotd_fname
 * is created before @param reference_time, then return NULL in result.  Second,
 * if one of the files mentioned in the @param dotd_fname is modified after time
 * @param reference_time, then return non-NULL in result. Otherwise return NULL
 * in result.  A non-NULL value in result is a pointer to a newly allocated
 * string describing the offending dependency.

 * If @param exclude_pattern is not NULL, then files matching the glob @param
 * exclude_pattern are not considered in the above comparison. 
 *
 *  This function is not declared static --- for purposes of testing.
 **/
int dcc_fresh_dependency_exists(const char *dotd_fname, 
                                const char *exclude_pattern,
                                time_t reference_time,
                                char **result)
{
    struct stat stat_dotd;
    off_t dotd_fname_size = 0;
    FILE *fp;
    char c;
    int res;
    char *dep_name;

    *result = NULL;
    /* Allocate buffer for dotd contents and open it. */
    res = stat(dotd_fname, &stat_dotd);
    if (res) {
        rs_trace("could not stat \"%s\": %s", dotd_fname, strerror(errno));
        return 0;
    }
    if (stat_dotd.st_mtime < reference_time) {
        /* That .d file appears to be too old; don't trust it for this
         * analysis. */
        rs_trace("old dotd file \"%s\"", dotd_fname);
        return 0;
    }
    dotd_fname_size = stat_dotd.st_size;
    /* Is dotd_fname_size representable as a size_t value ? */
    if ((off_t) (size_t) dotd_fname_size == dotd_fname_size) {
        dep_name = malloc((size_t) dotd_fname_size);
        if (!dep_name) {
            rs_log_error("failed to allocate space for dotd file");
            return EXIT_OUT_OF_MEMORY;
        }
    } else { /* This is exceedingly unlikely. */
        rs_trace("file \"%s\" is too big", dotd_fname);
        return 0;
    }
    if ((fp = fopen(dotd_fname, "r")) == NULL) {
        rs_trace("could not open \"%s\": %s", dotd_fname, strerror(errno));
        free(dep_name);
        return 0;
    }
    
    /* Find ':'. */
    while ((c = getc(fp)) != EOF && c != ':');
    if (c != ':') goto return_0;

    /* Process dependencies. */
    while (c != EOF) {
        struct stat stat_dep;
        int i = 0;
        /* Skip whitespaces and backslashes. */
        while ((c = getc(fp)) != EOF && (isspace(c) || c == '\\'));
        /* Now, we're at start of file name. */
        ungetc(c, fp);
        while ((c = getc(fp)) != EOF && 
               (!isspace(c) || c == '\\')) {
            if (i >= dotd_fname_size) {
                /* Impossible */
                rs_log_error("not enough room for dependency name");
                goto return_0;
            }
            if (c == '\\') {
                /* Skip the newline. */
                if ((c = getc(fp)) != EOF)
                    if (c != '\n') ungetc(c, fp);
            }
            else dep_name[i++] = c;
        }
        if (i != 0) {
            dep_name[i] = '\0';
#ifdef HAVE_FNMATCH_H
            if (exclude_pattern == NULL ||
                fnmatch(exclude_pattern, dep_name, 0) == FNM_NOMATCH) {
#else
            /* Tautology avoids compiler warning about unused variable. */
            if (exclude_pattern == exclude_pattern) {
#endif
                /* The dep_name is not excluded; now verify that it is not too
                 * young. */
                rs_log_info("Checking dependency: %s", dep_name);
                res = stat(dep_name, &stat_dep);
                if (res) goto return_0;
                if (stat_dep.st_ctime >= reference_time) {
                    fclose(fp);
                    *result = realloc(dep_name, strlen(dep_name) + 1);
                    if (*result == NULL) {
                        rs_log_error("realloc failed");
                        return EXIT_OUT_OF_MEMORY;
                    }
                    return 0;
                }
            }
        }
    }
  return_0: 
    fclose(fp);
    free(dep_name);
    return 0;
}


/**
 * Invoke a compiler locally.  This is, obviously, the alternative to
 * dcc_compile_remote().
 *
 * The server does basically the same thing, but it doesn't call this
 * routine because it wants to overlap execution of the compiler with
 * copying the input from the network.
 *
 * This routine used to exec() the compiler in place of distcc.  That
 * is slightly more efficient, because it avoids the need to create,
 * schedule, etc another process.  The problem is that in that case we
 * can't clean up our temporary files, and (not so important) we can't
 * log our resource usage.
 *
 * This is called with a lock on localhost already held.
 **/
static int dcc_compile_local(char *argv[],
                             char *input_name)
{
    pid_t pid;
    int ret;
    int status;

    dcc_note_execution(dcc_hostdef_local, argv);
    dcc_note_state(DCC_PHASE_COMPILE, input_name, "localhost");

    /* We don't do any redirection of file descriptors when running locally,
     * so if for example cpp is being used in a pipeline we should be fine. */
    if ((ret = dcc_spawn_child(argv, &pid, NULL, NULL, NULL)) != 0)
        return ret;

    if ((ret = dcc_collect_child("cc", pid, &status, timeout_null_fd)))
        return ret;

    return dcc_critique_status(status, "compile", input_name,
                               dcc_hostdef_local, 1);
}


 /* Make the decision to send email about @param input_name, but only after a
  * little further investgation.
  *
  * We avoid sending email if there's a fresh dependency. To find out, we need
  * @param deps_fname, a .d file, created during the build.  We check each
  * dependency described there. If just one changed after the build started,
  * then we really don't want to hear about distcc-pump errors, because
  * dependencies shouldn't change. The files generated during the build are
  * exceptions. To disregard these, the distcc user may specify a glob pattern
  * in environment variable DISTCC_EXCLUDE_FRESH_FILES.
  *
  * Also, if there has been too many discrepancies (where the build has
  * succeeded remotely but failed locally), then we need to stop using
  * distcc-pump for the remainder of the build.  The present function
  * contributes to this logic: if it is determined that email must be sent, then
  * the count of such situations is incremented using the file @param
  * discrepancy_filename.
 */
static int dcc_please_send_email_after_investigation(
    const char *input_fname,
    const char *deps_fname,
    const char *discrepancy_filename) {

    int ret;
    char *fresh_dependency;
    const char *include_server_port = getenv("INCLUDE_SERVER_PORT");
    struct stat stat_port;
    rs_log_warning("remote compilation of '%s' failed, retried locally "
                   "and got a different result.", input_fname);
    if ((include_server_port != NULL) &&
        (stat(include_server_port, &stat_port)) == 0) {
        time_t build_start = stat_port.st_ctime;
        if (deps_fname) {
            const char *exclude_pattern =
                getenv("DISTCC_EXCLUDE_FRESH_FILES");

            if ((ret = dcc_fresh_dependency_exists(deps_fname,
                                                   exclude_pattern,
                                                   build_start,
                                                   &fresh_dependency))) {
                return ret;
            }
            if (fresh_dependency) {
                rs_log_warning("file '%s', a dependency of %s, "
                               "changed during the build", fresh_dependency,
                               input_fname);
                free(fresh_dependency);
                return dcc_note_discrepancy(discrepancy_filename);
            }
        }
    }
    dcc_please_send_email();
    return dcc_note_discrepancy(discrepancy_filename);
}

/**
 * Execute the commands in argv remotely or locally as appropriate.
 *
 * We may need to run cpp locally; we can do that in the background
 * while trying to open a remote connection.
 *
 * This function is slightly inefficient when it falls back to running
 * gcc locally, because cpp may be run twice.  Perhaps we could adjust
 * the command line to pass in the .i file.  On the other hand, if
 * something has gone wrong, we should probably take the most
 * conservative course and run the command unaltered.  It should not
 * be a big performance problem because this should occur only rarely.
 *
 * @param argv Command to execute.  Does not include 0='distcc'.
 * Treated as read-only, because it is a pointer to the program's real
 * argv.
 *
 * @param status On return, contains the waitstatus of the compiler or
 * preprocessor.  This function can succeed (in running the compiler) even if
 * the compiler itself fails.  If either the compiler or preprocessor fails,
 * @p status is guaranteed to hold a failure value.
 **/
static int
dcc_build_somewhere(char *argv[],
                    int sg_level,
                    int *status)
{
    char *input_fname = NULL, *output_fname, *cpp_fname, *deps_fname = NULL;
    char **files;
    char **server_side_argv = NULL;
    char *server_stderr_fname = NULL;
    int needs_dotd = 0;
    int sets_dotd_target = 0;
    pid_t cpp_pid = 0;
    int cpu_lock_fd, local_cpu_lock_fd;
    int ret;
    int remote_ret = 0;
    struct dcc_hostdef *host = NULL;
    char *discrepancy_filename;

    if ((ret = ddc_discrepancy_filename(&discrepancy_filename)))
        return ret;

    if (sg_level)
        goto run_local;

    /* TODO: Perhaps tidy up these gotos. */

    if (dcc_scan_args(argv, &input_fname, &output_fname, &argv) != 0) {
        /* we need to scan the arguments even if we already know it's
         * local, so that we can pick up distcc client options. */
        goto lock_local;
    }
#if 0
    /* turned off because we never spend long in this state. */
    dcc_note_state(DCC_PHASE_STARTUP, input_fname, NULL);
#endif
    if ((ret = dcc_make_tmpnam("distcc_server_stderr", ".txt",
                               &server_stderr_fname))) {
        // So we are failing locally to make a temp file to store the 
        // server-side errors in; it's unlikely anything else will
        // work, but let's try the compilation locally.
        // FIXME: this will blame the server for a failure that is
        // local. However, we don't make any distrinction between
        // all the reasons dcc_compile_remote can fail either;
        // and some of those reasons are local.
        goto fallback;
    }
	
    if ((ret = dcc_lock_local_cpp(&local_cpu_lock_fd)) != 0) {
        goto fallback;
    }
    
    if ((ret = dcc_pick_host_from_list(&host, &cpu_lock_fd)) != 0) {
        /* Doesn't happen at the moment: all failures are masked by
           returning localhost. */
        goto fallback;
    }

    if (host->mode == DCC_MODE_LOCAL)
        /* We picked localhost and already have a lock on it so no
         * need to lock it now. */
        goto run_local;

    if (host->cpp_where == DCC_CPP_ON_SERVER) {
        /* Perhaps it is not a good idea to preprocess on the server. */
        dcc_perhaps_adjust_cpp_where_and_protover(input_fname, host,
                                                  discrepancy_filename);
    }
    if (host->cpp_where == DCC_CPP_ON_SERVER) {
    	if ((ret = dcc_talk_to_include_server(argv, &files))) {
            /* Fallback to doing cpp locally */
            /* It's unfortunate that the variable that controls that is in the
             * "host" datastructure, even though in this case it's the client
             * that fails to support it,  but "host" is what gets passed
             * around in the client code. We are, in essense, throwing away
             * the host's capability to do cpp, so if this code was to execute
             * again (it won't, not in the same process) we wouldn't know if
             * the server supports it or not.
             */
            rs_log_warning("failed to get includes from include server, "
                           "preprocessing locally");
            if (dcc_getenv_bool("DISTCC_TESTING_INCLUDE_SERVER", 0))
                dcc_exit(ret);
    	    host->cpp_where = DCC_CPP_ON_CLIENT;
            dcc_get_protover_from_features(host->compr, 
                                           host->cpp_where, 
                                           &host->protover);
    	} else {
            // done "preprocessing"
            dcc_unlock(local_cpu_lock_fd);
            // don't try to unlock again in dcc_compile_remote
            local_cpu_lock_fd = 0; 
        }
    }

    if (host->cpp_where == DCC_CPP_ON_CLIENT) {
    	files = NULL;
    	
        if ((ret = dcc_cpp_maybe(argv, input_fname, &cpp_fname, &cpp_pid) != 0))
            goto fallback;
    
        if ((ret = dcc_strip_local_args(argv, &server_side_argv)))
            goto fallback;
    } else {
        char *dotd_target = NULL;
    	cpp_fname = NULL;
    	cpp_pid = 0;
        dcc_get_dotd_info(argv, &deps_fname, &needs_dotd, 
                          &sets_dotd_target, &dotd_target);
    	if ((ret = dcc_copy_argv(argv, &server_side_argv, 2)))
    	    goto fallback;
        if (needs_dotd && !sets_dotd_target) {
           dcc_argv_append(server_side_argv, strdup("-MT"));
           if (dotd_target == NULL)
               dcc_argv_append(server_side_argv, strdup(output_fname));
           else 
               dcc_argv_append(server_side_argv, strdup(dotd_target));
        }
    }
    if ((ret = dcc_compile_remote(server_side_argv,
                                  input_fname,
                                  cpp_fname,
                                  files,
                                  output_fname,
                                  needs_dotd ? deps_fname : NULL,
                                  server_stderr_fname,
                                  cpp_pid, local_cpu_lock_fd, 
				  host, status)) != 0) {
        /* Returns zero if we successfully ran the compiler, even if
         * the compiler itself bombed out. */
        goto fallback;
    }

    dcc_enjoyed_host(host);

    dcc_unlock(cpu_lock_fd);

    ret = dcc_critique_status(*status, "compile", input_fname, host, 1);
    if (ret == 0) {
        // Try to copy the server-side errors on stderr.
        // If that fails, even though the compilation succeeded,
        // we haven't managed to give these errors to the user,
        // so we have to try again.
        // FIXME: Just like in the attempt to make a temporary file, this 
        // is unlikely to fail, if it does it's unlikely any other
        // operation will work, and this makes the mistake of
        // blaming the server for what is (clearly?) a local failure.
        if ((dcc_copy_file_to_fd(server_stderr_fname, STDERR_FILENO))) {
            rs_log_warning("Could not show server-side errors");
            goto fallback;
        }
        goto clean_up;
    }
    if (ret < 128) {
        /* Remote compile just failed, e.g. with syntax error. 
           It may be that the remote compilation failed because
           the file has an error, or because we did something
           wrong (e.g. we did not send all the necessary files.)
           Retry locally. If the local compilation also fails, 
           then we know it's the program that has the error,
           and it doesn't really matter that we recompile, because
           this is rare.
           If the local compilation succeeds, then we know it's our 
           fault, and we should do something about it later.
           (Currently, we send email to an appropriate email address).
        */
        rs_log_warning("remote compilation of '%s' failed, retrying locally", 
                       input_fname);
        remote_ret = ret;
        goto fallback;
    }


  fallback:
    if (host)
        dcc_disliked_host(host);

    if (!dcc_getenv_bool("DISTCC_FALLBACK", 1)) {
        rs_log_warning("failed to distribute and fallbacks are disabled");
        // Try copying any server-side error message to stderr;
        // If we fail the user will miss all the messages from the server; so 
        // we pretend we failed remotely.
        if ((dcc_copy_file_to_fd(server_stderr_fname, STDERR_FILENO))) {
            rs_log_error("Could not print error messages from '%s'", 
                         server_stderr_fname);
        }
        goto clean_up;
    }

    // At this point, we can abandon the remote errors.

    /* "You guys are so lazy!  Do I have to do all the work myself??" */
    if (host) {
        rs_log(RS_LOG_WARNING|RS_LOG_NONAME,
               "failed to distribute %s to %s, running locally instead",
               input_fname ? input_fname : "(unknown)",
               host->hostdef_string);
    } else {
        rs_log_warning("failed to distribute, running locally instead");
    }

  lock_local:
    dcc_lock_local(&cpu_lock_fd);

  run_local:
    /* Either compile locally, after remote failure, or simply do other cc tasks
       as assembling, linking, etc. */
    ret = dcc_compile_local(argv, input_fname);
    if (remote_ret != 0 && remote_ret != ret) {
        /* Oops! it seems what we did remotely is not the same as what we did
          locally. We normally send email in such situations (if emailing is
          enabled), but we attempt an a time analysis of source files in order
          to avoid doing so in case source files we changed during the build.
        */
        (void) dcc_please_send_email_after_investigation(
            input_fname,
            deps_fname,
            discrepancy_filename);
    }

  clean_up:
    free(server_side_argv);
    free(discrepancy_filename);
    return ret;
}


int dcc_build_somewhere_timed(char *argv[],
                              int sg_level,
                              int *status)
{
    struct timeval before, after, delta;
    int ret;

    if (gettimeofday(&before, NULL))
        rs_log_warning("gettimeofday failed");

    ret = dcc_build_somewhere(argv, sg_level, status);

    if (gettimeofday(&after, NULL)) {
        rs_log_warning("gettimeofday failed");
    } else {
        /* TODO: Show rate based on cpp size?  Is that meaningful? */
        timeval_subtract(&delta, &after, &before);

        rs_log(RS_LOG_INFO|RS_LOG_NONAME,
               "elapsed compilation time %ld.%06lds",
               delta.tv_sec, (long) delta.tv_usec);
    }

    return ret;
}
