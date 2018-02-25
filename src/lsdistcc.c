/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * lsdistcc -- A simple distcc server discovery program
 * Assumes all distcc servers are in DNS and are named distcc1...distccN.
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
/* Program to autodetect listening distcc servers by looking in DNS
 * for hosts named according to a given format.
 * hosts are considered good servers based solely on whether their
 * name fits the format and whether they are listening on the right port
 * (and optionally whether they respond when you send them a compile job).
 * Stops looking for servers after the first one it doesn't find in DNS.
 * Prints results to stdout.
 * Terminates with error status if no servers found.
 *
 * Examples:
 *
 * In your build script, add the lines
 *   DISTCC_HOSTS=`lsdistcc`
 *   export DISTCC_HOSTS
 * before the line that invokes make.
 *
 * Or, in your Makefile, add the lines
 *   export DISTCC_HOSTS = $(shell lsdistcc)
 *
 * Changelog:
 *
 * Wed Jun 20 2007 - Manos Renieris, Google
 * Added -P option.
 *
 * Mon Jun  4 2007 - Manos Renieris, Google
 * Reformatted in 80 columns.
 *
 * Tue Jan 31 2006 - Dan Kegel, Google
 * Added -x option to list down hosts with ,down suffix (since
 * in sharded server cache mode, the hash space is partitioned
 * over all servers regardless of whether they're up or down at the moment)
 *
 * Thu Jan 5 2006 - Dan Kegel, Google
 * Actually read the output from the server and partially parse it.
 *
 * Sat Nov 26 2005 - Dan Kegel, Google
 * Added -l option, improved -v output
 *
 * Tue Nov 22 2005 - Dan Kegel & Dongmin Zhang, Google
 *  added -pcc option to check that server actually responds when you send
 *      it a job
 *  added -c0 option to disable connect check
 *
 * Thu Oct 13 2005 - Dan Kegel, Google
 *  use rslave to do asynchronous-ish hostname lookup, do all connects
 *      in parallel
 *
 * Wed Oct  5 2005 - Dan Kegel, Google
 *  Added -d, -m options
 *
 * Fri Sep 16 2005 - Dan Kegel, Google
 *  Created
 *  Added -v option
--------------------------------------------------------------------------*/

#include <config.h>

#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "distcc.h"
#include "clinet.h"
#include "netutil.h"
#include "util.h"
#include "trace.h"
#include "rslave.h"
#include "../lzo/minilzo.h"

/* Linux calls this setrlimit() argument NOFILE; bsd calls it OFILE */
#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE  RLIMIT_OFILE
#endif

enum status_e { STATE_LOOKUP = 0,
                STATE_CONNECT,
                STATE_CONNECTING,
                STATE_READ_DONEPKT,
                STATE_READ_STATPKT,
                STATE_READ_REST,
                STATE_CLOSE,
                STATE_DONE};

struct state_s {
    rslave_request_t req;
    rslave_result_t res;
    struct timeval start;
    struct timeval deadline;
    char curhdrbuf[12];
    int curhdrlen;
    enum status_e status;
    int ntries;
    int fd;
    int up;     /* default is 0, set to 1 on success */
};
typedef struct state_s state_t;

/* Default parameters */
#define DEFAULT_FORMAT "distcc%d"   /* hostname format */
#define DEFAULT_PORT 3632           /* TCP port to connect to */
#define DEFAULT_PROTOCOL 1          /* protocol we'll try to speak */
#define DEFAULT_BIGTIMEOUT 7        /* max total runtime, seconds */
#define DEFAULT_DNSTIMEOUT_MS 500   /* individual DNS timeout, msec */
#define DEFAULT_CONNTIMEOUT_MS 900  /* individual connect timeout, msec */
#define DEFAULT_COMPTIMEOUT_MS 1500 /* individual compile timeout, msec
                                       (FIXME: should be lower) */
#define DEFAULT_OVERLAP 1           /* number of simultaneous DNS queries -1 */
#define DEFAULT_DNSGAP 0            /* number of missing hosts in DNS before
                                       we stop looking */
#define DEFAULT_COMPILER "none"

char canned_query[1000];
size_t canned_query_len = 0;

int opt_latency = 0;
int opt_numeric = 0;
int opt_overlap = DEFAULT_OVERLAP;
int opt_dnsgap = DEFAULT_DNSGAP;
int opt_port = DEFAULT_PORT;
int opt_protocol = DEFAULT_PROTOCOL;
int opt_bigtimeout_sec = DEFAULT_BIGTIMEOUT;
int opt_conntimeout_ms = DEFAULT_CONNTIMEOUT_MS;
int opt_comptimeout_ms = DEFAULT_COMPTIMEOUT_MS;
int opt_dnstimeout_ms = DEFAULT_DNSTIMEOUT_MS;
int opt_verbose = 0;
int opt_domain = 0;
int opt_match = 0;
int opt_bang_down = 0;
const char *opt_compiler = NULL;


