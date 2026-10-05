// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "url.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mh_url_free(mh_url *u)
{
    free(u->path);
    u->path = NULL;
}

static const char *skip_userinfo(const char *p, const char *end)
{
    const char *at = NULL;
    for (const char *q = p; q < end; q++)
        if (*q == '@') 
            at = q;
    return at ? at + 1 : p;
}

static mhttp_error parse_host(const char **pp, const char *end, mh_url *u, int *ipv6, mhttp_response *res)
{
    const char *p = *pp;
    size_t len;

    if (*p == '[') 
    {
        const char *close = memchr(p, ']', (size_t)(end - p));
        if (!close) 
            return mh_fail(res, MHTTP_ERR_URL, "bad IPv6 literal");
        len = (size_t)(close - p - 1);
        if (len == 0 || len >= sizeof u->host) 
            return mh_fail(res, MHTTP_ERR_URL, "bad host");
        memcpy(u->host, p + 1, len);
        *pp = close + 1;
        *ipv6 = 1;
    } 
    else 
    {
        const char *colon = memchr(p, ':', (size_t)(end - p));
        const char *host_end = colon ? colon : end;
        len = (size_t)(host_end - p);
        if (len == 0 || len >= sizeof u->host) 
            return mh_fail(res, MHTTP_ERR_URL, "bad host");
        memcpy(u->host, p, len);
        *pp = host_end;
    }
    return MHTTP_OK;
}

static mhttp_error parse_port(const char *p, const char *end, mh_url *u, mhttp_response *res)
{
    if (*p == ':' && p < end) 
    {
        int port = atoi(p + 1);
        if (port <= 0 || port > 65535) 
            return mh_fail(res, MHTTP_ERR_URL, "bad port");
        u->port = port;
    }
    return MHTTP_OK;
}

static void format_hostport(mh_url *u, int default_port, int ipv6)
{
    const char *open  = ipv6 ? "[" : "";
    const char *close = ipv6 ? "]" : "";

    if (u->port != default_port)
        snprintf(u->hostport, sizeof u->hostport, "%s%s%s:%d", open, u->host, close, u->port);
    else
        snprintf(u->hostport, sizeof u->hostport, "%s%s%s", open, u->host, close);
}

static mhttp_error parse_path(const char *rest, mh_url *u, mhttp_response *res)
{
    size_t len = strcspn(rest, "#");

    u->path = malloc(len + 2);
    if (!u->path) return mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");

    if (len == 0 || *rest == '?') 
    {
        u->path[0] = '/';
        memcpy(u->path + 1, rest, len);
        u->path[len + 1] = 0;
    } 
    else 
    {
        memcpy(u->path, rest, len);
        u->path[len] = 0;
    }
    return MHTTP_OK;
}

mhttp_error mh_url_parse(const char *s, mh_url *u, mhttp_response *res)
{
    const char *p, *authority_end;
    int default_port, ipv6 = 0;
    mhttp_error e;

    memset(u, 0, sizeof *u);

    if (mh_ci_ncmp(s, "https://", 8) == 0) 
    {
        u->https = 1;
        p = s + 8;
        default_port = 443;
    } 
    else if (mh_ci_ncmp(s, "http://", 7) == 0) 
    {
        p = s + 7;
        default_port = 80;
    } 
    else 
    {
        return mh_fail(res, MHTTP_ERR_URL, "unsupported URL scheme");
    }
    u->port = default_port;

    authority_end = p + strcspn(p, "/?#");
    p = skip_userinfo(p, authority_end);

    if ((e = parse_host(&p, authority_end, u, &ipv6, res))) 
        return e;
    
    if ((e = parse_port(p, authority_end, u, res)))         
        return e;

    format_hostport(u, default_port, ipv6);

    return parse_path(authority_end, u, res);
}

char *mh_url_resolve(const mh_url *base, const char *loc)
{
    const char *scheme = base->https ? "https" : "http";
    size_t cap = strlen(loc) + strlen(base->path) + strlen(base->hostport) + 16;
    char *out = malloc(cap);

    if (!out) 
        return NULL;

    if (mh_ci_ncmp(loc, "http://", 7) == 0 || mh_ci_ncmp(loc, "https://", 8) == 0) 
    {
        snprintf(out, cap, "%s", loc);
    } 
    else if (loc[0] == '/' && loc[1] == '/') 
    {
        snprintf(out, cap, "%s:%s", scheme, loc);
    } 
    else if (loc[0] == '/')
    {
        snprintf(out, cap, "%s://%s%s", scheme, base->hostport, loc);
    } 
    else 
    {
        size_t dir = strcspn(base->path, "?");
        while (dir > 0 && base->path[dir - 1] != '/') dir--;
        snprintf(out, cap, "%s://%s%.*s%s", scheme, base->hostport, (int)dir, base->path, loc);
    }
    return out;
}
