// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "mHTTP.h"
#include "ca.h"
#include "http.h"
#include "platform.h"
#include "url.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psa/crypto.h>

#define DEFAULT_TIMEOUT_MS 10000
#define DEFAULT_MAX_REDIRECTS 5
#define DEFAULT_MAX_BODY (64u * 1024u * 1024u)

static int g_initialized = 0;

mhttp_error mhttp_global_init(void)
{
    if (g_initialized)
        return MHTTP_OK;

    if (mh_net_init() != 0)
        return MHTTP_ERR_INIT;

    if (psa_crypto_init() != PSA_SUCCESS)
        return MHTTP_ERR_TLS;

    mh_ca_init();

    g_initialized = 1;
    return MHTTP_OK;
}
void mhttp_global_cleanup(void)
{
    if (!g_initialized)
        return;

    mh_ca_free();
    mh_net_cleanup();
    g_initialized = 0;
}

void mhttp_request_init(mhttp_request *req, const char *url)
{
    memset(req, 0, sizeof *req);
    req->url = url;
    req->timeout_ms = DEFAULT_TIMEOUT_MS;
    req->connect_timeout_ms = 0;
    req->max_redirects = DEFAULT_MAX_REDIRECTS;
    req->max_body_size = DEFAULT_MAX_BODY;
}

void mhttp_response_free(mhttp_response *res)
{
    if (!res) 
        return;
    free(res->body);
    free(res->headers);
    memset(res, 0, sizeof *res);
}

static void origin_key(const mh_url *u, char *out, size_t size)
{
    snprintf(out, size, "%d|%s", u->https, u->hostport);
}

static int redirect_becomes_get(int status)
{
    return status == 301 || status == 302 || status == 303;
}

static mhttp_error finalize(mhttp_response *res)
{
    res->code = MHTTP_OK;
    if (res->status >= 200 && res->status < 300)
    {
        res->ok = 1;
    }
    else
    {
        res->code = MHTTP_ERR_HTTP;
        snprintf(res->error, sizeof res->error, "HTTP %d", res->status);
    }
    return res->code;
}

static mhttp_error perform_url(const mhttp_request *req, const char *start_url, mhttp_response *res)
{
    mhttp_error e = MHTTP_OK;
    mh_message msg;
    char location[MH_LOCATION_MAX];
    char first_origin[320] = "";
    char *url;
    int connect_timeout_ms;
    int stall_timeout_ms;
    int max_redirects;

    memset(res, 0, sizeof *res);

    msg.method = req->method ? req->method : "GET";
    msg.headers = req->headers;
    msg.body = req->body;
    msg.body_len = req->body_len;

    connect_timeout_ms = req->connect_timeout_ms > 0
        ? req->connect_timeout_ms
        : (req->timeout_ms > 0 ? req->timeout_ms : DEFAULT_TIMEOUT_MS);

    stall_timeout_ms = req->timeout_ms;

    max_redirects = req->max_redirects > 0 ? req->max_redirects : DEFAULT_MAX_REDIRECTS;

    url = mh_strdup(start_url);
    if (!url)
        return mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");

    for (int hop = 0;; hop++)
    {
        mh_url u;
        char origin[320];
        char *next;
        int64_t connect_deadline;

        if ((e = mh_url_parse(url, &u, res)))
            break;

        origin_key(&u, origin, sizeof origin);
        if (!first_origin[0])
            memcpy(first_origin, origin, sizeof first_origin);
        else if (strcmp(first_origin, origin) != 0)
            msg.headers = NULL;

        connect_deadline = mh_now_ms() + connect_timeout_ms;

        e = mh_http_exchange(req, &msg, &u, connect_deadline, stall_timeout_ms, res, location);
        if (e || !location[0])
        {
            mh_url_free(&u);
            break;
        }

        if (hop >= max_redirects)
        {
            mh_url_free(&u);
            e = mh_fail(res, MHTTP_ERR_TOO_MANY_REDIRECTS, "too many redirects");
            break;
        }

        next = mh_url_resolve(&u, location);
        mh_url_free(&u);
        if (!next)
        {
            e = mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");
            break;
        }
        free(url);
        url = next;

        if (redirect_becomes_get(res->status))
        {
            if (strcmp(msg.method, "HEAD") != 0) msg.method = "GET";
            msg.body = NULL;
            msg.body_len = 0;
        }
    }
    free(url);

    if (e)
    {
        free(res->body);
        res->body = NULL;
        res->body_len = 0;
        return e;
    }
    return finalize(res);
}

static int has_scheme(const char *url)
{
    const char *sep = strstr(url, "://");
    return sep && sep < url + strcspn(url, "/?#");
}

static mhttp_error perform_guess_scheme(const mhttp_request *req, mhttp_response *res)
{
    static const char *const schemes[] = { "https://", "http://" };
    mhttp_error e = MHTTP_ERR_PARAM;

    for (size_t i = 0; i < 2; i++) {
        size_t len = strlen(schemes[i]) + strlen(req->url) + 1;
        char *full = malloc(len);

        if (!full)
            return mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");
        snprintf(full, len, "%s%s", schemes[i], req->url);

        mhttp_response_free(res);
        e = perform_url(req, full, res);
        free(full);

        if (e != MHTTP_ERR_CONNECT)
            break;
    }
    return e;
}

mhttp_error mhttp_perform(const mhttp_request *req, mhttp_response *res)
{
    if (!res)
        return MHTTP_ERR_PARAM;

    memset(res, 0, sizeof *res);

    if (!req || !req->url)
        return mh_fail(res, MHTTP_ERR_PARAM, "no url");

    if (!g_initialized)
        return mh_fail(res, MHTTP_ERR_PARAM, "mhttp_global_init() was not called");

    if (has_scheme(req->url))
        return perform_url(req, req->url, res);

    return perform_guess_scheme(req, res);
}

mhttp_error mhttp_get(const char *url, int timeout_ms, mhttp_response *res)
{
    mhttp_request req;
    mhttp_request_init(&req, url);
    req.timeout_ms = timeout_ms;
    return mhttp_perform(&req, res);
}

mhttp_error mhttp_post(const char *url, const char *content_type, const void *body, size_t len, int timeout_ms, mhttp_response *res)
{
    mhttp_request req;
    char ct_header[256];
    const char *headers[2];

    mhttp_request_init(&req, url);
    req.method = "POST";
    req.body = body;
    req.body_len = len;
    req.timeout_ms = timeout_ms;

    if (content_type && content_type[0])
    {
        snprintf(ct_header, sizeof ct_header, "Content-Type: %s", content_type);
        headers[0] = ct_header;
        headers[1] = NULL;
        req.headers = headers;
    }
    return mhttp_perform(&req, res);
}

int mhttp_response_header(const mhttp_response *res, const char *name, char *out, size_t outsz)
{
    const char *p;

    if (!res || !res->headers || !outsz)
        return 0;

    p = strchr(res->headers, '\n');
    while (p && *++p) 
    {
        size_t len = strcspn(p, "\n");
        char line[8192];

        if (len < sizeof line)
        {
            const char *value;
            memcpy(line, p, len);
            line[len] = 0;
            if ((value = mh_header_value(line, name)))
            {
                snprintf(out, outsz, "%s", value);
                return 1;
            }
        }
        p += len;
    }
    return 0;
}