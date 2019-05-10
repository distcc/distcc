/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright 2005 Google Inc.
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

/* Author: Thomas Kho */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/statvfs.h>

#include "exitcode.h"
#include "distcc.h"
#include "trace.h"
#include "dopt.h"
#include "stats.h"
#include "srvnet.h"
#include "util.h"
#include "netutil.h"
#include "fcntl.h"
#include "daemon.h"

int dcc_statspipe[2];

#define MAX_FILENAME_LEN 1024

/* in prefork.c */
void dcc_manage_kids(int listen_fd);

struct stats_s {
    int counters[STATS_ENUM_MAX];
    int kids_avg[3]; /* 1, 5, 15m */
    int longest_job_time;
    char longest_job_name[MAX_FILENAME_LEN];
    char longest_job_compiler[MAX_FILENAME_LEN];
    int io_rate; /* read/write sectors per second */
    int compile_timeseries[300]; /* 300 3-sec time intervals */

    /*
     * linked list of statsdata. This is to keep the last
     * couple of minutes of compile stats for analysis.
     */
    struct statsdata *sd_root;
} dcc_stats;

struct statsdata {
    enum stats_e type;
    struct statsdata *next;

    /* used only for STATS_COMPILE_OK */
    struct timeval start;
    struct timeval stop;
    int time;
    char filename[MAX_FILENAME_LEN];
    char compiler[MAX_FILENAME_LEN];
};

const char *stats_text[20] = { "TCP_ACCEPT", "REJ_BAD_REQ", "REJ_OVERLOAD",
    "COMPILE_OK", "COMPILE_ERROR", "COMPILE_TIMEOUT", "CLI_DISCONN",
    "OTHER" };

/* Call this to initialize stats */
int dcc_stats_init() {
    if (arg_stats) {
        if (pipe(dcc_statspipe) == -1) {
            return -1;
        }
    }
    memset(&dcc_stats, 0, sizeof(dcc_stats));
    return 0;
}


/* In the preforking model call this to initialize stats for forked children */
void dcc_stats_init_kid() {
    if (arg_stats) {
        close(dcc_statspipe[0]);
    }
}


/**
 * Logs countable event of type e to stats server
 **/
void dcc_stats_event(enum stats_e e) {
    if (arg_stats) {
        struct statsdata sd;
        memset(&sd, 0, sizeof(sd));
        sd.type = e;
        dcc_writex(dcc_statspipe[1], &sd, sizeof(sd));
    }
}


/**
 * Logs a completed job to stats server
 **/
void dcc_stats_compile_ok(char *compiler, char *filename, struct timeval start,
     struct timeval stop, int time_usec) {
    if (arg_stats) {
        struct statsdata sd;
        memset(&sd, 0, sizeof(sd));

        sd.type = STATS_COMPILE_OK;
        /* also send compiler, filename & runtime */
        memcpy(&(sd.start), &start, sizeof(struct timeval));
        memcpy(&(sd.stop), &stop, sizeof(struct timeval));
        sd.time = time_usec;
        strncpy(sd.filename, filename, MAX_FILENAME_LEN - 1);
        strncpy(sd.compiler, compiler, MAX_FILENAME_LEN - 1);
        dcc_writex(dcc_statspipe[1], &sd, sizeof(sd));
    }
}


/*
 * tracks the compile times
 */
static void dcc_stats_update_compile_times(struct statsdata *sd) {
    struct statsdata *prev_sd = NULL;
    struct statsdata *curr_sd = NULL;
    struct statsdata *new_sd;
    time_t two_min_ago = time(NULL) - 120;

    /* Record file with longest runtime */
    if (dcc_stats.longest_job_time < sd->time) {
        dcc_stats.longest_job_time = sd->time;
        strncpy(dcc_stats.longest_job_name, sd->filename,
                MAX_FILENAME_LEN);
        strncpy(dcc_stats.longest_job_compiler, sd->compiler,
                MAX_FILENAME_LEN);
    }

    /* store stats for compile time calcs */
    new_sd = malloc(sizeof(struct statsdata));
    memcpy(new_sd, sd, sizeof(struct statsdata));
    new_sd->next = dcc_stats.sd_root;
    dcc_stats.sd_root = new_sd;


    /* drop elements older than 2min */
    curr_sd = dcc_stats.sd_root;
    while (curr_sd != NULL) {
        if (curr_sd->stop.tv_sec < two_min_ago) {
            /* delete the stat */
            if (prev_sd == NULL) {
                dcc_stats.sd_root = curr_sd->next;
            } else {
                prev_sd->next = curr_sd->next;
            }
            free(curr_sd);
            curr_sd = prev_sd->next;
        } else {
            /* we didn't delete anything. move forward by one */
            prev_sd = curr_sd;
            curr_sd = curr_sd->next;
        }
    }
}

