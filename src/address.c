/*
 *  Copyright (C) 2004 Steve Harris
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef _MSC_VER
#include <io.h>
#define snprintf _snprintf
#else
#include <unistd.h>
#endif

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#endif

#include "lo_types_internal.h"
#include "lo_internal.h"
#include "lo/lo.h"
#include "config.h"

lo_address lo_address_new_with_proto(int proto, const char *host,
                                     const char *port)
{
    lo_address a;

    if (proto != LO_UDP && proto != LO_TCP && proto != LO_UNIX)
        return NULL;

    a = calloc(1, sizeof(struct _lo_address));
    if (a == NULL)
        return NULL;

    a->ai = NULL;
    a->socket = -1;
    a->protocol = proto;
    switch (proto) {
    default:
    case LO_UDP:
    case LO_TCP:
        if (host) {
            a->host = strdup(host);
        } else {
            a->host = strdup("localhost");
        }
        break;
    case LO_UNIX:
        a->host = strdup("localhost");
        break;
    }
    if (port) {
        a->port = strdup(port);
    } else {
        a->port = NULL;
    }

    a->ttl = -1;
    a->addr.size = 0;
    a->addr.iface = 0;

    return a;
}

lo_address lo_address_new(const char *host, const char *port)
{
    return lo_address_new_with_proto(LO_UDP, host, port);
}

lo_address lo_address_new_from_url(const char *url)
{
    lo_address a;
    int protocol;
    char *host, *port, *proto;

    if (!url || !*url) {
        return NULL;
    }

    protocol = lo_url_get_protocol_id(url);
    if (protocol == LO_UDP || protocol == LO_TCP) {
        host = lo_url_get_hostname(url);
        port = lo_url_get_port(url);
        a = lo_address_new_with_proto(protocol, host, port);
        if (host)
            free(host);
        if (port)
            free(port);
#ifndef WIN32
    } else if (protocol == LO_UNIX) {
        port = lo_url_get_path(url);
        a = lo_address_new_with_proto(LO_UNIX, NULL, port);
        if (port)
            free(port);
#endif
    } else {
        proto = lo_url_get_protocol(url);
        fprintf(stderr,
                PACKAGE_NAME ": protocol '%s' not supported by this "
                "version\n", proto);
        if (proto)
            free(proto);

        return NULL;
    }

    return a;
}

const char *lo_address_get_hostname(lo_address a)
{
    if (!a) {
        return NULL;
    }

    return a->host;
}

int lo_address_get_protocol(lo_address a)
{
    if (!a) {
        return -1;
    }

    return a->protocol;
}

const char *lo_address_get_port(lo_address a)
{
    if (!a) {
        return NULL;
    }

    return a->port;
}

static const char *get_protocol_name(int proto)
{
    switch (proto) {
    case LO_UDP:
        return "udp";
    case LO_TCP:
        return "tcp";
#ifndef WIN32
    case LO_UNIX:
        return "unix";
#endif
    }
    return NULL;
}


char *lo_address_get_url(lo_address a)
{
    char *buf;
    int ret = 0;
    int needquote = strchr(a->host, ':') ? 1 : 0;
    char *fmt;

    if (needquote) {
        fmt = "osc.%s://[%s]:%s/";
    } else {
        fmt = "osc.%s://%s:%s/";
    }
#ifndef _MSC_VER
    ret = snprintf(NULL, 0, fmt,
                   get_protocol_name(a->protocol), a->host, a->port);
#endif
    if (ret <= 0) {
        /* this libc is not C99 compliant, guess a size */
        ret = 1023;
    }
    buf = malloc((ret + 2) * sizeof(char));
    snprintf(buf, ret + 1, fmt,
             get_protocol_name(a->protocol), a->host, a->port);

    return buf;
}

void lo_address_free(lo_address a)
{
    if (a) {
        if (a->socket != -1) {
            closesocket(a->socket);
        }
        if (a->host)
            free(a->host);
        if (a->port)
            free(a->port);
        if (a->ai)
            freeaddrinfo(a->ai);
        if (a->addr.iface)
            free(a->addr.iface);
        free(a);
    }
}

int lo_address_errno(lo_address a)
{
    return a->errnum;
}

const char *lo_address_errstr(lo_address a)
{
    char *msg;

    if (a->errstr) {
        return a->errstr;
    }

    msg = strerror(a->errnum);
    if (msg) {
        return msg;
    } else {
        return "unknown error";
    }

    return "unknown error";
}

char *lo_url_get_protocol(const char *url)
{
    char *protocol, *ret;

    if (!url) {
        return NULL;
    }

    protocol = malloc(strlen(url));

    if (sscanf(url, "osc://%s", protocol)) {
        fprintf(stderr,
                PACKAGE_NAME " warning: no protocol specified in URL, "
                "assuming UDP.\n");
        ret = strdup("udp");
    } else if (sscanf(url, "osc.%[^:/[]", protocol)) {
        ret = strdup(protocol);
    } else {
        ret = NULL;
    }

    free(protocol);

    return ret;
}

int lo_url_get_protocol_id(const char *url)
{
    if (!url) {
        return -1;
    }

    if (!strncmp(url, "osc:", 4)) {
        fprintf(stderr,
                PACKAGE_NAME " warning: no protocol specified in URL, "
                "assuming UDP.\n");
        return LO_UDP;          // should be LO_DEFAULT?
    } else if (!strncmp(url, "osc.udp:", 8)) {
        return LO_UDP;
    } else if (!strncmp(url, "osc.tcp:", 8)) {
        return LO_TCP;
    } else if (!strncmp(url, "osc.unix:", 9)) {
        return LO_UNIX;
    }
    return -1;
}

