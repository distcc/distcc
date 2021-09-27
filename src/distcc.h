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

/* distcc.h -- common internal-use header file */

#include <sys/types.h>
#include <sys/time.h>     /* for struct timeval */


#ifdef NORETURN
/* nothing */
#elif defined(__GNUC__)
#  define NORETURN __attribute__((noreturn))
#elif defined(__LCLINT__)
#  define NORETURN /*@noreturn@*/ x
#else                           /* !__GNUC__ && !__LCLINT__ */
#  define NORETURN
#endif                          /* !__GNUC__ && !__LCLINT__ */

#ifdef UNUSED
/* nothing */
#elif defined(__GNUC__)
#  define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
#  define UNUSED(x) /*@unused@*/ x
#else                /* !__GNUC__ && !__LCLINT__ */
#  define UNUSED(x) x
#endif                /* !__GNUC__ && !__LCLINT__ */

/* According to the gcc info page, __attribute__((unused)) means "this
 * variable is *possibly* unused" (emphasis added).  So we can use it for
 * POSSIBLY_UNUSED.  This macro is used when a variable is used in one #ifdef
 * case but not another, say.
 */
#ifdef POSSIBLY_UNUSED
/* nothing */
#elif defined(__GNUC__)
#  define POSSIBLY_UNUSED(x) x __attribute__((unused))
#elif defined(__LCLINT__)
#  define POSSIBLY_UNUSED(x) /*@unused@*/ x
#else                /* !__GNUC__ && !__LCLINT__ */
#  define POSSIBLY_UNUSED(x) x
#endif                /* !__GNUC__ && !__LCLINT__ */

#if defined(__GNUC__) && ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
/* This works on Gentoo's (patched?) gcc 3.3.3 but not 3.2.3, and not Debian's
 * 3.3.4.  It should be standard on 3.4. */
#  define WARN_UNUSED  __attribute__((warn_unused_result))
#else
#  define WARN_UNUSED
#endif


#ifndef TRUE
#  define TRUE 1
#  define FALSE 0
#endif


struct dcc_hostdef;



#include "state.h"




enum dcc_compress {
    /* weird values to catch errors */
    DCC_COMPRESS_NONE     = 69,
    DCC_COMPRESS_LZO1X
};

enum dcc_cpp_where {
    /* weird values to catch errors */
    DCC_CPP_ON_CLIENT     = 42,
    DCC_CPP_ON_SERVER
};

enum dcc_protover {
    DCC_VER_1   = 1,            /**< vanilla */
    DCC_VER_2   = 2,            /**< LZO sprinkles */
    DCC_VER_3   = 3             /**< server-side cpp */
};





int str_endswith(const char *tail, const char *tiger);




/* A macro so that we get the right __FUNCTION__ in the trace message.
 *
 * We condition on rs_trace_enabled so that we don't do the to-string
 * conversion unless the user will actually see the result, because it's a
 * little expensive.  */
#define dcc_trace_argv(_message, _argv)         \
    if (rs_trace_enabled()) {                   \
        char *_astr;                            \
        _astr = dcc_argv_tostr(_argv);          \
        rs_trace("%s: %s", _message, _astr);    \
        free(_astr);                            \
    } else {}


/* help.c */
int dcc_trace_version(void);
int dcc_show_version(const char *prog);


/* hosts.c */
int dcc_parse_hosts_env(struct dcc_hostdef **ret_list,
                        int *ret_nhosts);
int dcc_parse_hosts(const char *where, const char *source_name,
                    struct dcc_hostdef **ret_list,
                    int *ret_nhosts, struct dcc_hostdef **ret_prev);

/* ncpu.c */
int dcc_ncpus(int *);

/* ssh.c */
int dcc_ssh_connect(char *ssh_cmd, char *user,
                    char *machine, char *path,
                    int *f_in, int *f_out,
                    pid_t *ssh_pid);

/* safeguard.c */
int dcc_increment_safeguard(void);
int dcc_recursion_safeguard(void);

/* clirpc.c */
int dcc_x_req_header(int fd,
                     enum dcc_protover protover);
int dcc_x_argv(int fd,
               const char *argc_token,
               const char *argv_token,
               char **argv);
int dcc_x_cwd(int fd);
int dcc_is_link(const char *fname, int *is_link);
int dcc_read_link(const char* fname, char *points_to);

/* srvrpc.c */
int dcc_r_cwd(int ifd, char **cwd);

/* remote.c */
int dcc_send_job_corked(int net_fd,
            char **argv,
            pid_t cpp_pid,
                        int *status,
                        const char *,
            const char *cpp_fname,
                        struct dcc_hostdef *);

int dcc_retrieve_results(int net_fd,
                         int *status,
                         const char *output_fname,
                         const char *deps_fname,
                         const char *server_stderr_fname,
                         struct dcc_hostdef *);

/* climasq.c */
int dcc_support_masquerade(char *argv[], char *progname, int *);


/* backoff.c */
int dcc_enjoyed_host(const struct dcc_hostdef *host);
int dcc_disliked_host(const struct dcc_hostdef *host);
int dcc_remove_disliked(struct dcc_hostdef **hostlist);
int dcc_backoff_is_enabled(void);



#define DISTCC_DEFAULT_PORT 3632
#define DISTCC_DEFAULT_STATS_ENABLED 0
#define DISTCC_DEFAULT_STATS_PORT 3633



