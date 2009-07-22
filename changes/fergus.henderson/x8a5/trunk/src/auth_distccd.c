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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "distcc.h"
#include "dopt.h"
#include "exitcode.h"
#include "netutil.h"
#include "trace.h"

static int dcc_gssapi_accept_secure_context(int to_net_sd,
					    int from_net_sd,
					    OM_uint32 *ret_flags,
					    char **principal);
static int dcc_gssapi_recv_handshake(int from_net_sd, int to_net_sd);
static int dcc_gssapi_notify_client(int sd, char status);

/*Global credentials so they're only required and released once*/
/*in the most suitable place.*/
gss_cred_id_t creds;
/*Global security context in case other services*/
/*are implemented in the future.*/
gss_ctx_id_t distccd_ctx_handle = GSS_C_NO_CONTEXT;

/*
 * Perform any requested security.
 *
 * @param to_net_sd.	Socket to write to.
 *
 * @param from_net_sd.	Socket to read from.
 *
 * @param ret_flags.	A representation of the services requested
 *			by the client.
 *
 * @param principal.	The name of the client principal.
 *
 * Returns 0 on success, otherwise error.
 */
int dcc_gssapi_check_client(int to_net_sd, int from_net_sd) {
    char *principal = NULL;
    int ret;
    OM_uint32 ret_flags;

    if ((ret = dcc_gssapi_accept_secure_context(to_net_sd,
					       from_net_sd,
					       &ret_flags,
					       &principal)) != 0) {
        return ret;
    }

    if ((ret = dcc_gssapi_compare_flags(GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG, ret_flags)) != 0) {
	dcc_gssapi_delete_ctx(&distccd_ctx_handle);
        return ret;
    }

    rs_log_info("Notifying client.");

    if ((ret = dcc_gssapi_notify_client(to_net_sd, ACCESS)) != 0) {
        dcc_gssapi_delete_ctx(&distccd_ctx_handle);
	return ret;
    }

    return 0;
}

/*
 * Accept a secure context using the GSS-API.  A handshake is attempted
 * in order to detect a non-authenticating client.
 *
 * @param to_net_sd.	Socket to write to.
 *
 * @param from_net_sd.	Socket to read from.
 *
 * @param ret_flags.	A representation of the security services
 *			requested by the client to be returned to
 *			the invoking function.
 *
 * @param principal.	The name of the client principal to be returned
 *			to the invoking function.
 *
 * Returns 0 on success, otherwise error.
 */
static int dcc_gssapi_accept_secure_context(int to_net_sd,
					    int from_net_sd,
					    OM_uint32 *ret_flags,
					    char **principal) {
    gss_buffer_desc input_tok = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc name_buffer = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_tok = GSS_C_EMPTY_BUFFER;
    gss_name_t int_client_name;
    gss_OID name_type;
    int ret;
    OM_uint32 major_status, minor_status, return_status;

    input_tok.value = NULL;
    input_tok.length = 0;
    output_tok.value = NULL;
    output_tok.length = 0;

    if ((ret = dcc_gssapi_recv_handshake(from_net_sd, to_net_sd)) != 0) {
        return ret;
    }

    do {
            if ((ret = recv_token(from_net_sd, &input_tok)) != 0) {
		rs_log_error("Error receiving token.");
		rs_log_info("(eof may indicate a client error during ctx init).");

                return ret;
            }

            major_status = gss_accept_sec_context(&minor_status,
						  &distccd_ctx_handle,
						  creds,
						  &input_tok,
						  GSS_C_NO_CHANNEL_BINDINGS,
						  &int_client_name,
						  NULL,
						  &output_tok,
						  ret_flags,
						  NULL,
						  NULL);

            if (GSS_ERROR(major_status)) {
		        rs_log_crit("Failed to accept secure context.");
		        dcc_gssapi_status_to_log(major_status, GSS_C_GSS_CODE);
	            dcc_gssapi_status_to_log(minor_status, GSS_C_MECH_CODE);

                if ((return_status = gss_release_buffer(&minor_status,
						  &input_tok)) != GSS_S_COMPLETE) {
                    rs_log_error("Failed to release buffer.");
                }

	            return EXIT_GSSAPI_FAILED;
            }

            if (output_tok.length > 0) {
                if ((ret = send_token(to_net_sd,
				     &output_tok)) != 0) {
                    dcc_gssapi_cleanup(&input_tok,
				       &output_tok,
				       &int_client_name);
		    return ret;
                }

                if ((return_status = gss_release_buffer(&minor_status,
						  &output_tok)) != GSS_S_COMPLETE) {
                    rs_log_error("Failed to release buffer.");
                }
            }

            if (input_tok.length > 0) {
                if ((return_status = gss_release_buffer(&minor_status,
						  &input_tok)) != GSS_S_COMPLETE) {
                    rs_log_error("Failed to release buffer.");
                }
            }


    } while (major_status != GSS_S_COMPLETE);

    if ((major_status = gss_display_name(&minor_status,
					int_client_name,
					&name_buffer,
					&name_type)) != GSS_S_COMPLETE) {
        rs_log_error("Failed to convert name.");
    }

    rs_log_info("Successfully authenticated %s.", (char *) name_buffer.value);

    if ((*principal = malloc(strlen((char *) name_buffer.value) + 1 )) == NULL) {
        rs_log_error("malloc failed : %ld bytes: out of memory.",
                                        (long) (strlen((char *) name_buffer.value) + 1));
        return EXIT_OUT_OF_MEMORY;
    }

    strcpy(*principal, (char *) name_buffer.value);
    dcc_gssapi_cleanup(&input_tok, &output_tok, &int_client_name);

    return 0;
}