const char *protocol_suffix[] = { NULL, /* to make the rest 1-based */
                                  "",
                                  ",lzo",
                                  ",lzo,cpp" };

#define MAXHOSTS 500
#define MAXTRIES 5       /* this constant can't be changed without
                              changing some code */
#define MAXFDS (MAXHOSTS+2)

/* just plain globals */
int fd2state[MAXHOSTS+1000];    /* kludge - fragile */
int nok;
int ndone;

/* globals used by other compilation units */
const char *rs_program_name = "lsdistcc";

/* Forward declarations (solely to prevent compiler warnings) */
void usage(void);
int bitcompare(const unsigned char *a, const unsigned char *b, int nbits);
void timeout_handler(int x);
void get_thename(const char**sformat, const char *domain_name,
                 int i, char *thename);
int detect_distcc_servers(const char **argv, int argc, int opti,
                          int bigtimeout, int dnstimeout, int matchbits,
                          int overlap, int dnsgap);
void server_read_packet_header(state_t *sp);
void server_handle_event(state_t *sp);

void usage(void) {
        printf("Usage: lsdistcc [-tTIMEOUT] [-mBITS] [-nvd] [format]\n\
Uses 'for i=1... sprintf(format, i)' to construct names of servers,\n\
stops after %d seconds or at second server that doesn't resolve,\n\
prints the names of all such servers listening on distcc's port.\n\
Default format is %s. \n\
If a list of host names are given in the command line,\n\
lsdistcc will only check those hosts. \n\
Options:\n\
-l       Output latency in milliseconds after each hostname\n\
           (not including DNS latency)\n\
-n       Print IP address rather than name\n\
-x       Append ,down to down hosts in host list\n\
-tTIMEOUT  Set number of seconds to stop searching after [%d]\n\
-hHTIMEOUT Set number of milliseconds before retrying gethostbyname [%d]\n\
-cCTIMEOUT Set number of milliseconds before giving up on connect [%d]\n\
           (0 to inhibit connect)\n\
-kKTIMEOUT Set number of milliseconds before giving up on compile [%d]\n\
           (0 to inhibit compile)\n\
-mBITS     Set number of bits of address that must match first host found [0]\n\
-oOVERLAP  Set number of extra DNS requests to send [%d]\n\
-gDNSGAP   Set number of missing DNS entries to tolerate [%d]\n\
-rPORT     Port to connect to [%d]\n\
-PPROTOCOL Protocol version to use (1-3) [%d]\n\
-pCOMPILER Name of compiler to use [%s]\n\
-d       Append DNS domain name to format\n\
-v       Verbose\n\
\n\
Example:\n\
lsdistcc -l -p$COMPILER\n\
lsdistcc -p$COMPILER hosta somehost hostx hosty\n\
", DEFAULT_BIGTIMEOUT,
   DEFAULT_FORMAT,
   DEFAULT_BIGTIMEOUT,
   DEFAULT_DNSTIMEOUT_MS,
   DEFAULT_CONNTIMEOUT_MS,
   DEFAULT_COMPTIMEOUT_MS,
   DEFAULT_OVERLAP,
   DEFAULT_DNSGAP,
   DEFAULT_PORT,
   DEFAULT_PROTOCOL,
   DEFAULT_COMPILER);
        exit(1);
}


/* Compare first nbits of a[] and b[]
 * If nbits is 1, only compares the MSB of a[0] and b[0]
 * Return 0 on equal, nonzero on nonequal
 */
int bitcompare(const unsigned char *a, const unsigned char *b, int nbits)
{
    int fullbytes = nbits/8;
    int leftoverbits = nbits & 7;

    if (fullbytes) {
        int d = memcmp((char *)a, (char *)b, (size_t) fullbytes);
        if (d)
                return d;
    }

    if (leftoverbits) {
        int mask = 0;
        int i;
        for (i=0; i<leftoverbits; i++)
            mask |= (1 << (7-i));
        /* printf("mask %x, a[%d] %x, b[%d] %x\n", mask,
                  fullbytes, a[fullbytes], fullbytes, b[fullbytes]); */
        return ((a[fullbytes] ^ b[fullbytes]) & mask);
    }
    return 0;
}

#if 0
#include <assert.h>
main()
{
    assert(bitcompare("0", "0", 8) == 0);
    assert(bitcompare("0", "1", 8) != 0);
    assert(bitcompare("0", "1", 7) == 0);
}
#endif


