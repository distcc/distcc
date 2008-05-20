/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4 fill-column: 78 -*-
 *
 * Copyright (C) 2005, 2006, 2007 by Google
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

int dcc_cleanup_dotd(const char *dotd_fname,
                     char **new_dotd_fname,
                     const char *root_dir,
                     const char *client_out_name,
                     const char *server_out_name);

int dcc_get_dotd_info(char **argv, char **dotd_fname,
                      int *needs_dotd, int *sets_dotd_target,
                      char **dotd_target);