#ifndef WAIT_ANY
#  define WAIT_ANY (-1)
#endif


/* If --enable-rfc2553 was given, then we will try to enable compile-time IPv6
 * support.  This means we must have a sockaddr_storage large enough to hold
 * IPv6 addresses.  If not, we'll just use a plain sockaddr, which is more
 * likely to compile correctly. */
#ifdef ENABLE_RFC2553
#  ifndef HAVE_SOCKADDR_STORAGE
#    error You can't use RFC2553 because you don't have a sockaddr_storage type
#  endif /* HAVE_SOCKADDR_STORAGE */
#  define dcc_sockaddr_storage sockaddr_storage
#else /* !ENABLE_RFC2553 */
#  define dcc_sockaddr_storage sockaddr
#endif /* !ENABLE_RFC2553 */

#ifndef O_BINARY
#  define O_BINARY 0
#endif


void dcc_set_trace_from_env(void);


/* compress.c */
int dcc_r_bulk_lzo1x(int outf_fd,
                      int in_fd,
                      unsigned in_len);



int dcc_compress_file_lzo1x(int in_fd,
                            size_t in_len,
                            char **out_buf,
                            size_t *out_len);

int dcc_compress_lzo1x_alloc(const char *in_buf,
                            size_t in_len,
                            char **out_buf_ret,
                            size_t *out_len_ret);



/* bulk.c */
void dcc_calc_rate(off_t size_out,
                   struct timeval *before,
                   struct timeval *after,
                   double *secs,
                   double *rate);

/* arg.c */
int dcc_set_action_opt(char **, const char *);
int dcc_set_output(char **, char *);
int dcc_set_input(char **, char *);
int dcc_scan_args(char *argv[], /*@out@*/ /*@relnull@*/ char **orig_o,
                  char **orig_i, char ***ret_newargv);
int dcc_expand_preprocessor_options(char ***argv_ptr);

/* argutil.c */
unsigned int dcc_argv_len(char **a);
int dcc_argv_search(char **a, const char *);
int dcc_argv_startswith(char **a, const char *);
int dcc_copy_argv(char **argv, char ***out_argv, int extra_args);
int dcc_argv_append(char **argv, char *toadd);
char *dcc_argv_tostr(char **a);
void dcc_free_argv(char **argv);

/* tempfile.c */
int dcc_get_tempdir(const char **);
int dcc_make_tmpnam(const char *, const char *suffix, char **);
int dcc_get_new_tmpdir(char **tmpdir);
int dcc_mk_tmpdir(const char *path);
int dcc_mkdir(const char *path);
int dcc_get_subdir(const char *name, char **path_ret) WARN_UNUSED;

int dcc_get_lock_dir(char **path_ret) WARN_UNUSED;
int dcc_get_state_dir(char **path_ret) WARN_UNUSED;
int dcc_get_top_dir(char **path_ret) WARN_UNUSED;
int dcc_get_tmp_top(const char **p_ret) WARN_UNUSED;

int dcc_mk_tmp_ancestor_dirs(const char* file);

/* cleanup.c */
void dcc_cleanup_tempfiles(void);
void dcc_cleanup_tempfiles_from_signal_handler(void);
int dcc_add_cleanup(const char *filename) WARN_UNUSED;

/* strip.c */
int dcc_strip_local_args(char **from, char ***out_argv);
int dcc_strip_dasho(char **from, char ***out_argv);

/* cpp.c */
int dcc_cpp_maybe(char **argv, char *input_fname, char **cpp_fname,
          pid_t *cpp_pid);

/* filename.c */
int dcc_is_source(const char *sfile);
int dcc_is_preprocessed(const char *sfile);
int dcc_is_object(const char *filename);
int dcc_source_needs_local(const char *);

char * dcc_find_extension(char *sfile);
const char * dcc_find_extension_const(const char *sfile);
int dcc_output_from_source(const char *sfile, const char *out_extn,
                           char **ofile);

const char * dcc_preproc_exten(const char *e);
const char * dcc_find_basename(const char *sfile);
void dcc_truncate_to_dirname(char *file);


/* io.c */

int dcc_writex(int fd, const void *buf, size_t len);

int dcc_r_token(int ifd, char *token);

int dcc_readx(int fd, void *buf, size_t len);
int dcc_pump_sendfile(int ofd, int ifd, size_t n);
int dcc_r_str_alloc(int fd, unsigned len, char **buf);

int tcp_cork_sock(int fd, int corked);
int dcc_close(int fd);
int dcc_get_io_timeout(void);
int dcc_want_mmap(void);


int dcc_select_for_write(int fd, int timeout);
int dcc_select_for_read(int fd, int timeout);

/* loadfile.c */
int dcc_load_file_string(const char *filename,
                         char **retbuf);


extern const int dcc_connect_timeout;


/* pump.c */
int dcc_r_bulk(int ofd,
               int ifd,
               unsigned f_size,
               enum dcc_compress compression);

int dcc_pump_readwrite(int ofd, int ifd, size_t n);

/* mapfile.c */
int dcc_map_input_file(int in_fd, off_t in_size, char **buf_ret);

/* XXX: Kind of kludgy, we should do dynamic allocation.  But this will do for
 * now. */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif


#ifndef WCOREDUMP
#  define WCOREDUMP(status) 0
#endif
