// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_H
#define MHTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MHTTP_VERSION "0.0.1"

typedef enum mhttp_error {
    MHTTP_OK = 0,
    MHTTP_ERR_PARAM,
    MHTTP_ERR_URL,
    MHTTP_ERR_NOMEM,
    MHTTP_ERR_DNS,
    MHTTP_ERR_CONNECT,
    MHTTP_ERR_TLS,
    MHTTP_ERR_NO_CA,
    MHTTP_ERR_SEND,
    MHTTP_ERR_RECV,
    MHTTP_ERR_TIMEOUT,
    MHTTP_ERR_CANCELLED,
    MHTTP_ERR_PROTOCOL,
    MHTTP_ERR_TOO_BIG,
    MHTTP_ERR_TOO_MANY_REDIRECTS,
    MHTTP_ERR_WRITE_CB,
    MHTTP_ERR_HTTP,
    MHTTP_ERR_INIT,
    MHTTP_ERR_IO
} mhttp_error;

typedef size_t (*mhttp_write_fn)(const void *data, size_t len, void *user);
typedef int (*mhttp_progress_fn)(int64_t now, int64_t total, void *user);
typedef int (*mhttp_cancel_fn)(void *user);

typedef struct mhttp_request {
    const char *url;
    const char *method;
    const char *const *headers;
    const void *body;
    size_t body_len;
    const char *user_agent;

    int timeout_ms;
    int connect_timeout_ms;

    int follow_redirects;
    int max_redirects;
    int insecure;
    int accept_decompression;
    size_t max_body_size;

    mhttp_write_fn on_data;
    void *on_data_user;
    mhttp_progress_fn on_progress;
    void *on_progress_user;
    mhttp_cancel_fn is_cancelled;
    void *is_cancelled_user;
} mhttp_request;

typedef struct mhttp_response {
    int ok;
    int status;
    mhttp_error code;
    char error[256];
    char *headers;
    uint8_t *body;
    size_t body_len;
} mhttp_response;

mhttp_error mhttp_global_init(void);
void mhttp_global_cleanup(void);

mhttp_error mhttp_set_ca_file(const char *path);
mhttp_error mhttp_set_ca_pem(const void *pem, size_t len);
mhttp_error mhttp_load_system_ca(void);
mhttp_error mhttp_ca_update(const char *path, int max_age_sec);\
int mhttp_ca_ready(void);

void mhttp_request_init(mhttp_request *req, const char *url);
mhttp_error mhttp_perform(const mhttp_request *req, mhttp_response *res);
mhttp_error mhttp_get(const char *url, int timeout_ms, mhttp_response *res);
mhttp_error mhttp_post(const char *url, const char *content_type, const void *body, size_t len, int timeout_ms, mhttp_response *res);
void mhttp_response_free(mhttp_response *res);

int mhttp_response_header(const mhttp_response *res, const char *name, char *out, size_t outsz);

const char *mhttp_tls_version(void);
const char *mhttp_strerror(mhttp_error code);
const char *mhttp_ca_download_url(void);
const char *mhttp_ca_system_source(void);
int mhttp_ca_cert_count(void);
const char *mhttp_zlib_version(void);

#ifdef __cplusplus
}
#endif

#endif