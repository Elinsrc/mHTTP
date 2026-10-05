// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_CONN_H
#define MHTTP_CONN_H

#include "mHTTP.h"
#include "platform.h"
#include "url.h"

#include <mbedtls/build_info.h>
#include <mbedtls/ssl.h>

typedef struct {
    mh_sock sock;
    int tls_on;
    int tls_init;

    const mhttp_request *req;
    mhttp_response *res;
    int64_t deadline;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;

    uint8_t rbuf[16384];
    size_t rpos, rlen;
    int reof;
} mh_conn;

mh_conn *mh_conn_new(const mhttp_request *req, mhttp_response *res, int64_t deadline);
void mh_conn_free(mh_conn *c);

mhttp_error mh_conn_connect(mh_conn *c, const mh_url *u, int64_t deadline);
mhttp_error mh_conn_wait(mh_conn *c, int for_write, int64_t deadline);

void mh_conn_set_deadline(mh_conn *c, int64_t deadline);

mhttp_error mh_conn_write(mh_conn *c, const uint8_t *data, size_t len);
mhttp_error mh_conn_read(mh_conn *c, uint8_t *buf, size_t len, size_t *got);

mhttp_error mh_conn_read_line(mh_conn *c, char *out, size_t max);
mhttp_error mh_conn_read_buffered(mh_conn *c, const uint8_t **ptr, size_t *n, size_t max);

#endif