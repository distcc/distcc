/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * Copyright 2007, 2009 Google Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emaillog.h"
#include "distcc.h"
#include "util.h"
#include "trace.h"
#include "bulk.h"
#include "snprintf.h"
#include "exitcode.h"

/* if never_send_email is true, we won't send email
   even if should_send_email is true */
static int should_send_email = 0;
static int never_send_email = 0;
static char *email_fname;
static int email_fileno = -1;
static int email_errno;

static const char logmailer[] = "/bin/mail";
static const char email_subject[] = "distcc-pump email" ;
static const char cant_send_message_format[] =
   "Please notify %s that distcc tried to send them email but failed";
static const char will_send_message_format[] = "Will send an email to %s";

static const char dcc_emaillog_whom_to_blame[] = DCC_EMAILLOG_WHOM_TO_BLAME;

void dcc_please_send_email(void) {
    should_send_email = 1;
}

void dcc_setup_log_email(void) {
    never_send_email = !dcc_getenv_bool("DISTCC_ENABLE_DISCREPANCY_EMAIL", 0);
    if (never_send_email)
      return;

    /* email_fname lives until the program exits.
       The file itself will eventually get unlinked by dcc_cleanup_tempfiles(),
       but email_fileno survives until after we send email, so the file won't
       get removed until the emailing (child) process is done.
    */

    dcc_make_tmpnam("distcc_error_log", "txt", &email_fname);

    email_fileno = open(email_fname, O_RDWR | O_TRUNC);
    if (email_fileno >= 0) {
      rs_add_logger(rs_logger_file, RS_LOG_DEBUG, NULL, email_fileno);
      rs_trace_set_level(RS_LOG_DEBUG);
    } else {
       email_errno = errno;
    }
}

int dcc_add_file_to_log_email(const char *description,
                              const char *fname) {
  char begin[] = "\nBEGIN ";
  char end[] = "\nEND ";
  int in_fd = 0;
  off_t fsize;
  int ret;

  if (never_send_email) return 0;

  ret = dcc_open_read(fname, &in_fd, &fsize);
  if (ret != 0) return ret;

  ret = write(email_fileno, begin, strlen(begin));
  if (ret != (ssize_t) strlen(begin)) return EXIT_IO_ERROR;

  ret = write(email_fileno, description, strlen(description));
  if (ret != (ssize_t) strlen(description)) return EXIT_IO_ERROR;

  ret = write(email_fileno, "\n", 1);
  if (ret != 1) return EXIT_IO_ERROR;

  ret = dcc_pump_readwrite(email_fileno, in_fd, fsize);
  if (ret != 0) return ret;

  ret = write(email_fileno, end, strlen(end));
  if (ret != (ssize_t) strlen(end)) return EXIT_IO_ERROR;

  ret = write(email_fileno, description, strlen(description));
  if (ret != (ssize_t) strlen(description)) return EXIT_IO_ERROR;

  ret = write(email_fileno, "\n", 1);
  if (ret != 1) return EXIT_IO_ERROR;

  close(in_fd);

  return 0;
}

void dcc_maybe_send_email(void) {
  int child_pid = 0;
  const char *whom_to_blame;
  if ((whom_to_blame = getenv("DISTCC_EMAILLOG_WHOM_TO_BLAME"))
      == NULL) {
    whom_to_blame = dcc_emaillog_whom_to_blame;
  }
  char *cant_send_message_to;
  int ret;

  if (should_send_email == 0) return;
  if (never_send_email) return;

  rs_log_warning(will_send_message_format, whom_to_blame);
  ret = asprintf(&cant_send_message_to, cant_send_message_format, whom_to_blame);
  if (ret == -1) {
      fprintf(stderr, "error sending email: asprintf() failed");
      return;
  }

  if (email_fileno < 0) {
      errno = email_errno;
      perror(cant_send_message_to);
      free(cant_send_message_to);
      return;
  }

  child_pid = fork();
  if (child_pid == 0) {
    if (dup2(email_fileno, 0) == -1 ||
        lseek(email_fileno, 0, SEEK_SET) == -1 ||
        execl(logmailer,
              logmailer, "-s", email_subject, whom_to_blame,
              (char*)NULL) == -1) {
      perror(cant_send_message_to);
      /* The fork succeeded but we didn't get to exec, or the exec
         failed. We need to exit immediately, otherwise the cleanup
         code will get executed twice.
      */
      _exit(1);
    }
  } else if (child_pid < 0) {
      perror(cant_send_message_to);
  }
  free(cant_send_message_to);
}
