// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "conn.h"
#include "tls.h"
#include "util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define WAIT_SLICE_MS 100

mh_conn *mh_conn_new(const mhttp_request *req, mhttp_response *res, int64_t deadline)
{
    mh_conn *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;

    c->sock = MH_INVALID_SOCK;
    c->req = req;
    c->res = res;
    c->deadline = deadline;
    return c;
}

void mh_conn_set_deadline(mh_conn *c, int64_t deadline)
{
    c->deadline = deadline;
}

void mh_conn_free(mh_conn *c)
{
    if (!c)
        return;

    mh_tls_close(c);

    if (c->sock != MH_INVALID_SOCK)
        mh_sock_close(c->sock);

    free(c);
}

mhttp_error mh_conn_wait(mh_conn *c, int for_write, int64_t deadline)
{
    for (;;)
    {
        int64_t left;
        int slice, r;

        if (mh_is_cancelled(c->req))
            return MHTTP_ERR_CANCELLED;

        left = deadline - mh_now_ms();
        if (left <= 0)
            return MHTTP_ERR_TIMEOUT;

        slice = left > WAIT_SLICE_MS ? WAIT_SLICE_MS : (int)left;
        r = mh_sock_wait(c->sock, for_write, slice);

        if (r < 0)
            return MHTTP_ERR_RECV;
        if (r > 0)
            return MHTTP_OK;
    }
}

static mhttp_error wait_io(mh_conn *c, int for_write)
{
    mhttp_error e = mh_conn_wait(c, for_write, c->deadline);
    return e ? mh_fail_wait(c->res, e) : MHTTP_OK;
}

static mhttp_error connect_one(mh_conn *c, const struct addrinfo *ai, int64_t deadline, int *sys_err)
{
    int one = 1;
    mhttp_error e;
    mh_sock sk = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);

    if (sk == MH_INVALID_SOCK)
    {
        *sys_err = mh_last_error();
        return MHTTP_ERR_CONNECT;
    }

    mh_sock_set_nonblock(sk);
    setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);

    if (connect(sk, ai->ai_addr, (int)ai->ai_addrlen) == 0)
    {
        c->sock = sk;
        return MHTTP_OK;
    }

    *sys_err = mh_last_error();
    if (!mh_is_inprogress(*sys_err))
    {
        mh_sock_close(sk);
        return MHTTP_ERR_CONNECT;
    }

    c->sock = sk;
    e = mh_conn_wait(c, 1, deadline);
    c->sock = MH_INVALID_SOCK;

    if (e == MHTTP_OK)
    {
        int so = 0;
        socklen_t sl = sizeof so;

        getsockopt(sk, SOL_SOCKET, SO_ERROR, (char *)&so, &sl);
        if (so)
        {
            *sys_err = so;
            e = MHTTP_ERR_CONNECT;
        }
    }

    if (e != MHTTP_OK)
    {
        mh_sock_close(sk);
        return e;
    }

    c->sock = sk;
    return MHTTP_OK;
}

mhttp_error mh_conn_connect(mh_conn *c, const mh_url *u, int64_t deadline)
{
    struct addrinfo hints, *list = NULL;
    char port[16];
    mhttp_error last = MHTTP_ERR_CONNECT;
    int sys_err = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof port, "%d", u->port);

    if (getaddrinfo(u->host, port, &hints, &list) != 0 || !list)
        return mh_fail(c->res, MHTTP_ERR_DNS, "cannot resolve host '%s'", u->host);

    for (const struct addrinfo *ai = list; ai; ai = ai->ai_next)
    {
        mhttp_error e = connect_one(c, ai, deadline, &sys_err);

        if (e == MHTTP_OK)
        {
            freeaddrinfo(list);
            return MHTTP_OK;
        }

        last = e;
        if (e == MHTTP_ERR_TIMEOUT || e == MHTTP_ERR_CANCELLED)
            break;
    }
    freeaddrinfo(list);

    if (last == MHTTP_ERR_TIMEOUT || last == MHTTP_ERR_CANCELLED)
        return mh_fail_wait(c->res, last);

    return mh_fail(c->res, MHTTP_ERR_CONNECT, "connect to %s:%d failed (err %d)", u->host, u->port, sys_err);
}