char *lo_url_get_hostname(const char *url)
{
    char *hostname = malloc(strlen(url));

    if (sscanf(url, "osc://%[^[:/]", hostname)) {
        return hostname;
    }
    if (sscanf(url, "osc.%*[^:/]://[%[^]/]]", hostname)) {
        return hostname;
    }
    if (sscanf(url, "osc.%*[^:/]://%[^[:/]", hostname)) {
        return hostname;
    }

    /* doesnt look like an OSC URL */
    free(hostname);

    return NULL;
}

char *lo_url_get_port(const char *url)
{
    char *port = malloc(strlen(url));

    if (sscanf(url, "osc://%*[^:]:%[0-9]", port)) {
        return port;
    }
    if (sscanf(url, "osc.%*[^:]://%*[^:]:%[0-9]", port)) {
        return port;
    }
    if (sscanf(url, "osc://[%*[^]]]:%[0-9]", port)) {
        return port;
    }
    if (sscanf(url, "osc.%*[^:]://[%*[^]]]:%[0-9]", port)) {
        return port;
    }

    /* doesnt look like an OSC URL with port number */
    free(port);

    return NULL;
}

char *lo_url_get_path(const char *url)
{
    char *path = malloc(strlen(url));

    if (sscanf(url, "osc://%*[^:]:%*[0-9]%s", path)) {
        return path;
    }
    if (sscanf(url, "osc.%*[^:]://%*[^:]:%*[0-9]%s", path) == 1) {
        return path;
    }
    if (sscanf(url, "osc.unix://%*[^/]%s", path) == 1) {
        return path;
    }
    if (sscanf(url, "osc.%*[^:]://%s", path)) {
        return path;
    }

    /* doesnt look like an OSC URL with port number and path */
    free(path);

    return NULL;
}

void lo_address_set_ttl(lo_address t, int ttl)
{
    if (t->protocol == LO_UDP)
        t->ttl = ttl;
}

int lo_address_get_ttl(lo_address t)
{
    return t->ttl;
}

int lo_address_resolve(lo_address a)
{
    int ret;

    if (a->protocol == LO_UDP || a->protocol == LO_TCP) {
        struct addrinfo *ai;
        struct addrinfo hints;

        memset(&hints, 0, sizeof(hints));
#ifdef ENABLE_IPV6
        hints.ai_family = PF_UNSPEC;
#else
        hints.ai_family = PF_INET;
#endif
        hints.ai_socktype =
            a->protocol == LO_UDP ? SOCK_DGRAM : SOCK_STREAM;

        if ((ret = getaddrinfo(a->host, a->port, &hints, &ai))) {
            a->errnum = ret;
            a->errstr = gai_strerror(ret);
            a->ai = NULL;
            return -1;
        }

        a->ai = ai;
    }

    return 0;
}

int lo_address_set_iface(lo_address t, const char *iface, const char *ip)
{
    if (!t->ai) {
        lo_address_resolve(t);
        if (!t->ai)
            return 2;  // Need the address family to continue
    }
    int fam = t->ai->ai_family;

    return lo_inaddr_find_iface(&t->addr, fam, iface, ip);
}

int lo_inaddr_find_iface(lo_inaddr t, int fam,
                         const char *iface, const char *ip)
{

    union {
        struct in_addr addr;
#ifdef ENABLE_IPV6
        struct in6_addr addr6;
#endif
    } a;

    if (ip) {
        int rc = inet_pton(fam, ip, &a);
        if (rc!=1)
            return (rc<0) ? 3 : 4;
    }

    struct ifaddrs *ifa, *ifa_list;
    if (getifaddrs(&ifa_list)==-1)
        return 5;
    ifa = ifa_list;

    int found = 0;
    while (ifa) {
        if (!ifa->ifa_addr) {
            ifa = ifa->ifa_next;
            continue;
        }
        if (ip) {
            if (ifa->ifa_addr->sa_family == AF_INET && fam == AF_INET)
            {
                if (memcmp(&((struct sockaddr_in*)ifa->ifa_addr)->sin_addr,
                           &a.addr, sizeof(struct in_addr))==0) {
                    found = 1;
                    t->size = sizeof(struct in_addr);
                    memcpy(&t->a, &a, t->size);
                    break;
                }
            }
#ifdef ENABLE_IPV6
            else if (ifa->ifa_addr->sa_family == AF_INET6 && fam == AF_INET6)
            {
                if (memcmp(&((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr,
                           &a.addr6, sizeof(struct in6_addr))==0) {
                    found = 1;
                    t->size = sizeof(struct in6_addr);
                    memcpy(&t->a, &a, t->size);
                    break;
                }
            }
#endif
        }
        if (iface) {
            if (ifa->ifa_addr->sa_family == fam
                && strcmp(ifa->ifa_name, iface)==0)
            {
                if (fam==AF_INET) {
                    found = 1;
                    t->size = sizeof(struct in_addr);
                    memcpy(&t->a, &((struct sockaddr_in*)
                                    ifa->ifa_addr)->sin_addr,
                           t->size);
                    break;
                }
#ifdef ENABLE_IPV6
                else if (fam==AF_INET6) {
                    found = 1;
                    t->size = sizeof(struct in6_addr);
                    memcpy(&t->a, &((struct sockaddr_in6*)
                                    ifa->ifa_addr)->sin6_addr,
                           t->size);
                    break;
                }
#endif
            }
        }
        ifa = ifa->ifa_next;
    }

    if (found && ifa->ifa_name) {
        if (t->iface) free(t->iface);
        t->iface = strdup(ifa->ifa_name);
    }

    freeifaddrs(ifa_list);
    return !found;
}

const char* lo_address_get_iface(lo_address t)
{
    if (t)
        return t->addr.iface;
    return 0;
}

/* vi:set ts=8 sts=4 sw=4: */
