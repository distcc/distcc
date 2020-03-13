/* Copyright (C) 2008 CERN
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

/* Author: Ian Baker */

#include <gssapi/gssapi.h>

/* Handshake exchange character. */
#define HANDSHAKE '*'
/* Notification of server access. */
#define ACCESS 'y'
/* Notification of server access denied. */
#define NO_ACCESS 'n'

struct dcc_hostdef;

int dcc_gssapi_acquire_credentials(void);
void dcc_gssapi_release_credentials(void);
int dcc_gssapi_obtain_list(int mode);
void dcc_gssapi_free_list(void);
int dcc_gssapi_check_client(int to_net_fd, int from_net_fd);
int dcc_gssapi_perform_requested_security(const struct dcc_hostdef *host,
                      int to_net_fd,
					  int from_net_fd);
void dcc_gssapi_status_to_log(OM_uint32 status_code, int status_type);
void dcc_gssapi_cleanup(gss_buffer_desc *input_tok,
			gss_buffer_desc *output_tok,
			gss_name_t *name);
int dcc_gssapi_compare_flags(OM_uint32 req_flags, OM_uint32 ret_flags);
void dcc_gssapi_delete_ctx(gss_ctx_id_t *ctx_handle);
int send_token(int sd, gss_buffer_t token);
int recv_token(int sd, gss_buffer_t token);
