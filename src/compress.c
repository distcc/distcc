/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003, 2004 by Martin Pool <mbp@sourcefrog.net>
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


                        /* I think that I can safely speak for the
                         * whole troll community when I say "I like
                         * watching train wrecks".  -- AC  */


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_MMAN_H
#  include <sys/mman.h>
#endif

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"
#include "minilzo.h"


static char work_mem[LZO1X_1_MEM_COMPRESS];

/**
 * @file
 *
 * Compressed bulk data transfer for distcc.
 *
 * lzo doesn't have any detectable magic at the start in the raw form.  (lzop
 * the command-line tool adds some.)  Therefore we indicate in the request
 * header (the protocol version) whether compression is on or off.  If it is
 * on, all bulk data in both directions is compressed.  metadata whether the
 * transfer is compressed or not.
 *
 * It might be nice to unify this code with that in pump.c, which deals with
 * uncompressed files.  There are some parallels between the routines.
 * However the details are rather different, because with compressed files we
 * do not know ahead of time how big the expanded form will be.  This affects
 * sending, where we need to make a large-enough temporary buffer to compress
 * into.  It also affects receipt, where we need to allow extra space for data
 * coming in.  So for the moment they remain separate.
 *
 * We used to use mmap here, but it complicated the code (and caused a bug in
 * 2.14) without being clearly any faster.  So it's out again.
 *
 * The chunk header gives the number of compressed bytes.  The number of
 * plaintext bytes isn't transmitted, and so for decompression we might need
 * to scale up the buffer.
 */


/*
 * Compress from a file to a newly malloc'd block.
 */
int dcc_compress_file_lzo1x(int in_fd,
                            size_t in_len,
                            char **out_buf,
                            size_t *out_len)
{
    char *in_buf = NULL;
    int ret;

    if ((in_buf = malloc(in_len)) == NULL) {
        rs_log_error("allocation of %ld byte buffer failed",
                     (long) in_len);
        ret = EXIT_OUT_OF_MEMORY;
        goto out;
    }

    if ((ret = dcc_readx(in_fd, in_buf, in_len)))
        goto out;

    if ((ret = dcc_compress_lzo1x_alloc(in_buf, in_len, out_buf, out_len)))
        goto out;

    out:
    if (in_buf != NULL) {
        free(in_buf);
    }

    return ret;
}


/**
 * Send LZO-compressed bulk data.
 *
 * The most straightforward method for miniLZO is to just send everything in
 * one big chunk.  So we just read the whole input into a buffer, build the
 * output in a buffer, and send it once its complete.
 **/
int dcc_compress_lzo1x_alloc(const char *in_buf,
                             size_t in_len,
                             char **out_buf_ret,
                             size_t *out_len_ret)
{
    int ret = 0, lzo_ret;
    char *out_buf = NULL;
    size_t out_size;
    lzo_uint out_len;

    /* NOTE: out_size is the buffer size, out_len is the amount of actual
     * data. */

    /* In the unlikely worst case, LZO can cause the input to expand a bit. */
    out_size = in_len + in_len/64 + 16 + 3;
    if ((out_buf = malloc(out_size)) == NULL) {
        rs_log_error("failed to allocate compression buffer");
        return EXIT_OUT_OF_MEMORY;
    }

    out_len = out_size;
    lzo_ret = lzo1x_1_compress((lzo_byte*)in_buf, in_len,
                               (lzo_byte*)out_buf, &out_len,
                               work_mem);
    if (lzo_ret != LZO_E_OK) {
        rs_log_error("LZO1X1 compression failed: %d", lzo_ret);
        free(out_buf);
        return EXIT_IO_ERROR;
    }

    *out_buf_ret = out_buf;
    *out_len_ret = out_len;

    rs_trace("compressed %ld bytes to %ld bytes: %d%%",
             (long) in_len, (long) out_len,
             (int) (in_len ? 100*out_len / in_len : 0));

    return ret;
}



/**
 * Receive @p in_len compressed bytes from @p in_fd, and write the
 * decompressed form to @p out_fd.
 *
 * There's no way for us to know how big the uncompressed form will be, and
 * there is also no way to grow the decompression buffer if it turns out to
 * initially be too small.  So we assume a ratio of 10x.  If it turns out to
 * be too small, we increase the buffer and try again.  Typical compression of
 * source or object is about 2x to 4x.  On modern Unix we should be able to
 * allocate (and not touch) many megabytes at little cost, since it will just
 * turn into an anonymous map.
 *
 * LZO doesn't have any way to decompress part of the input and then break to
 * get more output space, so our buffer needs to be big enough in the first
 * place or we would waste time repeatedly decompressing it.
 **/
int dcc_r_bulk_lzo1x(int out_fd, int in_fd,
                     unsigned in_len)
{
    int ret, lzo_ret;
    char *in_buf = NULL, *out_buf = NULL;
    size_t out_size = 0;
    lzo_uint out_len;

    /* NOTE: out_size is the buffer size, out_len is the amount of actual
     * data. */

    if (in_len == 0)
        return 0;               /* just check */

    if ((in_buf = malloc(in_len)) == NULL) {
        rs_log_error("failed to allocate decompression input");
        ret = EXIT_OUT_OF_MEMORY;
        goto out;
    }

    if ((ret = dcc_readx(in_fd, in_buf, in_len)) != 0)
        goto out;

#if 0
    /* Initial estimate for output buffer.  This is intentionally quite low to
     * exercise the resizing code -- if it works OK then we can scale this
     * up. */
    out_size = 2 * in_len;
#else
    out_size = 8 * in_len;
#endif

    try_again_with_a_bigger_buffer:

    if ((out_buf = malloc(out_size)) == NULL) {
        rs_log_error("failed to allocate decompression buffer");
        ret = EXIT_OUT_OF_MEMORY;
        goto out;
    }

    out_len = out_size;
    lzo_ret = lzo1x_decompress_safe((lzo_byte*)in_buf, in_len,
                                    (lzo_byte*)out_buf, &out_len, work_mem);

    if (lzo_ret == LZO_E_OK) {
        rs_trace("decompressed %ld bytes to %ld bytes: %d%%",
                 (long) in_len, (long) out_len,
                 (int) (out_len ? 100*in_len / out_len : 0));

        ret = dcc_writex(out_fd, out_buf, out_len);

        goto out;
    } else if (lzo_ret == LZO_E_OUTPUT_OVERRUN) {
        free(out_buf);
        out_buf = 0;
        out_size *= 2;
        /* FIXME: Make sure this doesn't overflow memory size? */
        rs_trace("LZO_E_OUTPUT_OVERRUN, trying again with %lu byte buffer",
                 (unsigned long) out_size);
        goto try_again_with_a_bigger_buffer;
    } else {
        rs_log_error("LZO1X1 decompression failed: %d", lzo_ret);
        ret = EXIT_IO_ERROR;
        goto out;
    }

out:
    free(in_buf);
    free(out_buf);

    return ret;
}
