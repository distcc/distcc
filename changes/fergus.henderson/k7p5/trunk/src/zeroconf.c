#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

#include <avahi-common/domain.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/address.h>
#include <avahi-common/simple-watch.h>
#include <avahi-client/lookup.h>

#include "distcc.h"
#include "hosts.h"
#include "zeroconf.h"
#include "trace.h"
#include "exitcode.h"

/* How long shall the background daemon be idle before i terminates itself? */
#define MAX_IDLE_TIME 20

/* Maxium size of host file to load */
#define MAX_FILE_SIZE (1024*100)

/* General daemon data */
struct daemon_data {
    struct host *hosts;
    int fd;
    int n_slots;

    AvahiClient *client;
    AvahiServiceBrowser *browser;
    AvahiSimplePoll *simple_poll;
};

/* Zeroconf service wrapper */
struct host {
    struct daemon_data *daemon_data;
    struct host *next;

    AvahiIfIndex interface;
    AvahiProtocol protocol;
    char *service;
    char *domain;

    AvahiAddress address;
    uint16_t port;
    int n_cpus;

    AvahiServiceResolver *resolver;
};

/* A generic, system independant lock routine, similar to sys_lock,
 * but more powerful:
 *        rw:         if non-zero: r/w lock instead of r/o lock
 *        enable:     lock or unlock
 *        block:      block when locking */
static int generic_lock(int fd, int rw, int enable, int block) {
#if defined(F_SETLK)
    struct flock lockparam;

    lockparam.l_type = enable ? (rw ? F_WRLCK : F_RDLCK) : F_UNLCK;
    lockparam.l_whence = SEEK_SET;
    lockparam.l_start = 0;
    lockparam.l_len = 0;        /* whole file */

    return fcntl(fd, block ? F_SETLKW : F_SETLK, &lockparam);
#elif defined(HAVE_FLOCK)
    return flock(fd, (enable ? (rw ? LOCK_EX : LOCK_SH) : LOCK_UN) | (block ? LOCK_NB : 0));
#elif defined(HAVE_LOCKF)
    return lockf(fd, (enable ? (block ? F_LOCK : F_TLOCK) : F_ULOCK));
#else
#  error "No supported lock method.  Please port this code."
#endif
}

/* Return the number of seconds, when the specified file was last
 * read. If the atime of that file is < clip_time, use clip_time
 * instead */
static time_t fd_last_used(int fd, time_t clip_time) {
    struct stat st;
    time_t now, ft;
    assert(fd >= 0);

    if (fstat(fd, &st) < 0) {
        rs_log_crit("fstat() failed: %s\n", strerror(errno));
        return -1;
    }

    if ((now = time(NULL)) == (time_t) -1) {
        rs_log_crit("time() failed: %s\n", strerror(errno));
        return -1;
    }

    ft = clip_time ? (st.st_atime < clip_time ? clip_time : st.st_atime) : st.st_atime;
    assert(ft <= now);

    return now - ft;
}

/* Write host data to host file */
static int write_hosts(struct daemon_data *d) {
    struct host *h;
    int r = 0;
    assert(d);

    rs_log_info("writing zeroconf data.\n");

    if (generic_lock(d->fd, 1, 1, 1) < 0) {
        rs_log_crit("lock failed: %s\n", strerror(errno));
        return -1;
    }

    if (lseek(d->fd, 0, SEEK_SET) < 0) {
        rs_log_crit("lseek() failed: %s\n", strerror(errno));
        return -1;
    }

    if (ftruncate(d->fd, 0) < 0) {
        rs_log_crit("ftruncate() failed: %s\n", strerror(errno));
        return -1;
    }

    for (h = d->hosts; h; h = h->next) {
        char t[256], a[AVAHI_ADDRESS_STR_MAX];

        if (h->resolver)
            /* Not yet fully resolved */
            continue;

        snprintf(t, sizeof(t), "%s:%u/%i\n", avahi_address_snprint(a, sizeof(a), &h->address), h->port, d->n_slots * h->n_cpus);

        if (dcc_writex(d->fd, t, strlen(t)) != 0) {
            rs_log_crit("write() failed: %s\n", strerror(errno));
            goto finish;
        }
    }

    r = 0;

finish:

    generic_lock(d->fd, 1, 0, 1);
    return r;

};

