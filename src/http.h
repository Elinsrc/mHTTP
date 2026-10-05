// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_HTTP_H
#define MHTTP_HTTP_H

#include "mHTTP.h"
#include "url.h"

#include <stddef.h>
#include <stdint.h>

#define MH_LOCATION_MAX 2048

typedef struct
{
    int status;
    int64_t content_length;
    int chunked;
    int compressed;
    char location[MH_LOCATION_MAX];
} mh_head;

typedef struct mh_message {
    const char *method;
    const char *const *headers;
    const void *body;
    size_t body_len;
} mh_message;

mhttp_error mh_http_exchange(const mhttp_request *req, const mh_message *msg, const mh_url *url, int64_t connect_deadline, int stall_timeout_ms, mhttp_response *res, char *location);

#endif