/* caclulate the avg kids used */
static void dcc_stats_calc_kid_avg(void) {
    static int total_5m[5] = {0};
    static int total_15m[15] = {0};
    static int pos_5m = 0;
    static int pos_15m = 0;
    static time_t last = 0;
    struct timeval now;
    struct timeval time_p;
    struct statsdata *curr_sd;
    int total_running = 0;
    int running = 0;
    int t = 0;
    int x = 0;
    int total = 0;

    gettimeofday(&now, NULL);
    
    /* calculate average kids used over the last minute */
    if ((now.tv_sec - 60) >= last) {
        /* we look at 1min ago back to 2 min ago because we only register
         * compiles when they complete. If we look right now, we miss all 
         * the current compiles that haven't completed.
         */
        for (t=60; t<120; t++) {
            time_p.tv_usec = now.tv_usec;
            time_p.tv_sec = now.tv_sec - t;
            running = 0;
            curr_sd = dcc_stats.sd_root;

            while (curr_sd != NULL) {
                if ((dcc_timecmp(curr_sd->start, time_p) <= 0) &&
                    (dcc_timecmp(curr_sd->stop, time_p) >= 0)) {
                    running++;
                }
                curr_sd = curr_sd->next;
            }
            total_running += running;
        }
        dcc_stats.kids_avg[0] = total_running / 60;


        /* populate 5m kid avgs */
        total_5m[pos_5m] = dcc_stats.kids_avg[0];
        pos_5m++;
        if (pos_5m >= 5)
            pos_5m = 0;

        /* calc 5m kid avg */
        total = 0;
        for (x=0; x<5; x++)
            total += total_5m[x];
        dcc_stats.kids_avg[1] = total/5;


        /* populate 15m kid avgs */
        total_15m[pos_15m] = dcc_stats.kids_avg[0];
        pos_15m++;
        if (pos_15m >= 15)
            pos_15m = 0;

        /* calc 15m kid avg */
        total = 0;
        for (x=0; x<15; x++)
            total += total_15m[x];
        dcc_stats.kids_avg[2] = total/15;

        last = now.tv_sec;
    }
}


/*
 * Updates a running total of compiles in the last 1, 5, 15 minutes
 *
 * Returns the oldest slot
 */
static int dcc_stats_update_running_total(int increment) {
    static int prev_slot;
    static time_t last = 0;
    static int total = 0;
    int i;
    int cur_slot;
    int *const cts = dcc_stats.compile_timeseries;
    time_t now = time(NULL);

    cur_slot = (now / 3) % 300;

    if (last + 900 < now) {
        /* reset all stats; last call was >15 min ago */
        for (i = 0; i < 300; i++)
            cts[i] = total;
        prev_slot = cur_slot;
    }

    if (prev_slot != cur_slot) {
        /* different timeslot, so set the interval [prev, cur) */
        for (i = (prev_slot)%300; i != cur_slot; i = (i+1)%300) {
            cts[i] = total;
        }
        prev_slot = cur_slot;
    }

    total += increment;
    last = now;

    return cur_slot;
}


static int *dcc_stats_get_compile_totals(void) {
    int cur_slot;
    static int ct[3]; /* 1, 5, 15 min compile totals */
    int *const cts = dcc_stats.compile_timeseries;

    cur_slot = dcc_stats_update_running_total(0);

    ct[0] = cts[(cur_slot + 299)%300] - cts[(cur_slot+280)%300];
    ct[1] = cts[(cur_slot + 299)%300] - cts[(cur_slot+200)%300];
    ct[2] = cts[(cur_slot + 299)%300] - cts[(cur_slot+1)%300];

    return ct;
}


