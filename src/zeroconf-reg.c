/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * Copyright (C) 2007 Lennart Poettering
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

#include <assert.h>
#include <stdio.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <avahi-common/thread-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>
#include <avahi-client/publish.h>

#include "distcc.h"
#include "zeroconf.h"
#include "trace.h"
#include "exitcode.h"

struct context {
    char *name;
    AvahiThreadedPoll *threaded_poll;
    AvahiClient *client;
    AvahiEntryGroup *group;
    uint16_t port;
    int n_cpus;
    int n_jobs;
};

static void publish_reply(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata);

static void register_stuff(struct context *ctx) {
#ifndef ENABLE_RFC2553
    static const AvahiProtocol dcc_proto = AVAHI_PROTO_INET;
#else
    static const AvahiProtocol dcc_proto = AVAHI_PROTO_UNSPEC;
#endif

    if (!ctx->group) {

        if (!(ctx->group = avahi_entry_group_new(ctx->client, publish_reply, ctx))) {
            rs_log_crit("Failed to create entry group: %s\n", avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }

    }

    if (avahi_entry_group_is_empty(ctx->group)) {
        char cpus[32], jobs[32], machine[64] = "cc_machine=", version[64] = "cc_version=", *m, *v;

        snprintf(cpus, sizeof(cpus), "cpus=%i", ctx->n_cpus);
        snprintf(jobs, sizeof(jobs), "jobs=%i", ctx->n_jobs);
        v = dcc_get_gcc_version(version+11, sizeof(version)-11);
        m = dcc_get_gcc_machine(machine+11, sizeof(machine)-11);

        /* Register our service */

        if (avahi_entry_group_add_service(
                    ctx->group,
                    AVAHI_IF_UNSPEC,
                    dcc_proto,
                    0,
                    ctx->name,
                    DCC_DNS_SERVICE_TYPE,
                    NULL,
                    NULL,
                    ctx->port,
                    "txtvers=1",
                    cpus,
                    jobs,
                    "distcc="PACKAGE_VERSION,
                    "gnuhost="GNU_HOST,
                    v ? version : NULL,
                    m ? machine : NULL,
                    (void*)NULL) < 0) {
            rs_log_crit("Failed to add service: %s\n", avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }

        if (v && m) {
            char stype[128];

            dcc_make_dnssd_subtype(stype, sizeof(stype), v, m);

            if (avahi_entry_group_add_service_subtype(
                        ctx->group,
                        AVAHI_IF_UNSPEC,
                        AVAHI_PROTO_UNSPEC,
                        0,
                        ctx->name,
                        DCC_DNS_SERVICE_TYPE,
                        NULL,
                        stype) < 0) {
                rs_log_crit("Failed to add service: %s\n", avahi_strerror(avahi_client_errno(ctx->client)));
                goto fail;
            }
        } else
            rs_log_warning("Failed to determine CC version, not registering DNS-SD service subtype!");

        if (avahi_entry_group_commit(ctx->group) < 0) {
            rs_log_crit("Failed to commit entry group: %s\n", avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }

    }

    return;

fail:
    avahi_threaded_poll_quit(ctx->threaded_poll);
}

/* Called when publishing of service data completes */
static void publish_reply(AvahiEntryGroup *UNUSED(g), AvahiEntryGroupState state, void *userdata) {
    struct context *ctx = userdata;

    switch (state) {

        case AVAHI_ENTRY_GROUP_COLLISION: {
            char *n;

            /* Pick a new name for our service */

            n = avahi_alternative_service_name(ctx->name);
            assert(n);

            avahi_free(ctx->name);
            ctx->name = n;

            register_stuff(ctx);
            break;
        }

        case AVAHI_ENTRY_GROUP_FAILURE:
            rs_log_crit("Failed to register service: %s\n", avahi_strerror(avahi_client_errno(ctx->client)));
            avahi_threaded_poll_quit(ctx->threaded_poll);
            break;

        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            ;
    }
}

static void client_callback(AvahiClient *client, AvahiClientState state, void *userdata) {
    struct context *ctx = userdata;

    ctx->client = client;

    switch (state) {

        case AVAHI_CLIENT_S_RUNNING:

            register_stuff(ctx);
            break;

        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_REGISTERING:

            if (ctx->group)
                avahi_entry_group_reset(ctx->group);

            break;

        case AVAHI_CLIENT_FAILURE:

            if (avahi_client_errno(client) == AVAHI_ERR_DISCONNECTED) {
                int error;

                avahi_client_free(ctx->client);
                ctx->client = NULL;
                ctx->group = NULL;

                /* Reconnect to the server */

                if (!(ctx->client = avahi_client_new(
                              avahi_threaded_poll_get(ctx->threaded_poll),
                              AVAHI_CLIENT_NO_FAIL,
                              client_callback,
                              ctx,
                              &error))) {

                    rs_log_crit("Failed to contact server: %s\n", avahi_strerror(error));
                    avahi_threaded_poll_quit(ctx->threaded_poll);
                }

            } else {
                rs_log_crit("Client failure: %s\n", avahi_strerror(avahi_client_errno(client)));
                avahi_threaded_poll_quit(ctx->threaded_poll);
            }

            break;

        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}

/* register a distcc service in DNS-SD/mDNS with the given port, number of CPUs, and maximum concurrent jobs */
void* dcc_zeroconf_register(uint16_t port, int n_cpus, int n_jobs) {
    struct context *ctx = NULL;
    char service[256] = "distcc@";
    int error;

    ctx = malloc(sizeof(struct context));
    assert(ctx);
    ctx->client = NULL;
    ctx->group = NULL;
    ctx->threaded_poll = NULL;
    ctx->port = port;
    ctx->n_cpus = n_cpus;
    ctx->n_jobs = n_jobs;

    /* Prepare service name */
    gethostname(service+7, sizeof(service)-8);
    service[sizeof(service)-1] = 0;

    ctx->name = strdup(service);
    assert(ctx->name);

    if (!(ctx->threaded_poll = avahi_threaded_poll_new())) {
        rs_log_crit("Failed to create event loop object.\n");
        goto fail;
    }

    if (!(ctx->client = avahi_client_new(avahi_threaded_poll_get(ctx->threaded_poll), AVAHI_CLIENT_NO_FAIL, client_callback, ctx, &error))) {
        rs_log_crit("Failed to create client object: %s\n", avahi_strerror(error));
        goto fail;
    }

    /* Create the mDNS event handler */
    if (avahi_threaded_poll_start(ctx->threaded_poll) < 0) {
        rs_log_crit("Failed to create thread.\n");
        goto fail;
    }

    return ctx;

fail:

    if (ctx)
        dcc_zeroconf_unregister(ctx);

    return NULL;
}

/* Unregister this server from DNS-SD/mDNS */
int dcc_zeroconf_unregister(void *u) {
    struct context *ctx = u;

    if (ctx->threaded_poll)
        avahi_threaded_poll_stop(ctx->threaded_poll);

    if (ctx->client)
        avahi_client_free(ctx->client);

    if (ctx->threaded_poll)
        avahi_threaded_poll_free(ctx->threaded_poll);

    avahi_free(ctx->name);

    free(ctx);

    return 0;
}