/* Free host data */
static void free_host(struct host *h) {
    assert(h);

    if (h->resolver)
        avahi_service_resolver_free(h->resolver);

    free(h->service);
    free(h->domain);
    free(h);
}

/* Remove a service from the host list */
static void remove_service(struct daemon_data *d, AvahiIfIndex interface, AvahiProtocol protocol, const char *name, const char *domain) {
    struct host *h, *p = NULL;
    assert(d);

    for (h = d->hosts; h; h = h->next) {
        if (h->interface == interface &&
            h->protocol == protocol &&
            !strcmp(h->service, name) &&
            avahi_domain_equal(h->domain, domain)) {

            if (p)
                p->next = h->next;
            else
                d->hosts = h->next;

            free_host(h);

            break;
        } else
            p = h;
    }
}

/* Called when a resolve call completes */
static void resolve_reply(
        AvahiServiceResolver *UNUSED(r),
        AvahiIfIndex UNUSED(interface),
        AvahiProtocol UNUSED(protocol),
        AvahiResolverEvent event,
        const char *name,
        const char *UNUSED(type),
        const char *UNUSED(domain),
        const char *UNUSED(host_name),
        const AvahiAddress *a,
        uint16_t port,
        AvahiStringList *txt,
        AvahiLookupResultFlags UNUSED(flags),
        void *userdata) {

    struct host *h = userdata;

    switch (event) {

        case AVAHI_RESOLVER_FOUND: {
            AvahiStringList *i;

            /* Look for the number of CPUs in TXT RRs */
            for (i = txt; i; i = i->next) {
                char *key, *value;

                if (avahi_string_list_get_pair(i, &key, &value, NULL) < 0)
                    continue;

                if (!strcmp(key, "cpus"))
                    if ((h->n_cpus = atoi(value)) <= 0)
                        h->n_cpus = 1;

                avahi_free(key);
                avahi_free(value);
            }

            h->address = *a;
            h->port = port;

            avahi_service_resolver_free(h->resolver);
            h->resolver = NULL;

            /* Write modified hosts file */
            write_hosts(h->daemon_data);

            break;
        }

        case AVAHI_RESOLVER_FAILURE:

            rs_log_warning("Failed to resolve service '%s': %s\n", name,
                           avahi_strerror(avahi_client_errno(h->daemon_data->client)));

            free_host(h);
            break;
    }

}

/* Called whenever a new service is found or removed */
static void browse_reply(
        AvahiServiceBrowser *UNUSED(b),
        AvahiIfIndex interface,
        AvahiProtocol protocol,
        AvahiBrowserEvent event,
        const char *name,
        const char *type,
        const char *domain,
        AvahiLookupResultFlags UNUSED(flags),
        void *userdata) {

    struct daemon_data *d = userdata;
    assert(d);

    switch (event) {
        case AVAHI_BROWSER_NEW: {
            struct host *h;

            h = malloc(sizeof(struct host));
            assert(h);

            rs_log_info("new service: %s\n", name);

            if (!(h->resolver = avahi_service_resolver_new(d->client,
                                                           interface,
                                                           protocol,
                                                           name,
                                                           type,
                                                           domain,
                                                           AVAHI_PROTO_UNSPEC,
                                                           0,
                                                           resolve_reply,
                                                           h))) {
                rs_log_warning("Failed to create service resolver for '%s': %s\n", name,
                               avahi_strerror(avahi_client_errno(d->client)));

                free(h);

            } else {

                /* Fill in missing data */
                h->service = strdup(name);
                assert(h->service);
                h->domain = strdup(domain);
                assert(h->domain);
                h->daemon_data = d;
                h->interface = interface;
                h->protocol = protocol;
                h->next = d->hosts;
                h->n_cpus = 1;
                d->hosts = h;
            }

            break;
        }

        case AVAHI_BROWSER_REMOVE:

            rs_log_info("Removed service: %s\n", name);

            remove_service(d, interface, protocol, name, domain);
            write_hosts(d);
            break;

        case AVAHI_BROWSER_FAILURE:
            rs_log_crit("Service Browser failure '%s': %s\n", name,
                        avahi_strerror(avahi_client_errno(d->client)));

            avahi_simple_poll_quit(d->simple_poll);
            break;

        case AVAHI_BROWSER_CACHE_EXHAUSTED:
        case AVAHI_BROWSER_ALL_FOR_NOW:
            ;

    }
}