/* On timeout, silently terminate program  */
void timeout_handler(int x)
{
    (void) x;

    if (opt_verbose > 0)
        fprintf(stderr, "Timeout!\n");

    /* FIXME: is it legal to call exit here? */
    exit(0);
}

static void generate_query(void)
{
    const char* program = "int foo(){return 0;}";
    unsigned char lzod_program[1000];
    unsigned char lzo_work_mem[LZO1X_1_MEM_COMPRESS];
    lzo_uint lzod_program_len;

    lzo1x_1_compress((const unsigned char *)program, strlen(program),
                     lzod_program, &lzod_program_len,
                     lzo_work_mem);

    switch (opt_protocol) {
    case 1: {
        static const char canned_query_fmt_protocol_1[]=
                                      "DIST00000001"
                                      "ARGC00000005"
                                      "ARGV%08x%s"
                                      "ARGV00000002-c"
                                      "ARGV00000007hello.c"
                                      "ARGV00000002-o"
                                      "ARGV00000007hello.o"
                                      "DOTI%08x%s";
        sprintf(canned_query,
                canned_query_fmt_protocol_1,
                 (unsigned)strlen(opt_compiler), opt_compiler,
                 (unsigned)strlen(program), program);
        canned_query_len = strlen(canned_query);
        break;
    }

     case 2: {
        static const char canned_query_fmt_protocol_2[]=
                                      "DIST00000002"
                                      "ARGC00000005"
                                      "ARGV%08x%s"
                                      "ARGV00000002-c"
                                      "ARGV00000007hello.c"
                                      "ARGV00000002-o"
                                      "ARGV00000007hello.o"
                                      "DOTI%08x";
        sprintf(canned_query,
                canned_query_fmt_protocol_2,
                 (unsigned)strlen(opt_compiler),
                 opt_compiler,
                 (unsigned)lzod_program_len);

        canned_query_len = strlen(canned_query) + lzod_program_len;
        memcpy(canned_query + strlen(canned_query),
               lzod_program, lzod_program_len);

        break;
      }

     case 3: {
        static const char canned_query_fmt_protocol_3[]=
                                      "DIST00000003"
                                      "CDIR00000001/"
                                      "ARGC00000005"
                                      "ARGV%08x%s"
                                      "ARGV00000002-c"
                                      "ARGV00000007hello.c"
                                      "ARGV00000002-o"
                                      "ARGV00000007hello.o"
                                      "NFIL00000001"
                                      "NAME00000008/hello.c"
                                      "FILE%08x";

        sprintf(canned_query,
                canned_query_fmt_protocol_3,
                 (unsigned)strlen(opt_compiler),
                 opt_compiler,
                 (unsigned)lzod_program_len);

        canned_query_len = strlen(canned_query) + lzod_program_len;
        memcpy(canned_query + strlen(canned_query),
               lzod_program, lzod_program_len);
        break;
      }
    }
}

/* Try reading a protocol packet header */
void server_read_packet_header(state_t *sp)
{
    int arg;
    int nread;

    nread = read(sp->fd, sp->curhdrbuf + sp->curhdrlen,
                 (size_t)(12 - sp->curhdrlen));
    if (nread == 0) {
        /* A nonblocking read returning zero bytes means EOF.
         * FIXME: it may mean this only on the first read after poll said
         * bytes were ready, so beware of false EOFs here?
         */
        if (opt_verbose > 0)
            fprintf(stderr, "lsdistcc: premature EOF while waiting for "
                            "result from server %s\n",
                    sp->req.hname);
        sp->status = STATE_CLOSE;
        return;
    }

    if (nread > 0)
        sp->curhdrlen += nread;

    if (sp->curhdrlen < 12)
        return;

    arg = (int)strtol(sp->curhdrbuf+4, NULL, 16);

    if (opt_verbose > 2) {
        int i;
        printf("Got hdr '%12.12s' = ", sp->curhdrbuf);
        for (i=0; i < sp->curhdrlen; i++)
                printf("%2x", sp->curhdrbuf[i]);
        printf("\n");
    }

    /* Parse and validate the packet header, move on to next state */
    switch (sp->status) {
    case STATE_READ_DONEPKT:
        if (memcmp(sp->curhdrbuf, "DONE", 4) != 0) {
            if (opt_verbose > 1)
                fprintf(stderr,
                        "%s wrong protocol; expected DONE, got %4.4s!\n",
                        sp->req.hname, sp->curhdrbuf);
            sp->status = STATE_CLOSE;
            break;
        }
        if (arg != opt_protocol) {
            if (opt_verbose > 1)
                fprintf(stderr,
                        "%s wrong protocol, expected %d got %d!\n",
                        sp->req.hname,
                        opt_protocol,
                        arg);
            sp->status = STATE_CLOSE;
            break;
        }
        /* No body to this type.  Read next packet. */
        sp->curhdrlen = 0;
        sp->status = STATE_READ_STATPKT;
        break;

    case STATE_READ_STATPKT:
        if (memcmp(sp->curhdrbuf, "STAT", 4) != 0) {
            if (opt_verbose > 1)
                fprintf(stderr,
                        "%s wrong protocol!  Expected STAT, got %4.4s\n",
                        sp->req.hname, sp->curhdrbuf);
            sp->status = STATE_CLOSE;
            break;
        }
        if (arg != 0) {
            if (opt_verbose > 1) {
            /* FIXME: only conditional because my server uses load shedding */
                fprintf(stderr,
                        "lsdistcc: warning: test compile on %s failed! "
                        "status 0x%x\n",
                        sp->req.hname, arg);
            }
            sp->status = STATE_CLOSE;
            break;
        }
        /* No body to this type.  Read next packet. */
        sp->curhdrlen = 0;
        sp->status = STATE_READ_REST;
        break;

    default:
        fprintf(stderr, "bug\n");
        exit(1);
    }
}

