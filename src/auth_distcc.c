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
#include <netdb.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "auth.h"
#include "distcc.h"
#include "exitcode.h"
#include "hosts.h"
#include "netutil.h"
#include "trace.h"

static int dcc_gssapi_establish_secure_context(const struct dcc_hostdef *host,
                           int to_net_sd,
					       int from_net_sd,
					       OM_uint32 req_flags,
					       OM_uint32 *ret_flags);
static int dcc_gssapi_send_handshake(int to_net_sd, int from_net_sd);
static int dcc_gssapi_recv_notification(int sd);

/**
 * Global security context in case other services are implemented in the
 * future.
 */
gss_ctx_id_t distcc_ctx_handle = GSS_C_NO_CONTEXT;

/*
 * Perform any requested security.  Message replay and out of sequence
 * detection are given in addition to mutual authentication.
 *
 * @param to_net_sd.	Socket to write to.
 *
 * @param from_net_sd.	Socket to read from.
 *
 * Returns 0 on success, otherwise error.
 */
int dcc_gssapi_perform_requested_security(const struct dcc_hostdef *host,
					  int to_net_sd,
					  int from_net_sd) {
    int ret;
    OM_uint32 req_flags, ret_flags;

    req_flags = GSS_C_MUTUAL_FLAG | GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG;

    if ((ret = dcc_gssapi_establish_secure_context(host,
						  to_net_sd,
						  from_net_sd,
						  req_flags,
						  &ret_flags)) != 0) {
	return ret;
    }

    if ((ret = dcc_gssapi_compare_flags(req_flags, ret_flags)) != 0) {
	dcc_gssapi_delete_ctx(&distcc_ctx_handle);
	return ret;
    }

    if ((ret = dcc_gssapi_recv_notification(from_net_sd)) != 0) {
        dcc_gssapi_delete_ctx(&distcc_ctx_handle);
        return ret;
    }

    rs_log_info("Authentication complete - happy compiling!");

    return 0;
}

/*
 * Establish a secure context using the GSS-API.  A handshake is attempted in
 * order to detect a non-authenticating server.  The server IP address is obtained
 * from the socket and is used to perform an fqdn lookup in case a DNS alias is
 * used as a host spec, this ensures we authenticate against the correct server.
 * We attempt to extract the server principal name, a service, from the
 * environment, if it is not specified we use "host/" as a default.
 *
 * @pram to_net_sd.	Socket to write to.
 *
 * @pram from_net_sd.	Socket to read from.
 *
 * @param req_flags.	A representation of the security services to
 *			be requested.
 *
 * @param ret_flags.    A representation of the security services
 *			provided/supported by the underlying mechanism
 *			to be returned to the invoking function.
 *
 * Returns 0 on success, otherwise error.
 */
