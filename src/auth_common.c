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

#include <config.h>

#ifdef HAVE_GSSAPI
#include <arpa/inet.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "distcc.h"
#include "exitcode.h"
#include "netutil.h"
#include "rpc.h"
#include "trace.h"

/*
 * Provide a textual representation of a GSS-API or mechanism-specific
 * status code and write this to the log file.
 *
 * @param status_code.	Status code to convert.
 *
 * @param status_type.	The type of the status code, either GSS_C_GSS_CODE
 *			for a GSS-API status code or GSS_C_MECH_CODE
 *			for a mechanism-specific status code.
 */
void dcc_gssapi_status_to_log(OM_uint32 status_code, int status_type) {
    gss_buffer_desc status_string = GSS_C_EMPTY_BUFFER;
    OM_uint32 major_status, minor_status;
    OM_uint32 message_ctx = 0;

    do {
        major_status = gss_display_status(&minor_status,
					  status_code,
					  status_type,
					  GSS_C_NULL_OID,
					  &message_ctx,
					  &status_string);

	rs_log_info("%s: %s", (status_type == GSS_C_GSS_CODE) ? "GSSAPI" : "Mech" ,
					  (char *) status_string.value);
        gss_release_buffer(&minor_status, &status_string);

    } while (message_ctx != 0);

    (void) major_status;
    (void) minor_status;
}

/*
 * Perform a cleanup on specified GSS-API internal data formats.
 *
 * @param input_tok.	The input token to release.
 *
 * @param output_tok.	The output token to release.
 *
 * @param name.		The name to release.
 */
void dcc_gssapi_cleanup(gss_buffer_desc *input_tok,
			gss_buffer_desc *output_tok,
			gss_name_t *name) {
    OM_uint32 major_status, minor_status;

    if (input_tok != NULL) {
        if ((major_status = gss_release_buffer(&minor_status,
					      input_tok)) != GSS_S_COMPLETE) {
            rs_log_error("Failed to release buffer.");
        }
    }

    if (output_tok != NULL) {
        if ((major_status = gss_release_buffer(&minor_status,
					      output_tok)) != GSS_S_COMPLETE) {
            rs_log_error("Failed to release buffer.");
        }
    }

    if (name != NULL) {
        if ((major_status = gss_release_name(&minor_status,
					    name)) != GSS_S_COMPLETE) {
            rs_log_error("Failed to release name.");
        }
    }

    (void) major_status;
    (void) minor_status;
}

/*
 * Perform a comparison of two sets of flags representing security
 * services.  At the moment we only check for mutual authentication,
 * replay and out of sequence detection, but this function could be
 * extended to include others.
 *
 * @param req_flags.	The first set of flags to be compared and
 *                      represents what is required by the client
 *                      or server.
 *
 * @param ret_flags.	The second set of flags to be compared and
 *                      what is provided/returned by the peer.
 *
 * Returns 0 on success, otherwise error.
 */
int dcc_gssapi_compare_flags(OM_uint32 req_flags, OM_uint32 ret_flags) {
    if (req_flags & GSS_C_MUTUAL_FLAG) {
        if (ret_flags & GSS_C_MUTUAL_FLAG) {
            rs_log_info("Mutual authentication requested and granted.");
        } else {
            rs_log_crit("Requested security services don't match those returned.");
            return EXIT_GSSAPI_FAILED;
        }
    }

    if (req_flags & GSS_C_REPLAY_FLAG) {
        if (ret_flags & GSS_C_REPLAY_FLAG) {
            rs_log_info("Replay detection requested and granted.");
        } else {
            rs_log_crit("Requested security services don't match those returned.");
            return EXIT_GSSAPI_FAILED;
        }
    }

    if (req_flags & GSS_C_SEQUENCE_FLAG) {
        if (ret_flags & GSS_C_SEQUENCE_FLAG) {
            rs_log_info("Out of sequence detection requested and granted.");
        } else {
            rs_log_crit("Requested security services don't match those returned.");
            return EXIT_GSSAPI_FAILED;
        }
    }

    return 0;
}

/*
 * Delete a specified secure context from the current process.
 * The output buffer token is not used as we do not notify the
 * peer.
 *
 * @param ctx_handle.	The secure context to delete.
 */
void dcc_gssapi_delete_ctx(gss_ctx_id_t *ctx_handle) {
    OM_uint32 major_status, minor_status;

    if (ctx_handle != NULL) {
        if (*ctx_handle != GSS_C_NO_CONTEXT) {
            if ((major_status = gss_delete_sec_context(&minor_status,
					ctx_handle,
					GSS_C_NO_BUFFER)) != GSS_S_COMPLETE) {
                rs_log_error("Failed to delete context.");
            }
        }
    }
}

/*
 * Send the specified token.  First we transmit the token length,
 * then the token itself.
 *
 * @param token.	The token to be sent.
 *
 * Returns 0 on success, otherwise error.
 */
int send_token(int sd, gss_buffer_t token) {
    int ret;

    if ((ret = dcc_x_token_int(sd, "TLEN", token->length)) != 0) {
	return ret;
    }

    if ((ret = dcc_writex(sd, token->value, token->length)) != 0) {
        return ret;
    }

    return 0;
}

/*
 * Receive a token.  First we receive the token length,
 * then the token itself.
 *
 * @param token.	The token to be received.
 *
 * Returns 0 on success, otherwise error.
 */
int recv_token(int sd, gss_buffer_t token) {
    int ret;
    unsigned length;

    if ((ret = dcc_r_token_int(sd, "TLEN", &length)) != 0) {
        return ret;
    }

    if (length > INT_MAX || length == 0) {
	rs_log_error("Malformed token length.");
	return EXIT_IO_ERROR;
    }

    token->length = length;
    token->value = malloc(length);

    if (token->value == NULL && length != 0) {
	    rs_log_error("malloc failed : %lu bytes: out of memory.",
	                                    (unsigned long) length);
        return EXIT_OUT_OF_MEMORY;
    }

    if ((ret = dcc_readx(sd, token->value, token->length)) != 0) {
        return ret;
    }

    return 0;
}
