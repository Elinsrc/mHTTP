// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mh_ci_ncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) 
    {
        int x = tolower((unsigned char)*a);
        int y = tolower((unsigned char)*b);
        if (x != y) 
            return x - y;
        if (!x) 
            return 0;
    }
    return 0;
}

int mh_ci_contains(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++)
        if (mh_ci_ncmp(haystack, needle, n) == 0) 
            return 1;
    return 0;
}

char *mh_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

const char *mh_header_value(const char *line, const char *name)
{
    size_t n = strlen(name);
    const char *v;

    if (mh_ci_ncmp(line, name, n) != 0 || line[n] != ':') 
        return NULL;
    
    v = line + n + 1;
    while (*v == ' ' || *v == '\t') 
        v++;
    return v;
}

mhttp_error mh_fail(mhttp_response *res, mhttp_error code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->error, sizeof res->error, fmt, ap);
    va_end(ap);
    res->code = code;
    return code;
}

mhttp_error mh_fail_wait(mhttp_response *res, mhttp_error code)
{
    const char *msg = "socket wait failed";
    if (code == MHTTP_ERR_TIMEOUT)   
        msg = "operation timed out";
    if (code == MHTTP_ERR_CANCELLED) 
        msg = "cancelled";
    return mh_fail(res, code, "%s", msg);
}

int mh_is_cancelled(const mhttp_request *req)
{
    return req->is_cancelled && req->is_cancelled(req->is_cancelled_user);
}

static int sb_reserve(mh_strbuf *b, size_t extra)
{
    size_t need = b->len + extra + 1;

    if (need > b->cap) 
    {
        size_t cap = b->cap ? b->cap : 256;
        char *grown;

        while (cap < need) 
        {
            if (cap > SIZE_MAX / 2) 
                return 0;
            cap *= 2;
        }
        grown = realloc(b->p, cap);
        if (!grown)
            return 0;
        b->p = grown;
        b->cap = cap;
    }
    return 1;
}

int mh_sb_addf(mh_strbuf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int n;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0 || !sb_reserve(b, (size_t)n)) 
    {
        va_end(ap2);
        return 0;
    }
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
    return 1;
}

void mh_sb_free(mh_strbuf *b)
{
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}