/* Grind state machine for a single server */
/* Take one transition through the state machine, unless that takes you
   to STATE_CLOSE, in which case go through that state too, into STATE_DONE
 */

void server_handle_event(state_t *sp)
{
    struct timeval now;
    gettimeofday(&now, 0);

    do {
        struct sockaddr_in sa;

        if (opt_verbose > 2)
            fprintf(stderr,
                    "now %lld %ld: server_handle_event: %s: state %d\n",
                    (long long) now.tv_sec, (long) now.tv_usec/1000,
                    sp->req.hname, sp->status);

        switch (sp->status) {
        case STATE_CONNECT:
            if (opt_conntimeout_ms == 0) {
                sp->fd = -1;
                sp->up = 1;
                sp->status = STATE_CLOSE;
                break;
            }

            /* Now do a nonblocking connect to that address */
            memset(&sa, 0, sizeof sa);
            sa.sin_family = AF_INET;
            sa.sin_port = htons(opt_port);
            memcpy(&sa.sin_addr, sp->res.addr, 4);

            if ((sp->fd = socket(sa.sin_family, SOCK_STREAM, 0)) == -1) {
                fprintf(stderr, "failed to create socket: %s", strerror(errno));
                sp->status = STATE_DONE;
            } else {
                dcc_set_nonblocking(sp->fd);
                /* start the nonblocking connect... */
                if (opt_verbose > 0)
                    fprintf(stderr,
                            "now %lld %ld: Connecting to %s\n",
                            (long long) now.tv_sec, (long) now.tv_usec/1000,
                            sp->req.hname);
                if (connect(sp->fd, (struct sockaddr *)&sa, sizeof(sa))
                    && errno != EINPROGRESS) {
                    if (opt_verbose > 0)
                        fprintf(stderr, "failed to connect socket: %s",
                        strerror(errno));
                    sp->status = STATE_CLOSE;
                } else {
                    sp->status = STATE_CONNECTING;
                    fd2state[sp->fd] = sp->res.id;
                    gettimeofday(&now, 0);
                    sp->start = now;
                    sp->deadline = now;
                    sp->deadline.tv_usec += 1000 * opt_conntimeout_ms;
                    sp->deadline.tv_sec += sp->deadline.tv_usec / 1000000;
                    sp->deadline.tv_usec = sp->deadline.tv_usec % 1000000;
                }
            }
            break;
        case STATE_CONNECTING:
            {
                int connecterr;
                socklen_t len = sizeof(connecterr);
                int nsend;
                int nsent;

                if (getsockopt(sp->fd, SOL_SOCKET, SO_ERROR,
                               (char *)&connecterr, &len) < 0) {
                    fprintf(stderr, "getsockopt SO_ERROR failed?!");
                    sp->status = STATE_CLOSE;
                    break;
                }
                if (connecterr) {
                    if (opt_verbose > 0)
                       fprintf(stderr,
                               "now %lld %ld: Connecting to %s failed "
                               "with errno %d = %s\n",
                         (long long) now.tv_sec, (long) now.tv_usec/1000,
                         sp->req.hname, connecterr, strerror(connecterr));
                    sp->status = STATE_CLOSE;   /* not listening */
                    break;
                }
                if (opt_comptimeout_ms == 0 || !opt_compiler) {
                    /* connect succeeded, don't need to compile */
                    sp->up = 1;
                    sp->status = STATE_CLOSE;
                    break;
                }
                if (opt_verbose > 0)
                    fprintf(stderr,
                            "now %lld %ld: %s: sending compile request\n",
                            (long long) now.tv_sec, (long) now.tv_usec/1000,
                            sp->req.hname);
                nsend = canned_query_len;
                nsent = write(sp->fd, canned_query, nsend);
                if (nsent != nsend) {
                    if (opt_verbose > 1) {
                        if (nsent == -1)
                            fprintf(stderr,
                                    "now %lld %ld: Sending to %s failed, "
                                    "errno %d\n",
                                    (long long) now.tv_sec, (long) now.tv_usec/1000,
                                    sp->req.hname, connecterr);
                        else
                            fprintf(stderr,
                                    "now %lld %ld: Sending to %s failed, "
                                    "nsent %d != nsend %d\n",
                                    (long long) now.tv_sec, (long) now.tv_usec/1000,
                                    sp->req.hname, nsent, nsend);
                    }
                    /* ??? remote disconnect?  Buffer too small? */
                    sp->status = STATE_CLOSE;
                    break;
                }
                sp->status=STATE_READ_DONEPKT;
                sp->curhdrlen = 0;
                sp->deadline = now;
                sp->deadline.tv_usec += 1000 * opt_comptimeout_ms;
                sp->deadline.tv_sec += sp->deadline.tv_usec / 1000000;
                sp->deadline.tv_usec = sp->deadline.tv_usec % 1000000;
            }
            break;

        case STATE_READ_DONEPKT:
        case STATE_READ_STATPKT:
            server_read_packet_header(sp);
            break;

        case STATE_READ_REST:
          {
            char buf[1000];
            int nread;
            nread = read(sp->fd, buf, sizeof(buf));
            if (nread == 0) {
                /* A nonblocking read returning zero bytes means EOF.
                 * FIXME: it may mean this only on the first read after
                 * poll said bytes were ready, so beware of false EOFs here?
                 */
                sp->up = 1;
                sp->status = STATE_CLOSE;
            }
          }
          break;

        case STATE_CLOSE:
            if (sp->fd != -1) {
                close(sp->fd);
                sp->fd = -1;
            }

            if (opt_bang_down || sp->up) {
                if (opt_numeric)
                    printf("%d.%d.%d.%d", sp->res.addr[0], sp->res.addr[1],
                           sp->res.addr[2], sp->res.addr[3]);
                else
                    printf("%s", sp->req.hname);

                if (opt_port != DEFAULT_PORT)
                    printf(":%d", opt_port);

                printf("%s", protocol_suffix[opt_protocol]);

                if (opt_bang_down && !sp->up)
                    printf(",down");

                if (opt_latency) {
                    int latency_ms;
                    gettimeofday(&now, 0);
                    latency_ms = (now.tv_usec - sp->start.tv_usec) /
                                 1000 + 1000 * (now.tv_sec - sp->start.tv_sec);
                    printf(" %d", latency_ms);
                }
                putchar('\n');
                if (opt_verbose)
                    fflush(stdout);
            }
            nok++;
            sp->status = STATE_DONE;
            ndone++;
            break;

        case STATE_DONE:
            ;
        default:
            ;
        }
    } while (sp->status == STATE_CLOSE);
}

