// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "http.h"
#include "conn.h"
#include "tls.h"
#include "util.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_LINE_MAX 8192
#define MH_HEADERS_MAX 65536
#define MH_DEFAULT_USER_AGENT "Mozilla/5.0 (compatible; mHTTP/" MHTTP_VERSION ")"

static int needs_content_length(const mh_message *m)
{
    return m->body_len
        || !strcmp(m->method, "POST")
        || !strcmp(m->method, "PUT")
        || !strcmp(m->method, "PATCH");
}

static void refresh_stall(mh_conn *c, int stall_timeout_ms)
{
    int64_t d = stall_timeout_ms > 0 ? mh_now_ms() + stall_timeout_ms : INT64_MAX;
    mh_conn_set_deadline(c, d);
}

static mhttp_error send_request(mh_conn *c, const mhttp_request *req, const mh_message *msg, const mh_url *url, int stall_timeout_ms)
{
    mh_strbuf head = {0};
    mhttp_error e;
    int ok = 1;

    ok &= mh_sb_addf(&head,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n",
        msg->method, url->path, url->hostport,
        req->user_agent ? req->user_agent : MH_DEFAULT_USER_AGENT);

    if (msg->headers)
    {
        for (const char *const *h = msg->headers; *h; h++)
            ok &= mh_sb_addf(&head, "%s\r\n", *h);
    }

    if (needs_content_length(msg))
        ok &= mh_sb_addf(&head, "Content-Length: %lu\r\n", (unsigned long)msg->body_len);

    ok &= mh_sb_addf(&head, "\r\n");

    if (!ok)
    {
        mh_sb_free(&head);
        return mh_fail(c->res, MHTTP_ERR_NOMEM, "out of memory");
    }

    e = mh_conn_write(c, (const uint8_t *)head.p, head.len);
    mh_sb_free(&head);
    if (e)
        return e;

    refresh_stall(c, stall_timeout_ms);

    if (msg->body_len)
    {
        e = mh_conn_write(c, msg->body, msg->body_len);
        if (e)
            return e;
        refresh_stall(c, stall_timeout_ms);
    }

    return MHTTP_OK;
}

static int is_informational(int status)
{
    return status >= 100 && status < 200 && status != 101;
}

static int is_redirect(int st)
{
    return st == 301 || st == 302 || st == 303 || st == 307 || st == 308;
}

static mhttp_error read_header_fields(mh_conn *c, mh_strbuf *raw, mh_head *h, int stall_timeout_ms)
{
    char line[MH_LINE_MAX];

    for (;;)
    {
        const char *v;
        mhttp_error e = mh_conn_read_line(c, line, sizeof line);

        if (e)
            return e;

        refresh_stall(c, stall_timeout_ms);

        if (!line[0])
            return MHTTP_OK;

        if (raw->len + strlen(line) > MH_HEADERS_MAX)
            return mh_fail(c->res, MHTTP_ERR_PROTOCOL, "headers too large");

        mh_sb_addf(raw, "%s\n", line);

        if ((v = mh_header_value(line, "Content-Length")))
        {
            char *endp;
            unsigned long long x = strtoull(v, &endp, 10);

            if (endp != v && x <= (unsigned long long)INT64_MAX)
                h->content_length = (int64_t)x;
        }
        else if ((v = mh_header_value(line, "Transfer-Encoding")))
        {
            if (mh_ci_contains(v, "chunked"))
                h->chunked = 1;
        }
        else if ((v = mh_header_value(line, "Location")))
        {
            snprintf(h->location, sizeof h->location, "%s", v);
        }
    }
}

static mhttp_error read_head(mh_conn *c, mhttp_response *res, mh_head *h, int stall_timeout_ms)
{
    char line[MH_LINE_MAX];
    mh_strbuf raw = {0};
    mhttp_error e;

    for (;;)
    {
        e = mh_conn_read_line(c, line, sizeof line);
        if (e)
            goto fail;

        refresh_stall(c, stall_timeout_ms);

        if (strncmp(line, "HTTP/1.", 7) != 0 || strlen(line) < 12)
        {
            e = mh_fail(res, MHTTP_ERR_PROTOCOL, "bad status line");
            goto fail;
        }

        h->status = atoi(line + 9);
        h->content_length = -1;
        h->chunked = 0;
        h->location[0] = 0;

        mh_sb_free(&raw);
        mh_sb_addf(&raw, "%s\n", line);

        e = read_header_fields(c, &raw, h, stall_timeout_ms);
        if (e)
            goto fail;

        if (!is_informational(h->status))
            break;
    }

    if (!raw.p)
    {
        e = mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");
        goto fail;
    }

    free(res->headers);
    res->headers = raw.p;
    return MHTTP_OK;

fail:
    mh_sb_free(&raw);
    return e;
}

typedef struct
{
    const mhttp_request *req;
    mhttp_response *res;
    int64_t received;
    int64_t total;
    size_t cap;
} mh_sink;

static mhttp_error sink_append(mh_sink *k, const uint8_t *data, size_t n)
{
    mhttp_response *res = k->res;
    size_t need;

    if (k->req->max_body_size && res->body_len + n > k->req->max_body_size)
        return mh_fail(res, MHTTP_ERR_TOO_BIG, "response exceeds max_body_size");

    need = res->body_len + n + 1;
    if (need < n)
        return mh_fail(res, MHTTP_ERR_NOMEM, "size overflow");

    if (need > k->cap)
    {
        size_t cap = k->cap ? k->cap : 4096;
        uint8_t *grown;

        while (cap < need)
        {
            if (cap > SIZE_MAX / 2)
                return mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");
            cap *= 2;
        }

        grown = realloc(res->body, cap);
        if (!grown)
            return mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");

        res->body = grown;
        k->cap = cap;
    }

    memcpy(res->body + res->body_len, data, n);
    res->body_len += n;
    res->body[res->body_len] = 0;
    return MHTTP_OK;
}