static void client_callback(AvahiClient *client, AvahiClientState state, void *userdata) {
    struct daemon_data *d = userdata;

    switch (state) {

        case AVAHI_CLIENT_FAILURE:
            rs_log_crit("Client failure: %s\n", avahi_strerror(avahi_client_errno(client)));
            avahi_simple_poll_quit(d->simple_poll);
            break;

        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_REGISTERING:
        case AVAHI_CLIENT_S_RUNNING:
        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}

/* The main function of the background daemon */
static int daemon_proc(const char *host_file, const char *lock_file, int n_slots) {
    int ret = 1;
    int lock_fd = -1;
    struct daemon_data d;
    time_t clip_time;
    int error;
    char machine[64], version[64], stype[128];

    rs_add_logger(rs_logger_syslog, RS_LOG_DEBUG, NULL, 0);

    /* Prepare daemon data structure */
    d.fd = -1;
    d.hosts = NULL;
    d.n_slots = n_slots;
    d.simple_poll = NULL;
    d.browser = NULL;
    d.client = NULL;
    clip_time = time(NULL);

    rs_log_info("Zeroconf daemon running.\n");

    /* Open daemon lock file and lock it */
    if ((lock_fd = open(lock_file, O_RDWR|O_CREAT, 0666)) < 0) {
        rs_log_crit("open('%s') failed: %s\n", lock_file, strerror(errno));
        goto finish;
    }

    if (generic_lock(lock_fd, 1, 1, 0) < 0) {
        /* lock failed, there's probably already another daemon running */
        goto finish;
    }

    /* Open host file */
    if ((d.fd = open(host_file, O_RDWR|O_CREAT, 0666)) < 0) {
        rs_log_crit("open('%s') failed: %s\n", host_file, strerror(errno));
        goto finish;
    }

    /* Clear host file */
    write_hosts(&d);

    if (!(d.simple_poll = avahi_simple_poll_new())) {
        rs_log_crit("Failed to create simple poll object.\n");
        goto finish;
    }

    if (!(d.client = avahi_client_new(
                  avahi_simple_poll_get(d.simple_poll),
                  0,
                  client_callback,
                  &d,
                  &error))) {
        rs_log_crit("Failed to create Avahi client object: %s\n", avahi_strerror(error));
        goto finish;
    }

    if (dcc_get_gcc_version(version, sizeof(version)) &&
        dcc_get_gcc_machine(machine, sizeof(machine))) {

        dcc_make_dnssd_subtype(stype, sizeof(stype), version, machine);
    } else {
        rs_log_warning("Warning, failed to get CC version and machine type.\n");

        strncpy(stype, DCC_DNS_SERVICE_TYPE, sizeof(stype));
        stype[sizeof(stype)-1] = 0;
    }

    rs_log_info("Browsing for '%s'.\n", stype);

    if (!(d.browser = avahi_service_browser_new(
                  d.client,
                  AVAHI_IF_UNSPEC,
                  AVAHI_PROTO_UNSPEC,
                  stype,
                  NULL,
                  0,
                  browse_reply,
                  &d))) {
        rs_log_crit("Failed to create service browser object: %s\n", avahi_strerror(avahi_client_errno(d.client)));
        goto finish;
    }

    /* Check whether the host file has been used recently */
    while (fd_last_used(d.fd, clip_time) <= MAX_IDLE_TIME) {

        /* Iterate the main loop for 5s */
        if (avahi_simple_poll_iterate(d.simple_poll, 5000) != 0) {
            rs_log_crit("Event loop exited abnormaly.\n");
            goto finish;
        }
    }

    /* Wer are idle */
    rs_log_info("Zeroconf daemon unused.\n");

    ret = 0;

finish:

    /* Cleanup */
    if (lock_fd >= 0) {
        generic_lock(lock_fd, 1, 0, 0);
        close(lock_fd);
    }

    if (d.fd >= 0)
        close(d.fd);

    while (d.hosts) {
        struct host *h = d.hosts;
        d.hosts = d.hosts->next;
        free_host(h);
    }

    if (d.client)
        avahi_client_free(d.client);

    if (d.simple_poll)
        avahi_simple_poll_free(d.simple_poll);

    rs_log_info("zeroconf daemon ended.\n");

    return ret;
}

/* Return path to the zeroconf directory in ~/.distcc */
static int get_zeroconf_dir(char **dir_ret) {
    static char *cached;
    int ret;

    if (cached) {
        *dir_ret = cached;
        return 0;
    } else {
        ret = dcc_get_subdir("zeroconf", dir_ret);
        if (ret == 0)
            cached = *dir_ret;
        return ret;
    }
}

/* Get the host list from zeroconf */
int dcc_zeroconf_add_hosts(struct dcc_hostdef **ret_list, int *ret_nhosts, int n_slots, struct dcc_hostdef **ret_prev) {
    char host_file[PATH_MAX], lock_file[PATH_MAX], *s = NULL;
    int lock_fd = -1, host_fd = -1;
    int fork_daemon = 0;
    int r = -1;
    char *dir;
    struct stat st;

    if (get_zeroconf_dir(&dir) != 0) {
        rs_log_crit("failed to get zeroconf dir.\n");
        goto finish;
    }

    snprintf(lock_file, sizeof(lock_file), "%s/lock", dir);
    snprintf(host_file, sizeof(host_file), "%s/hosts", dir);

    /* Open lock file */
    if ((lock_fd = open(lock_file, O_RDWR|O_CREAT, 0666)) < 0) {
        rs_log_crit("open('%s') failed: %s\n", lock_file, strerror(errno));
        goto finish;
    }

    /* Try to lock the lock file */
    if (generic_lock(lock_fd, 1, 1, 0) >= 0) {
        /* The lock succeeded => there's no daemon running yet! */
        fork_daemon = 1;
        generic_lock(lock_fd, 1, 0, 0);
    }

    close(lock_fd);

    /* Shall we fork a new daemon? */
    if (fork_daemon) {
        pid_t pid;

        rs_log_info("Spawning zeroconf daemon.\n");

        if ((pid = fork()) == -1) {
            rs_log_crit("fork() failed: %s\n", strerror(errno));
            goto finish;
        } else if (pid == 0) {
            int fd;
            /* Child */

            /* Close file descriptors and replace them by /dev/null */
            close(0);
            close(1);
            close(2);
            fd = open("/dev/null", O_RDWR);
            assert(fd == 0);
            fd = dup(0);
            assert(fd == 1);
            fd = dup(0);
            assert(fd == 2);

#ifdef HAVE_SETSID
            setsid();
#endif

            chdir("/");
            rs_add_logger(rs_logger_syslog, RS_LOG_DEBUG, NULL, 0);
            _exit(daemon_proc(host_file, lock_file, n_slots));
        }

        /* Parent */

        /* Wait some time for initial host gathering */
        usleep(1000000);         /* 1000 ms */
    }

    /* Open host list read-only */
    if ((host_fd = open(host_file, O_RDONLY)) < 0) {
        rs_log_crit("open('%s') failed: %s\n", host_file, strerror(errno));
        goto finish;
    }

    /* A read lock */
    if (generic_lock(host_fd, 0, 1, 1) < 0) {
        rs_log_crit("lock failed: %s\n", strerror(errno));
        goto finish;
    }

    /* Get file size */
    if (fstat(host_fd, &st) < 0) {
        rs_log_crit("stat() failed: %s\n", strerror(errno));
        goto finish;
    }

    if (st.st_size >= MAX_FILE_SIZE) {
        rs_log_crit("file too large.\n");
        goto finish;
    }

    /* read file data */
    s = malloc((size_t) st.st_size+1);
    assert(s);

    if (dcc_readx(host_fd, s, (size_t) st.st_size) != 0) {
        rs_log_crit("failed to read from file.\n");
        goto finish;
    }
    s[st.st_size] = 0;

    /* Parse host data */
    if (dcc_parse_hosts(s, host_file, ret_list, ret_nhosts, ret_prev) != 0) {
        rs_log_crit("failed to parse host file.\n");
        goto finish;
    }

    r = 0;

finish:
    if (host_fd >= 0) {
        generic_lock(host_fd, 0, 0, 1);
        close(host_fd);
    }

    free(s);

    return r;
}