/* A helper function for detecting all listening distcc servers: this
 * routine makes one pass through the poll() loop and analyzes what it
 * sees.
 */
static int one_poll_loop(struct rslave_s* rs, struct state_s states[],
                         int start_state, int end_state,
                         int nwithtries[], int* ngotaddr, int* nbaddns,
                         unsigned char firstipaddr[4], int dnstimeout_usec,
                         int matchbits, int overlap, int dnsgap)
{
    int i;
    int nfds;
    struct state_s *sp;
    int nready;
    int found;
    struct timeval now;
    struct pollfd pollfds[MAXFDS];

    /* See which sockets have any events */
    nfds = 0;
    memset(pollfds, 0, sizeof(pollfds));
    pollfds[nfds].fd = rslave_getfd_fromSlaves(rs);
    pollfds[nfds++].events = POLLIN;
    pollfds[nfds].fd = rslave_getfd_toSlaves(rs);
    /* Decide if we want to be notified if slaves are ready to handle
     * a DNS request.
     * To avoid sending too many DNS requests, we avoid sending more if
     * the number of first tries is greater than 'overlap'
     * or the number of outstanding DNS requests plus the number of
     * already satisfied ones would be greater than or equal to the max
     * number of hosts we're looking for.
     */
    pollfds[nfds++].events = ((nwithtries[1] <= overlap) &&
                              (nwithtries[1]+
                               nwithtries[2]+
                               nwithtries[3]+
                               nwithtries[4]+
                               *ngotaddr < end_state)) ? POLLOUT : 0;
    /* Set interest bits.
     * When connecting, we want to know if we can write (aka if the
     * connect has finished); when waiting for a compile to finish,
     * we want to know if we can read.
     */
    for (i=start_state; i<=end_state; i++) {
        switch (states[i].status) {
        case STATE_CONNECTING:
            pollfds[nfds].fd = states[i].fd;
            pollfds[nfds++].events = POLLOUT;
            break;
        case STATE_READ_DONEPKT:
        case STATE_READ_STATPKT:
        case STATE_READ_REST:
            pollfds[nfds].fd = states[i].fd;
            pollfds[nfds++].events = POLLIN;
            break;
        default: ;
        }
    }
    /* When polling, wait for no more than 50 milliseconds.
     * Anything lower doesn't help performance much.
     * Anything higher would inflate all our timeouts,
     * cause retries not to be sent as soon as they should,
     * and make the program take longer than it should.
     */
    nready = poll(pollfds, (unsigned)nfds, 50);
    if (nready == -1) {
	fprintf(stderr, "lsdistcc: poll failed: %s\n", strerror(errno));
	exit(1);
    }
    gettimeofday(&now, 0);


    /***** Check for timeout events *****/
    sp = NULL;
    found = FALSE;
    for (i=start_state; i<=end_state; i++) {
        sp = &states[i];
        if (sp->status == STATE_LOOKUP
            && sp->ntries > 0 && sp->ntries < MAXTRIES
            && (sp->deadline.tv_sec < now.tv_sec ||
                (sp->deadline.tv_sec == now.tv_sec &&
                 sp->deadline.tv_usec < now.tv_usec))) {
            found = TRUE;
            nwithtries[sp->ntries]--;
            sp->ntries++;
            nwithtries[sp->ntries]++;
            if (opt_verbose > 0)
                fprintf(stderr,
                        "now %lld %ld: Resending %s because "
                        "deadline was %lld %ld\n",
                        (long long) now.tv_sec, (long) now.tv_usec/1000,
                        sp->req.hname, (long long) sp->deadline.tv_sec,
                        (long) sp->deadline.tv_usec/1000);
            break;
        }

        if (sp->status == STATE_CONNECTING
            && (sp->deadline.tv_sec < now.tv_sec ||
                (sp->deadline.tv_sec == now.tv_sec &&
                 sp->deadline.tv_usec < now.tv_usec))) {
            sp->status = STATE_CLOSE;
            server_handle_event(sp);
            if (opt_verbose > 0)
                fprintf(stderr,
                        "now %lld %ld: %s timed out while connecting\n",
                        (long long) now.tv_sec, (long) now.tv_usec/1000,
                        sp->req.hname);
        }
        if ((sp->status == STATE_READ_DONEPKT ||
             sp->status == STATE_READ_STATPKT ||
             sp->status == STATE_READ_REST)
            && (sp->deadline.tv_sec < now.tv_sec ||
                (sp->deadline.tv_sec == now.tv_sec &&
                 sp->deadline.tv_usec < now.tv_usec))) {
            sp->status = STATE_CLOSE;
            server_handle_event(sp);
            if (opt_verbose > 0)
                fprintf(stderr,
                        "now %lld %ld: %s timed out while compiling\n",
                        (long long) now.tv_sec, (long) now.tv_usec/1000,
                        sp->req.hname);
        }
    }
    if (!found && (nwithtries[1] <= overlap) &&
        (pollfds[1].revents & POLLOUT)) {
        /* Look for a fresh record to send */
        for (i=start_state; i<=end_state; i++) {
            sp = &states[i];
            if (sp->status == STATE_LOOKUP && sp->ntries == 0) {
                found = TRUE;
                nwithtries[sp->ntries]--;
                sp->ntries++;
                nwithtries[sp->ntries]++;
                break;
            }
        }
    }
    /* If we found a record to send or resend, send it,
       and mark its timeout. */
    if (found) {
        if (opt_verbose)
            fprintf(stderr, "now %lld %ld: Looking up %s\n",
                    (long long) now.tv_sec, (long) now.tv_usec/1000,
		    sp->req.hname);
        rslave_writeRequest(rs, &sp->req);
        sp->deadline = now;
        sp->deadline.tv_usec += dnstimeout_usec;
        sp->deadline.tv_sec += sp->deadline.tv_usec / 1000000;
        sp->deadline.tv_usec = sp->deadline.tv_usec % 1000000;
    }

    /***** Check poll results for DNS results *****/
    if (pollfds[0].revents & POLLIN) {
        /* A reply is ready, huzzah! */
        rslave_result_t result;
        if (rslave_readResult(rs, &result)) {
            printf("bug: can't read from pipe\n");
        } else {
            /* Find the matching state_t, save the result,
               and mark it as done */
            /* printf("result.id %d\n", result.id); fflush(stdout); */
            assert(result.id >= start_state && result.id <= end_state);
            sp = &states[result.id];
            if (sp->status == STATE_LOOKUP) {
                nwithtries[sp->ntries]--;
                sp->res = result;
                (*ngotaddr)++;
                if (matchbits > 0) {
                    if (*ngotaddr == 1) {
                        memcpy(firstipaddr, result.addr, 4);
                    } else {
                        /* break if new server on a 'different network'
                         than first server */
                        if (bitcompare(firstipaddr, result.addr, matchbits))
                            result.err = -1;
                    }
                }

                if (result.err) {
                    if (opt_verbose)
                        fprintf(stderr, "now %lld %ld: %s not found\n",
                                (long long) now.tv_sec, (long) now.tv_usec/1000,
                                sp->req.hname);
                    sp->status = STATE_DONE;
                    ndone++;
                    (*nbaddns)++;
                    if (*nbaddns > dnsgap) {
                        int highest = 0;
                        /* start no more lookups */
                        for (i=start_state; i <= end_state; i++)
                            if (states[i].ntries > 0)
                                highest = i;
                        assert(highest <= end_state);
                        if (opt_verbose && end_state != highest)
                            fprintf(stderr,
                                    "Already searching up to host %d, "
                                    "won't search any higher\n",
                                    highest);
                        end_state = highest;
                        assert(end_state <= MAXHOSTS);
                    }
                } else {
                    sp->status = STATE_CONNECT;
                    server_handle_event(sp);
                }
            }
        }
    }

    /***** Grind state machine for each remote server *****/
    for (i=2; i<nfds && i < MAXFDS; i++) {
        sp = states + fd2state[pollfds[i].fd];  /* FIXME */
        if (pollfds[i].revents)
            server_handle_event(sp);
    }
    return end_state;
}