/* Sets dcc_stats.io_rate at most 50 secs */
static void dcc_stats_minutely_update(void) {
    static int prev_io_tot = -1;
    static time_t last = 0;
    int n_reads, n_writes;
    time_t now = time(NULL);

    if (last + 50 < now) {
        dcc_get_disk_io_stats(&n_reads, &n_writes);

        if (prev_io_tot == -1)
            dcc_stats.io_rate = -1;
        else
            dcc_stats.io_rate = (n_reads + n_writes - prev_io_tot) / (now - last);

        prev_io_tot = n_reads + n_writes;
        last = now;
    }
}

static long dcc_get_tmpdirinfo(void) {
    const char *tmp_dir;
    struct statvfs buf;

    if (dcc_get_tmp_top(&tmp_dir) != 0)
        return -1;

    if (statvfs(tmp_dir, &buf) != 0)
        return -1;

    if (buf.f_bsize >= 1024)
        return buf.f_bavail * (buf.f_bsize / 1024) / 1024;
    else
        return (buf.f_bavail * buf.f_bsize) / (1024 * 1024);
}

/**
 * Accept a connection on the stats port and send the reply, regardless of data
 * that the client sends us.
 **/
static void dcc_service_stats_request(int http_fd) {
    int acc_fd;
    int *ct;
    int num_D;
    int max_RSS;
    char *max_RSS_name;
    size_t reply_len;
    char challenge[1024];
    char reply[2048];
    struct dcc_sockaddr_storage cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    double loadavg[3];
    int free_space_mb;
    ssize_t ret;

    const char replytemplate[] = "\
HTTP/1.0 200 OK\n\
Content-Type: text/plain\n\
Connection: close\n\n\
argv /distccd\n\
<distccstats>\n\
dcc_tcp_accept %d\n\
dcc_rej_bad_req %d\n\
dcc_rej_overload %d\n\
dcc_compile_ok %d\n\
dcc_compile_error %d\n\
dcc_compile_timeout %d\n\
dcc_cli_disconnect %d\n\
dcc_other %d\n\
dcc_longest_job %s\n\
dcc_longest_job_compiler %s\n\
dcc_longest_job_time_msecs %d\n\
dcc_max_kids %d\n\
dcc_avg_kids1 %d\n\
dcc_avg_kids2 %d\n\
dcc_avg_kids3 %d\n\
dcc_current_load %d\n\
dcc_load1 %1.2lf\n\
dcc_load2 %1.2lf\n\
dcc_load3 %1.2lf\n\
dcc_num_compiles1 %d\n\
dcc_num_compiles2 %d\n\
dcc_num_compiles3 %d\n\
dcc_num_procstate_D %d\n\
dcc_max_RSS %d\n\
dcc_max_RSS_name %s\n\
dcc_io_rate %d\n\
dcc_free_space %d MB\n\
</distccstats>\n";

    dcc_stats_minutely_update(); /* force update to get fresh disk io data */
    ct = dcc_stats_get_compile_totals();
    dcc_getloadavg(loadavg);

    free_space_mb = dcc_get_tmpdirinfo();
    dcc_get_proc_stats(&num_D, &max_RSS, &max_RSS_name);

    if (dcc_stats.longest_job_name[0] == 0)
        strcpy(dcc_stats.longest_job_name, "none");
    if (dcc_stats.longest_job_compiler[0] == 0)
        strcpy(dcc_stats.longest_job_compiler, "none");

    acc_fd = accept(http_fd, (struct sockaddr *) &cli_addr, &cli_len);
    if (dcc_check_client((struct sockaddr *)&cli_addr,
                         (int) cli_len,
                         opt_allowed) == 0) {
        reply_len = snprintf(reply, 2048, replytemplate,
                               dcc_stats.counters[STATS_TCP_ACCEPT],
                               dcc_stats.counters[STATS_REJ_BAD_REQ],
                               dcc_stats.counters[STATS_REJ_OVERLOAD],
                               dcc_stats.counters[STATS_COMPILE_OK],
                               dcc_stats.counters[STATS_COMPILE_ERROR],
                               dcc_stats.counters[STATS_COMPILE_TIMEOUT],
                               dcc_stats.counters[STATS_CLI_DISCONN],
                               dcc_stats.counters[STATS_OTHER],
                               dcc_stats.longest_job_name,
                               dcc_stats.longest_job_compiler,
                               dcc_stats.longest_job_time,
                               dcc_max_kids,
                               dcc_stats.kids_avg[0],
                               dcc_stats.kids_avg[1],
                               dcc_stats.kids_avg[2],
                               dcc_getcurrentload(),
                               loadavg[0], loadavg[1], loadavg[2],
                               ct[0], ct[1], ct[2],
                               num_D, max_RSS, max_RSS_name,
                               dcc_stats.io_rate,
                               free_space_mb);
        dcc_set_nonblocking(acc_fd);
        ret = read(acc_fd, challenge, 1024); /* empty the receive queue */
        if (ret < 0) rs_log_info("read on acc_fd failed");
        dcc_writex(acc_fd, reply, reply_len);
    }

    /* Don't think we need this to prevent RST anymore, since we read() now */
#if 0
    shutdown(acc_fd, SHUT_WR); /* prevent connection reset */
#endif
    dcc_close(acc_fd);
}


