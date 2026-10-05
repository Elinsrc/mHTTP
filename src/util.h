// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_UTIL_H
#define MHTTP_UTIL_H

#include <stddef.h>
#include "mHTTP.h"

int mh_ci_ncmp(const char *a, const char *b, size_t n);
int mh_ci_contains(const char *haystack, const char *needle);
char *mh_strdup(const char *s);

const char *mh_header_value(const char *line, const char *name);

mhttp_error mh_fail(mhttp_response *res, mhttp_error code, const char *fmt, ...);

mhttp_error mh_fail_wait(mhttp_response *res, mhttp_error code);

int mh_is_cancelled(const mhttp_request *req);

typedef struct {
    char  *p;
    size_t len;
    size_t cap;
} mh_strbuf;

int  mh_sb_addf(mh_strbuf *b, const char *fmt, ...);
void mh_sb_free(mh_strbuf *b);

#endif
