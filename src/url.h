// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_URL_H
#define MHTTP_URL_H

#include "mHTTP.h"

typedef struct {
    int https;
    int port;
    char host[256];
    char hostport[300];
    char *path;
} mh_url;

mhttp_error mh_url_parse(const char *s, mh_url *out, mhttp_response *res);
void mh_url_free(mh_url *u);

char *mh_url_resolve(const mh_url *base, const char *location);

#endif
