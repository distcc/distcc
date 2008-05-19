#ifndef foozeroconfhfoo
#define foozeroconfhfoo

#include <inttypes.h>

int dcc_zeroconf_add_hosts(struct dcc_hostdef **re_list, int *ret_nhosts, int slots, struct dcc_hostdef **ret_prev);

void *dcc_zeroconf_register(uint16_t port, int n_cpus);
int dcc_zeroconf_unregister(void*);

char* dcc_get_gcc_version(char *s, size_t nbytes);
char* dcc_get_gcc_machine(char *s, size_t nbytes);

char* dcc_make_dnssd_subtype(char *stype, size_t nbytes, const char *v, const char *m);

#define DCC_DNS_SERVICE_TYPE "_distcc._tcp"

#endif
