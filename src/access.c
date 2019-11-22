/* -*- c-file-style: "java"; indent-tabs-mode: nil; tab-width: 4; fill-column: 78 -*-
 *
 * distcc -- A simple distributed compiler system
 *
 * Copyright (C) 2003, 2004 by Martin Pool <mbp@samba.org>
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

                    /*
                     * They that forsake the law praise the wicked: but such
                     * as keep the law contend with them.
                     *        -- Proverbs 28:4
                     */


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "types.h"
#include "distcc.h"
#include "trace.h"
#include "access.h"
#include "exitcode.h"

/**
 * @file
 *
 * IP-based access-control system
 */

static const uint32_t allones = 0xffffffffUL;
#ifdef ENABLE_RFC2553
static const uint8_t allones8 = 0xffU;
#endif

/**
 * Split a "HOST/BITS" mask specification into HOST and BITS.
 *
 * @returns 0 on success or -1 if the program ran out of heap space
 **/
static int split_mask(const char *spec,
                       char **host,
                       const char **bits)
{
    int value_len = strcspn(spec, "/");
    
    if (host != NULL) {
        char *host_str;
        
        host_str = malloc(value_len + 1);
        if (host_str == NULL) {
            rs_log_error("could not allocate memory (%d bytes)", value_len + 1);
            return -1;
        }
        strncpy(host_str, spec, value_len);
        host_str[value_len] = '\0';
        
        *host = host_str;
    }
    
    if (bits != NULL) {
        if (spec[value_len] && spec[value_len + 1])
            *bits = spec + value_len + 1;
        else
            *bits = NULL;
    }
    
    return 0;
}

/**
 * Set a dcc_address_t to a full mask.
 **/
static void set_allones(dcc_address_t *addr)
{
#ifndef ENABLE_RFC2553
    addr->s_addr = allones;
#else
    memset(&addr->addr, allones8, sizeof(struct in6_addr));
#endif
}


/**
 * Set a 32-bit address, @p addr, to a mask of @p mask_size bits.
 **/
static inline void set_mask_inet(struct in_addr *addr, int mask_bits) {
    addr->s_addr = htonl(~(allones >> mask_bits));
}


#ifdef ENABLE_RFC2553
/**
 * Set a v6 address, @p addr, to a mask of @p mask_size bits.
 **/
static void set_mask_inet6(struct in6_addr *addr, int mask_bits) {
    uint8_t *ip6_addr = addr->s6_addr;
    int allones_count = mask_bits / 8;
    int i;

    for (i = 0; i < allones_count; i++)
        ip6_addr[i] = allones8;

    ip6_addr[i] = ~(allones8 >> (mask_bits % 8));
    i++;

    for (; i < 16; i++)
        ip6_addr[i] = 0;
}
#endif /* ENABLE_RFC2553 */


/**
 * Set a dcc_address_t to a mask of @p mask_bits bits.  The family field of
 * the address should be set appropriately if RFC2553 is enabled.
 **/
static void set_mask(dcc_address_t *mask, int mask_bits)
{
#ifndef ENABLE_RFC2553
    set_mask_inet(mask, mask_bits);
#else
    if (mask->family == AF_INET)
        set_mask_inet(&mask->addr.inet, mask_bits);
    else
        set_mask_inet6(&mask->addr.inet6, mask_bits);
#endif
}

/**
 * Interpret a "HOST/BITS" mask specification.  Return @p value and @p mask.
 **/
int dcc_parse_mask(const char *spec,
                   dcc_address_t *value,
                   dcc_address_t *mask)
{
    char *value_str;
    int ret;
    const char *mask_str;
#ifndef ENABLE_RFC2553
    struct in_addr ia;
#else
    struct addrinfo hints;
    struct addrinfo *res;
#endif

    ret = split_mask(spec, &value_str, &mask_str);
    if (ret == -1)
        return EXIT_OUT_OF_MEMORY;

    /* extract and parse value part */
#ifndef ENABLE_RFC2553
    ret = inet_aton(value_str, &ia);
    if (ret == 0) {
        rs_log_error("can't parse internet address \"%s\"", value_str);
        free(value_str);
        return EXIT_BAD_ARGUMENTS;
    }
    *value = ia;

#else /* ENABLE_RFC2553 */

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = 0;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(value_str, NULL, &hints, &res);
    if (ret != 0) {
        rs_log_error("can't parse internet address \"%s\": %s",
                     value_str, gai_strerror(ret));
        free(value_str);
        return EXIT_BAD_ARGUMENTS;
    }

    value->family = res->ai_family;
    if (res->ai_family == AF_INET) {
        value->addr.inet = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    } else if (res->ai_family == AF_INET6) {
        value->addr.inet6 = ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
    } else {
        rs_log_error("unsupported address family %d for address \"%s\"",
                     res->ai_family, value_str);
        freeaddrinfo(res);
        free(value_str);
        return EXIT_BAD_ARGUMENTS;
    }

    freeaddrinfo(res);
#endif /* ENABLE_RFC2553 */
    free(value_str);


#ifdef ENABLE_RFC2553
    mask->family = value->family;
#endif
    if (mask_str != NULL) {
        int mask_bits;
#ifndef ENABLE_RFC2553
        static const int mask_max_bits = 32;
#else
        int mask_max_bits;
        if (mask->family == AF_INET)
            mask_max_bits = 32;
        else
            mask_max_bits = 128;
#endif

        /* find mask length as a number of bits */
        mask_bits = atoi(mask_str);
        if (mask_bits < 0 || mask_bits > mask_max_bits) {
            rs_log_error("invalid mask \"%s\"", mask_str);
            return EXIT_BAD_ARGUMENTS;
        }

        /* Make a network-endian mask with the top mask_bits set.  */
        if (mask_bits == mask_max_bits) {
            set_allones(mask);
        } else {
            set_mask(mask, mask_bits);
        }
    } else {
        set_allones(mask);
    }
    return 0;
}


