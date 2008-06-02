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

                /* "A new contraption to capture a dandelion in one
                 * piece has been put together by the crew."
                 *      -- Boards of Canada, "Geogaddi" */


/**
 * @file
 *
 * Bulk file transfer, used for sending .i, .o files etc.
 *
 * Files are always sent in the standard IO format: stream name,
 * length, bytes.  This implies that we can deliver to a fifo (just
 * keep writing), but we can't send from a fifo, because we wouldn't
 * know how many bytes were coming.
 *
 * @note We don't time transmission of files: because the write returns when
 * they've just been written into the OS buffer, we don't really get
 * meaningful numbers except for files that are very large.
 **/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/time.h>

#include "distcc.h"
#include "trace.h"
#include "rpc.h"
#include "bulk.h"
#include "time.h"
#include "exitcode.h"
#include "timeval.h"


/**
 * Open a file for read, and also put its size into @p fsize.
 *
 * If the file does not exist, then returns 0, but @p ifd is -1 and @p
 * fsize is zero.  If @p fsize is zero, the caller should not try to
 * read from the file.
 *
 * This strange behaviour for ENOENT is useful because if there is
 * e.g. no output file from the compiler, we don't want to abort, but
 * rather just send nothing.  The receiver has the corresponding
 * behaviour of not creating zero-length files.
 *
 * Using fstat() helps avoid a race condition -- not a security issue,
 * but possibly a failure.  Shouldn't be very likely though.
 *
 * The caller is responsible for closing @p ifd.
 **/
int dcc_open_read(const char *fname, int *ifd, off_t *fsize)
{
    struct stat buf;

    *ifd = open(fname, O_RDONLY|O_BINARY);
    if (*ifd == -1) {
        int save_errno = errno;
        if (save_errno == ENOENT) {
            /* that's OK, just assume it's empty */
            *fsize = 0;
            return 0;
        } else {
            rs_log_error("failed to open %s: %s", fname, strerror(save_errno));
            return EXIT_IO_ERROR;
        }
    }

    if (fstat(*ifd, &buf) == -1) {
        rs_log_error("fstat %s failed: %s", fname, strerror(errno));
        dcc_close(*ifd);
        return EXIT_IO_ERROR;
    }

    *fsize = buf.st_size;

    return 0;
}

void dcc_calc_rate(off_t size_out,
                   struct timeval *before,
                   struct timeval *after,
                   double *secs,
                   double *rate)
{
    struct timeval delta;

    /* FIXME: Protect against division by zero and other floating-point
     * exceptions. */

    timeval_subtract(&delta, after, before);

    *secs = (double) delta.tv_sec + (double) delta.tv_usec / 1e6;

    if (*secs == 0.0)
        *rate = 0.0;
    else
        *rate = ((double) size_out / *secs) / 1024.0;
}


static int dcc_x_file_lzo1x(int out_fd,
                            int in_fd,
                            const char *token,
                            unsigned in_len)
{
    int ret;
    char *out_buf = NULL;
    size_t out_len;

    /* As a special case, send 0 as 0 */
    if (in_len == 0) {
        if ((ret = dcc_x_token_int(out_fd, token, 0)))
            goto out;
    } else {
        if ((ret = dcc_compress_file_lzo1x(in_fd, in_len, &out_buf, &out_len)))
            goto out;

        if ((ret = dcc_x_token_int(out_fd, token, out_len)))
            goto out;

        if ((ret = dcc_writex(out_fd, out_buf, out_len)))
            goto out;
    }

    ret = 0;

    out:
    free(out_buf);
    return ret;
}


/**
 * Transmit from a local file to the network.  Sends TOKEN, LENGTH, BODY,
 * where the length is the appropriate compressed length.
 *
 * Does compression if needed.
 *
 * @param ofd File descriptor for the network connection.
 * @param fname Name of the file to send.
 * @param token Token for this file, e.g. "DOTO".
 **/
int dcc_x_file(int ofd,
               const char *fname,
               const char *token,
               enum dcc_compress compression,
               off_t *f_size_out)
{
    int ifd;
    int ret;
    off_t f_size;

    if (dcc_open_read(fname, &ifd, &f_size))
        return EXIT_IO_ERROR;
    if (ifd == -1)
        return EXIT_IO_ERROR;
    if (f_size_out)
        *f_size_out = f_size;

    rs_trace("send %lu byte file %s with token %s and compression %d",
             (unsigned long) f_size, fname, token, compression);

    if (compression == DCC_COMPRESS_NONE) {
        if ((ret = dcc_x_token_int(ofd, token, f_size)))
            goto failed;

        /* FIXME: These could get truncated if the file was very large (>4G).
         * That seems pretty unlikely. */
#ifdef HAVE_SENDFILE
        ret = dcc_pump_sendfile(ofd, ifd, (size_t) f_size);
#else
        ret = dcc_pump_readwrite(ofd, ifd, (size_t) f_size);
#endif
    } else if (compression == DCC_COMPRESS_LZO1X) {
        ret = dcc_x_file_lzo1x(ofd, ifd, token, f_size);
    } else {
        rs_log_error("invalid compression");
        return EXIT_PROTOCOL_ERROR;
    }

    if (ifd != -1)
        dcc_close(ifd);
    return 0;

  failed:
    if (ifd != -1)
        dcc_close(ifd);
    return ret;
}