static int dcc_gssapi_establish_secure_context(const struct dcc_hostdef *host,
					       int to_net_sd,
					       int from_net_sd,
					       OM_uint32 req_flags,
					       OM_uint32 *ret_flags) {
    char *ext_princ_name = NULL;
    char *full_name = NULL;
    char *princ_env_val = NULL;
    gss_buffer_desc input_tok = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc name_buffer = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_tok = GSS_C_EMPTY_BUFFER;
    gss_name_t int_serv_name;
    gss_OID name_type;
    int ret;
    OM_uint32 major_status, minor_status, return_status;
    socklen_t addr_len;
    struct hostent *hp;
    struct sockaddr_in addr;

    if (!host->auth_name) {
        addr_len = sizeof(addr);

        if ((ret = getpeername(to_net_sd, (struct sockaddr *)&addr, &addr_len)) != 0) {
            rs_log_error("Failed to look up peer address using socket \"%d\": %s.",
                        to_net_sd,
                        hstrerror(h_errno));
            return EXIT_CONNECT_FAILED;
        }

        rs_log_info("Successfully looked up IP address %s using socket %d.",
                                                inet_ntoa(addr.sin_addr),
                                                to_net_sd);

        if ((hp = gethostbyaddr((char *) &addr.sin_addr,
                                sizeof(addr.sin_addr),
                                AF_INET)) == NULL) {
            rs_log_error("Failed to look up host by address \"%s\": %s.",
                        inet_ntoa(addr.sin_addr),
                        hstrerror(h_errno));
            return EXIT_CONNECT_FAILED;
        }

        rs_log_info("Successfully looked up host %s using IP address %s.",
                                                hp->h_name,
                                                inet_ntoa(addr.sin_addr));
        full_name = hp->h_name;
    } else {
        full_name = host->auth_name;
    }

    if ((princ_env_val = getenv("DISTCC_PRINCIPAL"))) {
        if (asprintf(&ext_princ_name, "%s@%s", princ_env_val, full_name) < 0) {
            rs_log_error("Failed to allocate memory for asprintf.");
            return EXIT_OUT_OF_MEMORY;
        }

        name_type = GSS_C_NT_HOSTBASED_SERVICE;
    } else {
        if (asprintf(&ext_princ_name, "host/%s", full_name) < 0) {
            rs_log_error("Failed to allocate memory for asprintf.");
            return EXIT_OUT_OF_MEMORY;
        }

        name_type = GSS_C_NT_USER_NAME;
    }

    name_buffer.value = ext_princ_name;
    name_buffer.length = strlen(ext_princ_name);

    if ((major_status = gss_import_name(&minor_status,
				       &name_buffer,
				       name_type,
				       &int_serv_name)) != GSS_S_COMPLETE) {
	rs_log_error("Failed to import service name to internal GSS-API format.");
        return EXIT_GSSAPI_FAILED;
    }

    input_tok.value = NULL;
    input_tok.length = 0;
    output_tok.value = NULL;
    output_tok.length = 0;

    if ((ret = dcc_gssapi_send_handshake(to_net_sd, from_net_sd)) != 0) {
        return ret;
    }

    do
    {
        major_status = gss_init_sec_context(&minor_status,
					    GSS_C_NO_CREDENTIAL,
					    &distcc_ctx_handle,
					    int_serv_name,
					    GSS_C_NO_OID,
					    req_flags,
					    0,
					    GSS_C_NO_CHANNEL_BINDINGS,
					    &input_tok,
					    NULL,
					    &output_tok,
					    ret_flags,
					    NULL);

	if (GSS_ERROR(major_status)) {
	    rs_log_crit("Failed to initiate a secure context.");
	    dcc_gssapi_status_to_log(major_status, GSS_C_GSS_CODE);
	    dcc_gssapi_status_to_log(minor_status, GSS_C_MECH_CODE);
	    return EXIT_GSSAPI_FAILED;
	}

	if (output_tok.length > 0) {
	    if ((ret = send_token(to_net_sd, &output_tok)) != 0) {
		dcc_gssapi_cleanup(&input_tok, &output_tok, &int_serv_name);
		return ret;
	    } else {
		if ((return_status = gss_release_buffer(&minor_status,
						 &output_tok)) != GSS_S_COMPLETE) {
		    rs_log_error("Failed to release buffer.");
		}
	    }
	}

	if (input_tok.length > 0) {
	    if ((return_status = gss_release_buffer(&minor_status,
						   &input_tok)) != GSS_S_COMPLETE) {
		rs_log_error("Failed to release buffer.");
	    }
	}

	if (major_status == GSS_S_CONTINUE_NEEDED) {
	    if ((ret = recv_token(from_net_sd, &input_tok)) != 0) {
		dcc_gssapi_cleanup(&input_tok, &output_tok, &int_serv_name);
		return ret;
	    }
	}

    } while (major_status != GSS_S_COMPLETE);

    rs_log_info("Successfully authenticated %s.", ext_princ_name);
    dcc_gssapi_cleanup(&input_tok, &output_tok, &int_serv_name);

    if ((major_status = gss_release_buffer(&minor_status,
					  &name_buffer)) != GSS_S_COMPLETE) {
	rs_log_error("Failed to release buffer.");
    }

    return 0;
}

/*
 * Attempt handshake exchange with the server to indicate client's
 * desire to authenticate.
 *
 * @param to_net_sd.	Socket to write to.
 *
 * @param from_net_sd.	Socket to read from.
 *
 * Returns 0 on success, otherwise error.
 */
static int dcc_gssapi_send_handshake(int to_net_sd, int from_net_sd) {
    char auth = HANDSHAKE;
    fd_set sockets;
    int ret;
    struct timeval timeout;

    rs_log_info("Sending handshake.");

    if ((ret = dcc_writex(to_net_sd, &auth, sizeof(auth))) != 0) {
	return ret;
    }

    rs_log_info("Sent %c.", auth);

    FD_ZERO(&sockets);
    FD_SET(from_net_sd, &sockets);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    ret = select(from_net_sd + 1, &sockets, NULL, NULL, &timeout);

    if (ret < 0) {
        rs_log_error("select failed: %s.", strerror(errno));
        return EXIT_IO_ERROR;
    }

    if (ret == 0) {
        rs_log_error("Timeout - does this server require authentication?");
        return EXIT_TIMEOUT;
    }

    rs_log_info("Receiving handshake.");

    if ((ret = dcc_readx(from_net_sd, &auth, sizeof(auth))) != 0) {
        return ret;
    }

    rs_log_info("Received %c.", auth);

    if (auth != HANDSHAKE) {
        rs_log_crit("No server handshake.");
        return EXIT_GSSAPI_FAILED;
    }

    return 0;
}

/*
 * Receive notification from an authenticating server as to
 * whether the client has successfully gained access or not.
 *
 * @param sd.	Socket to read from.
 *
 * Returns 0 on success, otherwise error.
 */
static int dcc_gssapi_recv_notification(int sd) {
    char notification;
    fd_set sockets;
    int ret;
    struct timeval timeout;

    FD_ZERO(&sockets);
    FD_SET(sd, &sockets);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    ret = select(sd + 1, &sockets, NULL, NULL, &timeout);

    if (ret < 0) {
        rs_log_error("select failed: %s.", strerror(errno));
        return EXIT_IO_ERROR;
    }

    if (ret == 0) {
        rs_log_error("Timeout - error receiving notification.");
        return EXIT_TIMEOUT;
    }

    if ((ret = dcc_readx(sd, &notification, sizeof(notification))) != 0) {
        rs_log_crit("Failed to receive notification.");
        return ret;
    }

    if (notification != ACCESS) {
	    rs_log_crit("Access denied by server.");
        rs_log_info("Your principal may be blacklisted or may not be whitelisted.");
	    return EXIT_ACCESS_DENIED;
    }

    rs_log_info("Access granted by server.");
    return 0;
}