/**
 * Check whether two 32-bit addresses match.
 *
 * @returns 0 if they match, or EXIT_ACCESS_DENIED.
 **/
static int check_address_inet(const struct in_addr *client,
                              const struct in_addr *value,
                              const struct in_addr *mask)
{
    if ((client->s_addr & mask->s_addr) == (value->s_addr & mask->s_addr)) {
        rs_trace("match client %#lx, value %#lx, mask %#lx",
                 (long) client->s_addr,
                 (long) value->s_addr,
                 (long) mask->s_addr);
        return 0;
    } else {
        rs_trace("deny client %#lx, value %#lx, mask %#lx",
                 (long) client->s_addr,
                 (long) value->s_addr,
                 (long) mask->s_addr);
        return EXIT_ACCESS_DENIED;
    }
}

#ifdef ENABLE_RFC2553
/**
 * Compare the masked components of two v6 addresses
 *
 * @returns 1 if they match, or 0.
 **/
static int compare_address_inet6(const struct in6_addr *client,
                                 const struct in6_addr *value,
                                 const struct in6_addr *mask)
{
    int i;

    for (i = 0; i < 16; ++i)
        if ((client->s6_addr[i] & mask->s6_addr[i]) != (value->s6_addr[i] & mask->s6_addr[i]))
            return 0;

    return 1;
}

/**
 * Check whether two v6 addresses match.
 *
 * @returns 0 if they match, or EXIT_ACCESS_DENIED.
 **/
static int check_address_inet6(const struct in6_addr *client,
                               const struct in6_addr *value,
                               const struct in6_addr *mask)
{
    if (compare_address_inet6(client, value, mask)) {
        rs_trace("match v6 client");
        //rs_trace("match client %#lx, value %#lx, mask %#lx",
        //(long) *client, (long) *value, (long) *mask);
        return 0;
    } else {
        rs_trace("deny v6 client");
        //rs_trace("deny client %#lx, value %#lx, mask %#lx",
        //(long) *client, (long) *value, (long) *mask);
        return EXIT_ACCESS_DENIED;
    }
}

/**
 * Extract the lower 32-bits of a v6 address.
 **/
static uint32_t inet_from_inet6(const struct in6_addr *addr)
{
    uint32_t dest = 0;
    int i;

    for (i = 0; i < 4; ++i) {
        uint32_t dest_byte = addr->s6_addr[12 + i];
        dest |= (dest_byte << (8 * (3-i)));
    }

    return dest;
}
#endif /* ENABLE_RFC2553 */


/**
 * Check whether a client ought to be allowed.
 *
 * @returns 0 for allowed, or EXIT_ACCESS_DENIED.
 **/
int dcc_check_address(const struct sockaddr *client,
                      const dcc_address_t *value,
                      const dcc_address_t *mask)
{
    if (client->sa_family == AF_INET) {
        /* The double-cast here avoids warnings from -Wcast-align. */
        const struct sockaddr_in *sockaddr = (const struct sockaddr_in *) (void *) client;
        const struct in_addr *cli_addr = &sockaddr->sin_addr;

#ifndef ENABLE_RFC2553
        return check_address_inet(cli_addr, value, mask);
#else
        if (value->family == AF_INET)
            return check_address_inet(cli_addr, &value->addr.inet, &mask->addr.inet);
        else
            return EXIT_ACCESS_DENIED;


    } else if (client->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)client;
        const struct in6_addr *a6 = &sa6->sin6_addr;

        if (value->family == AF_INET) {
            if (IN6_IS_ADDR_V4MAPPED(a6) || IN6_IS_ADDR_V4COMPAT(a6)) {
                struct in_addr a4;
                a4.s_addr = inet_from_inet6(a6);
                return check_address_inet(&a4, &value->addr.inet, &mask->addr.inet);
            } else
                return EXIT_ACCESS_DENIED;
        } else
            return check_address_inet6(a6, &value->addr.inet6, &mask->addr.inet6);
        
#endif
    } else {
        rs_log_notice("access denied from unsupported address family %d",
                      client->sa_family);
        return EXIT_ACCESS_DENIED;
    }
}
