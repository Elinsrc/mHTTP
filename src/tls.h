// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_TLS_H
#define MHTTP_TLS_H

#include "conn.h"

mhttp_error mh_tls_start(mh_conn *c, const mh_url *u);

mhttp_error mh_tls_fail(mh_conn *c, mhttp_error code, int ret, const char *what);

void mh_tls_close(mh_conn *c);

#endif