/* Get the name based on the sformat. If the first element in sformat is a
 * format, ignore the rest, and use the format to generate the series of names;
 * otherwise, copy the name from sformat. Attach domain_name if needed.
 */
void get_thename(const char**sformat, const char *domain_name, int i,
                char *thename)
{
    if (strstr(sformat[0], "%d") != NULL)
        sprintf(thename, sformat[0], i);
    else
        strcpy(thename, sformat[i-1]);
    if (opt_domain) {
        strcat(thename, ".");
        strcat(thename, domain_name);
    }
}


/* Detect all listening distcc servers and print their names to stdout.
 * Looks for servers numbered 1 through infinity, stops at
 * first server that doesn't resolve in DNS, or after 'timeout' seconds,
 * whichever comes first.
 * On entry:
 *  sformat: format of names of distcc servers to check
 *  bigtimeout: how many seconds to terminate slow run after
 *  dnstimeout: how many milliseconds before retrying a particular
 *            gethostbyname call
 *  matchbits: top matchbits of address must match first host found,
               else stop searching
 *  overlap: how many extra DNS queries to keep in flight normally
 *  dnsgap: how many missing DNS entries to tolerate
 * On exit:
 *  returns number of servers found.
 */
int detect_distcc_servers(const char **argv, int argc, int opti,
                          int bigtimeout, int dnstimeout,
                          int matchbits, int overlap, int dnsgap)
{
    unsigned char firstipaddr[4];
    int dnstimeout_usec = dnstimeout * 1000;   /* how long before
                                                  resending gethostbyname */
    int i;
    int n = MAXHOSTS;
    int maxfds = MAXHOSTS + 10;
    char thename[256];

    struct state_s states[MAXHOSTS+1];
    int start_state, end_state;
    int ngotaddr;
    int nbaddns;
    int nwithtries[MAXTRIES+1];

    struct rslave_s rs;

    const char *default_format = DEFAULT_FORMAT;
    const char **sformat = &default_format;
    const char *domain_name;
    if (opt_domain) {
        if (dcc_get_dns_domain(&domain_name)) {
                fprintf(stderr, "Can't get domain name\n");
                exit(1);
        }
    }
    if (opti < argc) {
        if (strstr(argv[opti], "%d") != NULL) {
            sformat = &argv[opti++];
        } else {
            /* A list of host names can be given in the command line */
            n = argc-opti;
            sformat = &argv[opti++];
        }
    }

    /* Figure out the limit on the number of fd's we can open, as per
     * the OS.  We allow 8 fds for uses other than this in the program
     * (eg stdin, stdout).  If possible, ask the OS for more fds.
     * We'll ideally use n + 2 fds in our poll loop, so ask for n + 10
     * fds total.
     */
    struct rlimit rlim = {0, 0};
    getrlimit(RLIMIT_NOFILE, &rlim);
    if (rlim.rlim_cur < (rlim_t)n + 10) {
        rlim.rlim_cur = (rlim_t)n + 10;
        if (rlim.rlim_cur > rlim.rlim_max)
            rlim.rlim_cur = rlim.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rlim);
        getrlimit(RLIMIT_NOFILE, &rlim);
        if (rlim.rlim_cur > 14)
           maxfds = (int)(rlim.rlim_cur - 10);
    }

    /* Don't run longer than bigtimeout seconds */
    signal(SIGALRM, timeout_handler);
    alarm((unsigned) bigtimeout);

    if (rslave_init(&rs))
        return 0;

    ngotaddr = 0;
    memset(nwithtries, 0, sizeof(nwithtries));
    memset(states, 0, sizeof(states));

    /* all hosts start off in state 'sent 0' */
    for (i=1; i<=n; i++) {
        rslave_request_t *req = &states[i].req;
        get_thename(sformat, domain_name, i, thename);
        rslave_request_init(req, thename, i);
        states[i].status = STATE_LOOKUP;
        states[i].ntries = 0;
        nwithtries[0]++;
    }

    ndone = 0;
    nok = 0;
    nbaddns = 0;
    /* Loop until we're done finding distcc servers.  We have to do
     * this loop in groups, with each group using no more than maxfds
     * fd's.  One call to one_poll_loop uses n + 2 fds.
     */
    for (start_state = 1; start_state <= n; start_state = end_state + 1) {
        int orig_end_state;
        end_state = start_state + maxfds-2;
        if (end_state > n)
            end_state = n;
        orig_end_state = end_state;
        while (ndone < end_state) {
            end_state = one_poll_loop(&rs, states, start_state, end_state,
                                      nwithtries, &ngotaddr, &nbaddns,
                                      firstipaddr, dnstimeout_usec,
                                      matchbits, overlap, dnsgap);
        }
        if (end_state < orig_end_state) {
            /* If we lowered end_state, it means we decided to stop
             * searching early.
             */
            break;
        }
    }
    return nok;
}