/**
 * Receive a file stream from the network into a local file.
 * Make all necessary directories if they don't exist.
 *
 * Can handle compression.
 *
 * @param len Compressed length of the incoming file.
 * @param filename local filename to create.
 **/
int dcc_r_file(int ifd, const char *filename,
               unsigned len,
               enum dcc_compress compr)
{
    int ofd;
    int ret, close_ret;
    struct stat s;

    /* This is meant to behave similarly to the output routines in bfd/cache.c
     * in gnu binutils, because makefiles or configure scripts may depend on
     * it for edge cases.
     *
     * We try to remove the output file first, if its size is not 0.  That
     * should make the newly created file be owned by the current user; it
     * might also help in the dangerous case of some other process still
     * reading from the file.
     *
     * Checking for size 0 means that we won't unlink special files like
     * /dev/null or fifos.
     *
     * However, failure to remove the file does not cause a warning; we may
     * not have write permission on the directory, but +w for the file.
     */

    if (dcc_mk_tmp_ancestor_dirs(filename)) {
        rs_log_error("failed to create path for '%s'", filename);
        return EXIT_IO_ERROR;
    }

    if (stat(filename, &s) == 0) {
        if (s.st_size != 0) {
            if (unlink(filename) && errno != ENOENT) {
                rs_trace("failed to remove %s: %s", filename, strerror(errno));
                /* continue */
            }
        }
    } else {
        if (errno != ENOENT) {
            rs_trace("stat %s failed: %s", filename, strerror(errno));
        }
        /* continue */
    }

    ofd = open(filename, O_TRUNC|O_WRONLY|O_CREAT|O_BINARY, 0666);
    if (ofd == -1) {
        rs_log_error("failed to create %s: %s", filename, strerror(errno));
        return EXIT_IO_ERROR;
    }

    ret = 0;
    if (len > 0) {
        ret = dcc_r_bulk(ofd, ifd, len, compr);
    }
    close_ret = dcc_close(ofd);

    if (!ret && !close_ret) {
        rs_trace("received %d bytes to file %s", len, filename);
        return 0;
    }

    rs_trace("failed to receive %s, removing it", filename);
    if (unlink(filename)) {
        rs_log_error("failed to unlink %s after failed transfer: %s",
                     filename, strerror(errno));
    }
    return EXIT_IO_ERROR;
}



/**
 * Receive a file and print timing statistics.  Only used for big files.
 *
 * Wrapper around dcc_r_file().
 **/
int dcc_r_file_timed(int ifd, const char *fname, unsigned size,
                     enum dcc_compress compr)
{
    struct timeval before, after;
    int ret;

    if (gettimeofday(&before, NULL))
        rs_log_warning("gettimeofday failed");

    ret = dcc_r_file(ifd, fname, size, compr);

    if (gettimeofday(&after, NULL)) {
        rs_log_warning("gettimeofday failed");
    } else {
        double secs, rate;

        dcc_calc_rate(size, &before, &after, &secs, &rate);
        rs_log_info("%ld bytes received in %.6fs, rate %.0fkB/s",
                    (long) size, secs, rate);
    }

    return ret;
}

int dcc_r_token_file(int in_fd,
                     const char *token,
                     const char *fname,
                     enum dcc_compress compr)
{
    int ret;
    unsigned i_size;

    if ((ret = dcc_r_token_int(in_fd, token, &i_size)))
        return ret;

    if ((ret = dcc_r_file_timed(in_fd, fname, (size_t) i_size, compr)))
        return ret;

    return 0;
}

int dcc_copy_file_to_fd(const char *in_fname, int out_fd)
{
    off_t len;
    int ifd;
    int ret;

    if ((ret = dcc_open_read(in_fname, &ifd, &len)))
        return ret;

#ifdef HAVE_SENDFILE
    ret = dcc_pump_sendfile(out_fd, ifd, (size_t) len);
#else
    ret = dcc_pump_readwrite(out_fd, ifd, (size_t) len);
#endif

    if (ret) {
        close(ifd);
        return ret;
    }
    return 0;
}
