/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2002, 2003, 2004 by Martin Pool
 * Copyright 2008 Google Inc.
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


                        /* "I've always wanted to use sendfile(), but
                         * never had a reason until now"
                         *              -- mbp                        */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>
#ifdef HAVE_SYS_SENDFILE_H
#  include <sys/sendfile.h>
#endif /* !HAVE_SYS_SENDFILE_H */
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "distcc.h"
#include "trace.h"
#include "util.h"
#include "exitcode.h"



/*
 * Could also use sendfilev() on Solaris >= 8:
 *
 * http://docs.sun.com/db/doc/816-0217/6m6nhtaps?a=view
 */


#ifdef HAVE_SENDFILE
/* If you don't have it, just use dcc_pump_readwrite */

/**
 * sys_sendfile maps all the different implementations of sendfile() into
 * something like the Linux interface.
 *
 * Our sockets are never non-blocking, so that seems to me to say that
 * the kernel will never return EAGAIN -- we will always either send
 * the whole thing or get an error.  Is that really true?
 *
 * How nice to have the function parameters reversed between platforms
 * in a way that will not give a compiler warning.
 *
 * @param offset offset in input to start writing; updated on return
 * to reflect the number of bytes sent.
 *
 * sys_sendfile returns the number of bytes sent, if transmission succeeded.
 * If there was an error, it returns -1 with errno set.  It should never
 * return 0.
 **/


#if defined(__FreeBSD__)
static ssize_t sys_sendfile(int ofd, int ifd, off_t *offset, size_t size)
{
    off_t sent_bytes;
    int ret;

    /* According to the manual, this can never partially complete on a
     * socket open for blocking IO. */
    ret = sendfile(ifd, ofd, *offset, size, 0, &sent_bytes, 0);
    if (ret == -1) {
        /* http://cvs.apache.org/viewcvs.cgi/apr/network_io/unix/sendrecv.c?rev=1.95&content-type=text/vnd.viewcvs-markup */
        if (errno == EAGAIN) {
            if (sent_bytes == 0) {
                /* Didn't send anything. Return error with errno == EAGAIN. */
                return -1;
            } else {
                /* We sent some bytes, but they we would block.  Treat this as
                 * success for now. */
                *offset += sent_bytes;
                return sent_bytes;
            }
        } else {
            /* some other error */
            return -1;
        }
    } else if (ret == 0) {
        *offset += size;
        return size;
    } else {
        rs_log_error("don't know how to handle return %d from BSD sendfile",
                     ret);
        return -1;
    }
}
#elif defined(linux) || defined(__GNU__)
static ssize_t sys_sendfile(int ofd, int ifd, off_t *offset, size_t size)
{
    return sendfile(ofd, ifd, offset, size);
}
#elif defined(__hpux) || defined(__hpux__)
/* HP cc in ANSI mode defines __hpux; gcc defines __hpux__ */
static ssize_t sys_sendfile(int ofd, int ifd, off_t *offset, size_t size)
{
    ssize_t ret;

    ret = sendfile(ofd, ifd, *offset, size, NULL, 0);
    if (ret == -1) {
        return -1;
    } else if (ret > 0) {
        *offset += ret;
        return ret;
    } else {
        rs_log_error("don't know how to handle return %ld from HP-UX sendfile",
                     (long) ret);
        return -1;
    }
}
#elif defined(__MACH__) && defined(__APPLE__)
static ssize_t sys_sendfile(int ofd, int ifd, off_t *offset, size_t size)
{
    off_t sent_bytes = size;
    int ret;

    ret = sendfile(ofd, ifd, *offset, &sent_bytes, NULL, 0);
    if (ret == -1) {
        if (errno == EAGAIN) {
            if (sent_bytes == 0) {
                /* Didn't send anything. Return error with errno == EAGAIN. */
                return -1;
            } else {
                /* We sent some bytes, but they we would block.  Treat this as
                 * success for now. */
                *offset += sent_bytes;
                return sent_bytes;
            }
        } else {
            /* some other error */
            return -1;
        }
    } else if (ret == 0) {
        *offset += size;
        return size;
    } else {
        rs_log_error("don't know how to handle return %d from OS X sendfile",
                     ret);
        return -1;
    }
}
#else
/* Please write a sendfile implementation for your system! */
static ssize_t sys_sendfile(int UNUSED(ofd), int UNUSED(ifd),
                            off_t *UNUSED(offset), size_t UNUSED(size))
{
    rs_log_warning("no sendfile implementation on this platform");
    errno = ENOSYS;
    return -1;
}
#endif /* !(__FreeBSD__) && !def(linux) && ... */


/*
 * Transmit the body of a file using sendfile().
 *
 * Linux at the moment requires the input be page-based -- ie a disk file, and
 * only on particular filesystems.  If the sendfile() call fails in a way that
 * makes us think that regular IO might work, then we try that instead.  For
 * example, the /tmp filesystem may not support sendfile().
 */
int
dcc_pump_sendfile(int ofd, int ifd, size_t size)
{
    ssize_t sent;
    off_t offset = 0;
    int ret;

    while (size) {
        /* Handle possibility of partial transmission, e.g. if
         * sendfile() is interrupted by a signal.  size is decremented
         * as we go. */

        sent = sys_sendfile(ofd, ifd, &offset, size);
        if (sent == -1) {
            if (errno == EAGAIN) {
                /* Sleep until we're able to write out more data. */
                if ((ret = dcc_select_for_write(ofd,
                                                dcc_get_io_timeout())) != 0) {
                    return ret;
                }
                rs_trace("select() returned, continuing to write");
            } else if (errno == EINTR) {
                rs_trace("sendfile() interrupted, continuing");
            } else if (offset == 0) {
                /* The offset==0 tests is because we may be part way through
                 * the file.  We can't just naively go back to read/write
                 * because sendfile() does not update the file pointer: we
                 * would need to lseek() first.  That case is not handled at
                 * the moment because it's unlikely that sendfile() would
                 * suddenly be unsupported while we're using it.  A failure
                 * halfway through probably indicates a genuine error.
                 */
                rs_log_info("decided to use read/write rather than sendfile");
                return dcc_pump_readwrite(ofd, ifd, size);
            } else {
                rs_log_error("sendfile failed: %s", strerror(errno));
                return EXIT_IO_ERROR;
            }
        } else if (sent == 0) {
            rs_log_error("sendfile returned 0? can't cope");
            return EXIT_IO_ERROR;
        } else if (sent != (ssize_t) size) {
            /* offset is automatically updated by sendfile. */
            size -= sent;
            rs_log_notice("sendfile: partial transmission of %ld bytes; retrying %ld @%ld",
                          (long) sent, (long) size, (long) offset);
        } else {
            /* normal case, everything was sent. */
            break;
        }
    }
    return 0;
}
#endif /* def HAVE_SENDFILE */
