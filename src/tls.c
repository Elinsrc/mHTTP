// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "tls.h"
#include "ca.h"
#include "util.h"

#include <string.h>

#include <mbedtls/error.h>
#include <mbedtls/version.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

static int rng_callback(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    return psa_generate_random(out, len) == PSA_SUCCESS ? 0 : -1;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    mh_sock s = *(mh_sock *)ctx;
    int r = mh_sock_send(s, buf, len);

    if (r < 0)
        return mh_is_wouldblock(mh_last_error()) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return r;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    mh_sock s = *(mh_sock *)ctx;
    int r = mh_sock_recv(s, buf, len);

    if (r < 0)
        return mh_is_wouldblock(mh_last_error()) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return r;
}

mhttp_error mh_tls_fail(mh_conn *c, mhttp_error code, int ret, const char *what)
{
    char text[128];
    mbedtls_strerror(ret, text, sizeof text);
    return mh_fail(c->res, code, "%s: %s (-0x%04X)", what, text, (unsigned)(-ret));
}

static mhttp_error cert_verify_fail(mh_conn *c)
{
    char info[256];
    size_t n;
    uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);

    mbedtls_x509_crt_verify_info(info, sizeof info, "", flags);

    n = strlen(info);
    while (n && (info[n - 1] == '\n' || info[n - 1] == ' ')) info[--n] = 0;

    return mh_fail(c->res, MHTTP_ERR_TLS, "certificate verification failed: %s", info);
}

static mhttp_error configure(mh_conn *c, const mh_url *u)
{
    int ret;

    ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret) 
        return mh_tls_fail(c, MHTTP_ERR_TLS, ret, "ssl_config_defaults");

    mbedtls_ssl_conf_min_tls_version(&c->conf, MBEDTLS_SSL_VERSION_TLS1_2);

    mbedtls_ssl_conf_rng(&c->conf, rng_callback, NULL);

    if (c->req->insecure) 
    {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    } 
    else 
    {
        if (!mhttp_ca_ready())
            return mh_fail(c->res, MHTTP_ERR_NO_CA, "no CA certificates loaded (set insecure=1 to skip verification)");
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&c->conf, mh_ca_chain(), NULL);
    }

    ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (ret) 
        return mh_tls_fail(c, MHTTP_ERR_TLS, ret, "ssl_setup");

    ret = mbedtls_ssl_set_hostname(&c->ssl, u->host);
    if (ret) 
        return mh_tls_fail(c, MHTTP_ERR_TLS, ret, "set_hostname");

    mbedtls_ssl_set_bio(&c->ssl, &c->sock, bio_send, bio_recv, NULL);
    return MHTTP_OK;
}

static mhttp_error handshake(mh_conn *c)
{
    for (;;) 
    {
        int ret = mbedtls_ssl_handshake(&c->ssl);

        if (ret == 0) 
            return MHTTP_OK;

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) 
        {
            mhttp_error e = mh_conn_wait(c, ret == MBEDTLS_ERR_SSL_WANT_WRITE, c->deadline);
            if (e) 
                return mh_fail_wait(c->res, e);
            continue;
        }
        if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) 
            return cert_verify_fail(c);

        return mh_tls_fail(c, MHTTP_ERR_TLS, ret, "TLS handshake");
    }
}

mhttp_error mh_tls_start(mh_conn *c, const mh_url *u)
{
    mhttp_error e;

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    c->tls_init = 1;

    if ((e = configure(c, u))) 
        return e;
    
    if ((e = handshake(c)))    
        return e;

    c->tls_on = 1;
    return MHTTP_OK;
}

void mh_tls_close(mh_conn *c)
{
    if (!c->tls_init) 
        return;

    if (c->tls_on) 
        mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    c->tls_init = c->tls_on = 0;
}

const char *mhttp_tls_version(void)
{
    static char buf[32];
    mbedtls_version_get_string_full(buf);
    return buf;
}