int main(int argc, char **argv)
{
    int opti;
    int nfound;

    for (opti = 1; opti < argc && argv[opti][0] == '-'; opti++) {
        switch (argv[opti][1]) {
        case 'm':
            opt_match = atoi(argv[opti]+2);
            if (opt_match > 31 || opt_match < 0)
                usage();
            break;
        case 't':
            opt_bigtimeout_sec = atoi(argv[opti]+2);
            if (opt_bigtimeout_sec < 0)
                usage();
            break;
        case 'h':
            opt_dnstimeout_ms = atoi(argv[opti]+2);
            if (opt_dnstimeout_ms < 0)
                usage();
            break;
        case 'c':
            opt_conntimeout_ms = atoi(argv[opti]+2);
            if (opt_conntimeout_ms < 0)
                usage();
            break;
        case 'k':
            opt_comptimeout_ms = atoi(argv[opti]+2);
            if (opt_comptimeout_ms < 0)
                usage();
            break;
        case 'o':
            opt_overlap = atoi(argv[opti]+2);
            if (opt_overlap < 0)
                usage();
            break;
        case 'g':
            opt_dnsgap = atoi(argv[opti]+2);
            if (opt_dnsgap < 0)
                usage();
            break;
        case 'P':
            opt_protocol = atoi(argv[opti]+2);
            if (opt_protocol <= 0 || opt_protocol > 3) {
                usage();
            }
            break;
        case 'p':
            opt_compiler = argv[opti]+2;
            if (! *opt_compiler)
                usage();
            break;
        case 'r':
            opt_port = atoi(argv[opti]+2);
            if (opt_port <= 0)
                usage();
            break;
        case 'l':
            opt_latency = 1;
            break;
        case 'n':
            opt_numeric = 1;
            break;
        case 'x':
            opt_bang_down = 1;
            break;
        case 'v':
            opt_verbose++;
            break;
        case 'd':
            opt_domain++;
            break;
        default:
            usage();
        }
    }

    if (opt_compiler)
        generate_query();

    nfound = detect_distcc_servers((const char **)argv, argc, opti,
                                   opt_bigtimeout_sec,
                                   opt_dnstimeout_ms,
                                   opt_match,
                                   opt_overlap,
                                   opt_dnsgap);

    /* return failure if no servers found */
    return (nfound > 0) ? 0 : 1;
}