mhttp_error mh_conn_write(mh_conn *c, const uint8_t *p, size_t n)
{
    while (n)
    {
        int sent;
        size_t chunk = n > INT_MAX ? INT_MAX : n;

        if (c->tls_on)
        {
            int ret = mbedtls_ssl_write(&c->ssl, p, chunk);

            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                mhttp_error e = wait_io(c, ret == MBEDTLS_ERR_SSL_WANT_WRITE);
                if (e)
                    return e;
                continue;
            }

            if (ret < 0)
                return mh_tls_fail(c, MHTTP_ERR_SEND, ret, "TLS write");

            sent = ret;
        }
        else
        {
            sent = mh_sock_send(c->sock, p, chunk);

            if (sent < 0)
            {
                int err = mh_last_error();

                if (mh_is_wouldblock(err))
                {
                    mhttp_error e = wait_io(c, 1);
                    if (e)
                        return e;
                    continue;
                }

                return mh_fail(c->res, MHTTP_ERR_SEND, "send failed (err %d)", err);
            }
        }

        p += sent;
        n -= (size_t)sent;
    }
    return MHTTP_OK;
}

mhttp_error mh_conn_read(mh_conn *c, uint8_t *buf, size_t n, size_t *got)
{
    *got = 0;
    if (n > INT_MAX)
        n = INT_MAX;

    for (;;)
    {
        mhttp_error e;

        if (c->tls_on)
        {
            int ret = mbedtls_ssl_read(&c->ssl, buf, n);

            if (ret > 0)
            {
                *got = (size_t)ret;
                return MHTTP_OK;
            }

            if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == MBEDTLS_ERR_SSL_CONN_EOF)
                return MHTTP_OK;

            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                e = wait_io(c, ret == MBEDTLS_ERR_SSL_WANT_WRITE);
                if (e)
                    return e;
                continue;
            }

            if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
                continue;

            return mh_tls_fail(c, MHTTP_ERR_RECV, ret, "TLS read");
        }
        else
        {
            int r = mh_sock_recv(c->sock, buf, n);

            if (r > 0)
            {
                *got = (size_t)r;
                return MHTTP_OK;
            }

            if (r == 0)
                return MHTTP_OK;

            if (mh_is_wouldblock(mh_last_error()))
            {
                e = wait_io(c, 0);
                if (e)
                    return e;
                continue;
            }

            return mh_fail(c->res, MHTTP_ERR_RECV, "recv failed (err %d)", mh_last_error());
        }
    }
}

static mhttp_error fill(mh_conn *c)
{
    size_t got;
    mhttp_error e = mh_conn_read(c, c->rbuf, sizeof c->rbuf, &got);

    if (e)
        return e;

    c->rpos = 0;
    c->rlen = got;
    if (!got)
        c->reof = 1;

    return MHTTP_OK;
}

mhttp_error mh_conn_read_line(mh_conn *c, char *out, size_t max)
{
    size_t n = 0;

    for (;;)
    {
        char ch;

        if (c->rpos >= c->rlen)
        {
            mhttp_error e;

            if (c->reof)
                return mh_fail(c->res, MHTTP_ERR_PROTOCOL, "unexpected end of stream");

            e = fill(c);
            if (e)
                return e;

            if (c->reof)
                return mh_fail(c->res, MHTTP_ERR_PROTOCOL, "unexpected end of stream");
        }

        ch = (char)c->rbuf[c->rpos++];

        if (ch == '\n')
        {
            if (n && out[n - 1] == '\r')
                n--;
            out[n] = 0;
            return MHTTP_OK;
        }

        if (n + 1 >= max)
            return mh_fail(c->res, MHTTP_ERR_PROTOCOL, "line too long");

        out[n++] = ch;
    }
}

mhttp_error mh_conn_read_buffered(mh_conn *c, const uint8_t **ptr, size_t *n, size_t max)
{
    if (c->rpos >= c->rlen)
    {
        mhttp_error e;

        *n = 0;
        if (c->reof)
            return MHTTP_OK;

        e = fill(c);
        if (e)
            return e;

        if (c->reof)
            return MHTTP_OK;
    }

    *ptr = c->rbuf + c->rpos;
    *n = c->rlen - c->rpos;
    if (*n > max)
        *n = max;

    c->rpos += *n;
    return MHTTP_OK;
}