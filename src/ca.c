// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "ca.h"
#include "mHTTP.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <wincrypt.h>
#endif

#define CA_URL "https://curl.se/ca/cacert.pem"
#define CA_DOWNLOAD_LIMIT (8u * 1024u * 1024u)

static mbedtls_x509_crt g_ca;
static int g_ca_inited = 0;
static int g_ca_ready = 0;

int mh_ca_init(void)
{
    mbedtls_x509_crt_init(&g_ca);
    g_ca_ready = 0;
    g_ca_inited = 1;
    return 0;
}

void mh_ca_free(void)
{
    mbedtls_x509_crt_free(&g_ca);
    g_ca_ready = 0;
    g_ca_inited = 0;
}

mbedtls_x509_crt *mh_ca_chain(void)
{
    return &g_ca;
}

int mhttp_ca_ready(void)
{
    return g_ca_ready;
}

static int has_certs(void)
{
    return g_ca.raw.p != NULL;
}

mhttp_error mhttp_set_ca_pem(const void *pem, size_t len)
{
    char *tmp;
    int ret;

    if (!g_ca_inited)
        return MHTTP_ERR_INIT;

    tmp = malloc(len + 1);
    if (!tmp)
        return MHTTP_ERR_NOMEM;

    memcpy(tmp, pem, len);
    tmp[len] = 0;

    mbedtls_x509_crt_free(&g_ca);
    mbedtls_x509_crt_init(&g_ca);
    ret = mbedtls_x509_crt_parse(&g_ca, (const unsigned char *)tmp, len + 1);
    free(tmp);

    g_ca_ready = (ret >= 0 && has_certs());
    return g_ca_ready ? MHTTP_OK : MHTTP_ERR_NO_CA;
}

static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;
    char *buf;

    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0)
    {
        fclose(f);
        return NULL;
    }

    buf = malloc((size_t)len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    buf[len] = 0;
    *size = (size_t)len;
    return buf;
}

mhttp_error mhttp_set_ca_file(const char *path)
{
    size_t size = 0;
    char *buf = read_file(path, &size);
    mhttp_error e;

    if (!buf)
        return MHTTP_ERR_IO;

    e = mhttp_set_ca_pem(buf, size);
    free(buf);
    return e;
}

#if defined(_WIN32)

static mhttp_error load_system(void)
{
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    PCCERT_CONTEXT cert = NULL;
    int count = 0;

    if (!store)
        return MHTTP_ERR_NO_CA;

    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL)
    {
        if ((cert->dwCertEncodingType & X509_ASN_ENCODING) &&
            mbedtls_x509_crt_parse_der(&g_ca, cert->pbCertEncoded, cert->cbCertEncoded) == 0)
        {
            count++;
        }
    }
    CertCloseStore(store, 0);

    g_ca_ready = count > 0;
    return count > 0 ? MHTTP_OK : MHTTP_ERR_NO_CA;
}

#elif defined(__ANDROID__)

static mhttp_error load_system(void)
{
    static const char *dirs[] =
    {
        "/apex/com.android.conscrypt/cacerts",
        "/system/etc/security/cacerts",
        NULL
    };

    for (int i = 0; dirs[i]; i++)
    {
        if (mbedtls_x509_crt_parse_path(&g_ca, dirs[i]) >= 0 && has_certs())
        {
            g_ca_ready = 1;
            return MHTTP_OK;
        }
    }
    return MHTTP_ERR_NO_CA;
}

#else

static mhttp_error load_system(void)
{
    static const char *files[] =
    {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/cert.pem",
        "/etc/ssl/ca-bundle.pem",
        NULL
    };

    for (int i = 0; files[i]; i++)
    {
        if (mhttp_set_ca_file(files[i]) == MHTTP_OK)
            return MHTTP_OK;
    }
    return MHTTP_ERR_NO_CA;
}

#endif

mhttp_error mhttp_load_system_ca(void)
{
    if (!g_ca_inited)
        return MHTTP_ERR_INIT;

    return load_system();
}

static void mkdirs_for_file(const char *path)
{
    char tmp[512];
    size_t n = strlen(path);

    if (n >= sizeof tmp)
        return;

    memcpy(tmp, path, n + 1);

    for (size_t i = 1; i < n; i++)
    {
        if (tmp[i] == '/' || tmp[i] == '\\')
        {
            char sep = tmp[i];
            tmp[i] = 0;
            mh_mkdir(tmp);
            tmp[i] = sep;
        }
    }
}

static int save_atomically(const char *path, const uint8_t *data, size_t len)
{
    char tmp[600];
    FILE *f;
    size_t written;

    mkdirs_for_file(path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    f = fopen(tmp, "wb");
    if (!f)
        return 0;

    written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len)
    {
        remove(tmp);
        return 0;
    }

    remove(path);
    return rename(tmp, path) == 0;
}

static int download_bundle(const char *path)
{
    mhttp_request rq;
    mhttp_response r;
    int saved = 0;

    mhttp_request_init(&rq, CA_URL);
    rq.timeout_ms = 30000;
    rq.follow_redirects = 1;
    rq.max_body_size = CA_DOWNLOAD_LIMIT;

    rq.insecure = !g_ca_ready;

    if (mhttp_perform(&rq, &r) == MHTTP_OK && r.body_len > 0)
    {
        mbedtls_x509_crt probe;
        mbedtls_x509_crt_init(&probe);

        if (mbedtls_x509_crt_parse(&probe, r.body, r.body_len + 1) >= 0 && probe.raw.p != NULL)
            saved = save_atomically(path, r.body, r.body_len);

        mbedtls_x509_crt_free(&probe);
    }

    mhttp_response_free(&r);
    return saved;
}

mhttp_error mhttp_ca_update(const char *path, int max_age_sec)
{
    struct stat st;
    int have  = (stat(path, &st) == 0 && st.st_size > 0);
    int fresh = have && difftime(time(NULL), st.st_mtime) < (double)max_age_sec;

    if (!fresh && download_bundle(path))
        have = 1;

    return have ? mhttp_set_ca_file(path) : MHTTP_ERR_NO_CA;
}