/**
 * Process a packet of stats data
 **/
static void dcc_stats_process(struct statsdata *sd) {
    if (sd->type > STATS_ENUM_MAX) {
        /* Got a bad message */
        return;
    }

    switch (sd->type) {

    case STATS_TCP_ACCEPT:
    case STATS_REJ_BAD_REQ:
    case STATS_REJ_OVERLOAD:
        break;
    case STATS_COMPILE_OK:
        dcc_stats_update_compile_times(sd);
        /* fallthrough */
    case STATS_COMPILE_ERROR:
    case STATS_COMPILE_TIMEOUT:
    case STATS_CLI_DISCONN:
        /* We want to update the running compile total for all jobs that
         * non-trivially tax the CPU */
        dcc_stats_update_running_total(1);
    default: ;
    }

    dcc_stats.counters[sd->type]++;
}


/**
 * Collect runtime statistics from kids and serve them via HTTP
 * Also, maintains the pool of kids.
 **/
int dcc_stats_server(int listen_fd)
{
    int http_fd, max_fd;
    int i, ret;
    fd_set fds, fds_master;
    struct statsdata sd;
    struct timeval timeout;

    /* clear stats data */
    for (i = 0; i < STATS_ENUM_MAX; i++)
        dcc_stats.counters[i] = 0;
    dcc_stats.longest_job_time = -1;
    dcc_stats.longest_job_name[0] = 0;
    dcc_stats.io_rate = -1;

    if ((ret = dcc_socket_listen(arg_stats_port, &http_fd,
                                    opt_listen_addr)) != 0) {
        return ret;
    }
    rs_log_info("HTTP server started on port %d\n", arg_stats_port);

    /* We don't want children to inherit this FD */
    fcntl(http_fd, F_SETFD, FD_CLOEXEC);

    max_fd = (http_fd > dcc_statspipe[0]) ? (http_fd + 1)
                                           : (dcc_statspipe[0] + 1);

    FD_ZERO(&fds_master);
    FD_SET(dcc_statspipe[0], &fds_master);
    FD_SET(http_fd, &fds_master);

    while (1) {
        dcc_stats_minutely_update();
        dcc_stats_calc_kid_avg();

        timeout.tv_sec = 60;
        timeout.tv_usec = 0;
        fds = fds_master;
        ret = select(max_fd, &fds, NULL, NULL, &timeout);
        if (ret != -1) {
            if (FD_ISSET(dcc_statspipe[0], &fds)) {
                /* Received stats report from a child */
                if (read(dcc_statspipe[0], &sd, sizeof(sd)) != -1) {
                    dcc_stats_process(&sd);
                }
            }

            if (FD_ISSET(http_fd, &fds)) {
                /* Received request on stats reporting port */
                dcc_service_stats_request(http_fd);
            }
        } else {
            if (errno == EINTR) {
                /* Interrupted -- SIGCHLD? */
            }
        }

        dcc_manage_kids(listen_fd);
    }
}