static mhttp_error sink_put(mh_sink *k, const uint8_t *data, size_t n)
{
    const mhttp_request *req = k->req;
    mhttp_error e;

    if (!n)
        return MHTTP_OK;

    if (mh_is_cancelled(req))
        return mh_fail_wait(k->res, MHTTP_ERR_CANCELLED);

    if (req->on_data)
    {
        if (req->on_data(data, n, req->on_data_user) != n)
            return mh_fail(k->res, MHTTP_ERR_WRITE_CB, "write callback aborted transfer");
    }
    else
    {
        e = sink_append(k, data, n);
        if (e)
            return e;
    }

    k->received += (int64_t)n;

    if (req->on_progress && req->on_progress(k->received, k->total, req->on_progress_user))
        return mh_fail(k->res, MHTTP_ERR_CANCELLED, "aborted by progress callback");

    return MHTTP_OK;
}

static mhttp_error pump(mh_conn *c, mh_sink *k, uint64_t remaining, int stall_timeout_ms, mhttp_error eof_code, const char *eof_msg)
{
    while (remaining)
    {
        const uint8_t *p;
        size_t n;
        size_t want = remaining > SIZE_MAX ? SIZE_MAX : (size_t)remaining;
        mhttp_error e = mh_conn_read_buffered(c, &p, &n, want);

        if (e)
            return e;

        if (!n)
            return mh_fail(c->res, eof_code, "%s", eof_msg);

        refresh_stall(c, stall_timeout_ms);

        e = sink_put(k, p, n);
        if (e)
            return e;

        remaining -= n;
    }
    return MHTTP_OK;
}

static mhttp_error skip_trailers(mh_conn *c, int stall_timeout_ms)
{
    char line[MH_LINE_MAX];

    for (;;)
    {
        mhttp_error e = mh_conn_read_line(c, line, sizeof line);

        if (e)
            return e;

        refresh_stall(c, stall_timeout_ms);

        if (!line[0])
            return MHTTP_OK;
    }
}

static mhttp_error read_body_chunked(mh_conn *c, mh_sink *k, int stall_timeout_ms)
{
    char line[MH_LINE_MAX];

    for (;;)
    {
        char *endp;
        unsigned long long size;
        mhttp_error e = mh_conn_read_line(c, line, sizeof line);

        if (e)
            return e;

        refresh_stall(c, stall_timeout_ms);

        size = strtoull(line, &endp, 16);
        if (endp == line)
            return mh_fail(c->res, MHTTP_ERR_PROTOCOL, "bad chunk size");

        if (size == 0)
            return skip_trailers(c, stall_timeout_ms);

        e = pump(c, k, size, stall_timeout_ms, MHTTP_ERR_PROTOCOL, "connection closed inside chunk");
        if (e)
            return e;

        e = mh_conn_read_line(c, line, sizeof line);
        if (e)
            return e;

        refresh_stall(c, stall_timeout_ms);
    }
}

static mhttp_error read_body_until_close(mh_conn *c, mh_sink *k, int stall_timeout_ms)
{
    for (;;)
    {
        const uint8_t *p;
        size_t n;
        mhttp_error e = mh_conn_read_buffered(c, &p, &n, (size_t)INT_MAX);

        if (e)
            return e;

        if (!n)
            return MHTTP_OK;

        refresh_stall(c, stall_timeout_ms);

        e = sink_put(k, p, n);
        if (e)
            return e;
    }
}

static mhttp_error read_body(mh_conn *c, const mhttp_request *req, mhttp_response *res, const mh_head *h, int stall_timeout_ms)
{
    mh_sink sink = { req, res, 0, 0, 0 };

    if (!h->chunked && h->content_length > 0)
        sink.total = h->content_length;

    if (h->chunked)
        return read_body_chunked(c, &sink, stall_timeout_ms);

    if (h->content_length >= 0)
    {
        return pump(c, &sink, (uint64_t)h->content_length, stall_timeout_ms, MHTTP_ERR_RECV, "connection closed before full body received");
    }

    return read_body_until_close(c, &sink, stall_timeout_ms);
}

static int response_has_body(const char *method, int status)
{
    return strcmp(method, "HEAD") != 0
        && status != 204
        && status != 304
        && !(status >= 100 && status < 200);
}

mhttp_error mh_http_exchange(const mhttp_request *req, const mh_message *msg, const mh_url *url, int64_t connect_deadline, int stall_timeout_ms, mhttp_response *res, char *location)
{
    mh_conn *c;
    mh_head head;
    mhttp_error e;

    location[0] = 0;

    c = mh_conn_new(req, res, connect_deadline);
    if (!c)
        return mh_fail(res, MHTTP_ERR_NOMEM, "out of memory");

    e = mh_conn_connect(c, url, connect_deadline);
    if (e)
        goto done;

    if (url->https)
    {
        e = mh_tls_start(c, url);
        if (e)
            goto done;
    }

    refresh_stall(c, stall_timeout_ms);

    e = send_request(c, req, msg, url, stall_timeout_ms);
    if (e)
        goto done;

    e = read_head(c, res, &head, stall_timeout_ms);
    if (e)
        goto done;

    res->status = head.status;

    if (req->follow_redirects && is_redirect(head.status) && head.location[0])
    {
        snprintf(location, MH_LOCATION_MAX, "%s", head.location);
        goto done;
    }

    if (response_has_body(msg->method, head.status))
        e = read_body(c, req, res, &head, stall_timeout_ms);

done:
    mh_conn_free(c);
    return e;
}