/*
 * Attempt handshake exchange with the client to indicate server's
 * desire to authentciate.
 *
 * @param from_net_sd.	Socket to read from.
 *
 * @param to_net_sd.	Socket to write to.
 *
 * Returns 0 on success, otherwise error.
 */
static int dcc_gssapi_recv_handshake(int from_net_sd, int to_net_sd) {
    char auth;
    int ret;

    rs_log_info("Receiving handshake.");

    if ((ret = dcc_readx(from_net_sd, &auth, sizeof(auth))) != 0) {
        return ret;
    }

    rs_log_info("Received %c.", auth);

    if (auth != HANDSHAKE) {
	rs_log_crit("No client handshake - did the client require authentication?");
	return EXIT_GSSAPI_FAILED;
    }

    rs_log_info("Sending handshake.");

    if ((ret = dcc_writex(to_net_sd, &auth, sizeof(auth))) != 0) {
        return ret;
    }

    rs_log_info("Sent %c.", auth);

    return 0;
}

/*
 * Send notification of access/no access to client.
 *
 * @param sd.		Socket to write notification to.
 *
 * @param status.	Status of access request.
 *			Either 'y' or 'n'
 *
 * Returns 0 on success, otherwise error.
 */
static int dcc_gssapi_notify_client(int sd, char status) {
    int ret;

    if ((ret = dcc_writex(sd, &status, sizeof(status))) != 0) {
	rs_log_crit("Failed to notify client.");
	return ret;
    }

    return 0;
}

/*
 * Acquire credentials for the distccd daemon.  We attempt to extract
 * the server principal name from the environment and ascertain the
 * name type.
 *
 * Returns 0 on success, otherwise error.
 */
int dcc_gssapi_acquire_credentials(void) {
    char *princ_env_val = NULL;
    gss_buffer_desc name_buffer = GSS_C_EMPTY_BUFFER;
    gss_name_t int_princ_name;
    gss_OID name_type;
    OM_uint32 major_status, minor_status;

    princ_env_val = getenv("DISTCCD_PRINCIPAL");

    if (princ_env_val == NULL) {
        rs_log_error("No principal name specified.");
        return EXIT_GSSAPI_FAILED;
    }

    if (strchr(princ_env_val, '@') != NULL) {
        name_type = GSS_C_NT_HOSTBASED_SERVICE;
    } else {
        name_type = GSS_C_NT_USER_NAME;
    }

    name_buffer.value = princ_env_val;
    name_buffer.length = strlen(princ_env_val);

    rs_log_info("Acquiring credentials.");

    name_buffer.length = strlen(name_buffer.value);

    if ((major_status = gss_import_name(&minor_status,
				       &name_buffer,
				       name_type,
				       &int_princ_name)) != GSS_S_COMPLETE) {
	rs_log_error("Failed to import princ name (%s) to internal GSS-API format.",
							(char *) name_buffer.value);
        return EXIT_GSSAPI_FAILED;
    }

    major_status = gss_acquire_cred(&minor_status,
                                    int_princ_name,
                                    0,
                                    GSS_C_NO_OID_SET,
                                    GSS_C_ACCEPT,
                                    &creds,
                                    NULL,
                                    NULL);

    if (major_status != GSS_S_COMPLETE) {
        rs_log_crit("Failed to acquire credentials.");
        dcc_gssapi_status_to_log(major_status, GSS_C_GSS_CODE);
	    dcc_gssapi_status_to_log(minor_status, GSS_C_MECH_CODE);

	    if ((major_status = gss_release_name(&minor_status,
					    &int_princ_name)) != GSS_S_COMPLETE) {
	        rs_log_error("Failed to release GSS-API buffer.");
        }

        return EXIT_GSSAPI_FAILED;
    }

    if ((major_status = gss_release_name(&minor_status,
					&int_princ_name)) != GSS_S_COMPLETE) {
	rs_log_error("Failed to release GSS-API buffer.");
    }

    rs_log_info("Credentials successfully acquired for %s.",
						(char *) name_buffer.value);

    name_buffer.value = NULL;

    if ((major_status = gss_release_buffer(&minor_status,
					  &name_buffer)) != GSS_S_COMPLETE) {
        rs_log_error("Failed to release GSS-API buffer.");
    }

    return 0;
}

/*
 * Release acquired credentials.
 */
void dcc_gssapi_release_credentials(void) {
    OM_uint32 major_status, minor_status;

    if ((major_status = gss_release_cred(&minor_status,
					&creds)) != GSS_S_COMPLETE) {
	rs_log_error("Failed to release credentials.");
    }

    rs_log_info("Credentials released successfully.");
}
