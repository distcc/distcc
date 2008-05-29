/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
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

#ifndef DCC_EMAILLOG_H
#define DCC_EMAILLOG_H

/* See also include_server/basics.py */
#define DCC_EMAILLOG_WHOM_TO_BLAME "distcc-pump-errors"
void dcc_please_send_email(void);
void dcc_setup_log_email(void);
void dcc_maybe_send_email(void);
int dcc_add_file_to_log_email(const char *description, const char *fname);

#endif /* EMAILLOG_H */
