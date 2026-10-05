// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "outname.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);

    if (p)
        memcpy(p, s, n);
    return p;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static char *percent_decode(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    size_t o = 0;

    if (!out)
        return NULL;

    for (size_t i = 0; i < len; i++)
    {
        if (s[i] == '%' && i + 2 < len)
        {
            int hi = hex_val(s[i + 1]);
            int lo = hex_val(s[i + 2]);

            if (hi >= 0 && lo >= 0)
            {
                out[o++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[o++] = s[i];
    }
    out[o] = 0;
    return out;
}

static void sanitize_filename(char *name)
{
    char *p = name;
    size_t len;

    for (char *s = name; *s; s++)
        if (*s == '/' || *s == '\\')
            p = s + 1;

    if (p != name)
        memmove(name, p, strlen(p) + 1);

    for (char *s = name; *s; s++)
    {
        unsigned char c = (unsigned char)*s;

        if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            *s = '_';
    }

    p = name;
    while (*p == '.' || *p == ' ')
        p++;
    if (p != name)
        memmove(name, p, strlen(p) + 1);

    len = strlen(name);
    while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '.'))
        name[--len] = 0;

    if (len > 200)
        name[200] = 0;
}

static char *extract_cd_filename(const char *value)
{
    const char *p;
    char *out = NULL;

    p = strstr(value, "filename*=");
    if (p)
    {
        const char *q = strstr(p, "''");

        if (q)
        {
            char raw[512];
            size_t n;

            q += 2;
            n = strcspn(q, ";\r\n");
            if (n >= sizeof raw) n = sizeof raw - 1;
            memcpy(raw, q, n);
            raw[n] = 0;

            out = percent_decode(raw);
        }
    }

    if (!out)
    {
        p = strstr(value, "filename=");
        if (p)
        {
            char raw[512];
            size_t n;

            p += 9;
            while (*p == ' ') p++;

            if (*p == '"')
            {
                p++;
                n = strcspn(p, "\"");
            }
            else
            {
                n = strcspn(p, ";\r\n");
            }

            if (n >= sizeof raw) n = sizeof raw - 1;
            memcpy(raw, p, n);
            raw[n] = 0;

            out = xstrdup(raw);
        }
    }

    if (out)
    {
        sanitize_filename(out);
        if (!out[0])
        {
            free(out);
            out = NULL;
        }
    }

    return out;
}

static char *basename_from_url(const char *url)
{
    const char *scheme_sep = strstr(url, "://");
    const char *path_start;
    const char *end;
    char *raw, *decoded;
    size_t n;

    path_start = scheme_sep ? strchr(scheme_sep + 3, '/') : strchr(url, '/');
    if (!path_start || !*(path_start + 1))
        return NULL;

    path_start++;

    for (const char *s = path_start; *s && *s != '?' && *s != '#'; s++)
        if (*s == '/')
            path_start = s + 1;

    end = path_start;
    while (*end && *end != '?' && *end != '#')
        end++;

    n = (size_t)(end - path_start);
    if (n == 0)
        return NULL;

    raw = malloc(n + 1);
    if (!raw)
        return NULL;
    memcpy(raw, path_start, n);
    raw[n] = 0;

    decoded = percent_decode(raw);
    free(raw);
    if (!decoded)
        return NULL;

    sanitize_filename(decoded);
    if (!decoded[0])
    {
        free(decoded);
        return NULL;
    }

    return decoded;
}

char *cli_resolve_output_name(const mhttp_response *res, const char *url)
{
    char cd_value[512];
    char *name = NULL;

    if (res && mhttp_response_header(res, "Content-Disposition", cd_value, sizeof cd_value))
        name = extract_cd_filename(cd_value);

    if (!name && url)
        name = basename_from_url(url);

    if (!name)
        name = xstrdup("download");

    return name;
}

char *cli_unique_path(const char *path)
{
    FILE *f;
    char *result;
    char base[480], ext[64];
    const char *dot;
    int n;

    f = fopen(path, "rb");
    if (!f)
        return xstrdup(path);
    fclose(f);

    dot = strrchr(path, '.');

    if (dot && dot != path)
    {
        size_t base_len = (size_t)(dot - path);

        if (base_len >= sizeof base) base_len = sizeof base - 1;
        memcpy(base, path, base_len);
        base[base_len] = 0;
        snprintf(ext, sizeof ext, "%s", dot);
    }
    else
    {
        snprintf(base, sizeof base, "%s", path);
        ext[0] = 0;
    }

    result = malloc(600);
    if (!result)
        return xstrdup(path);

    for (n = 1; n < 1000; n++)
    {
        snprintf(result, 600, "%s (%d)%s", base, n, ext);

        f = fopen(result, "rb");
        if (!f)
            return result;
        fclose(f);
    }

    free(result);
    return xstrdup(path);
}

int cli_url_looks_like_file(const char *url)
{
    static const char *not_files[] = {
        "html", "htm", "php", "asp", "aspx", "jsp", "cgi", "json", "xml", NULL
    };
    char *name;
    const char *dot;
    int ok = 0;

    if (!url)
        return 0;

    name = basename_from_url(url);
    if (!name)
        return 0;

    dot = strrchr(name, '.');
    if (dot && dot != name && dot[1])
    {
        char ext[16];
        size_t n = strlen(dot + 1);

        if (n < sizeof ext)
        {
            int has_alpha = 0;

            ok = 1;
            for (size_t i = 0; i < n; i++)
            {
                unsigned char c = (unsigned char)dot[1 + i];

                if (!isalnum(c))
                {
                    ok = 0;
                    break;
                }
                if (isalpha(c))
                    has_alpha = 1;
                ext[i] = (char)tolower(c);
            }
            ext[n] = 0;

            if (!has_alpha)
                ok = 0;

            for (int i = 0; ok && not_files[i]; i++)
                if (!strcmp(ext, not_files[i]))
                    ok = 0;
        }
    }

    free(name);
    return ok;
}

static int has_prefix_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != *prefix)
            return 0;
    return 1;
}

static int is_text_type(const char *ct)
{
    static const char *text_types[] = {
        "text/", 
        "application/json", 
        "application/xml",
        "application/javascript",
        "application/x-www-form-urlencoded", 
        NULL
    };
    const char *end = ct + strcspn(ct, ";");

    while (*ct == ' ')
        ct++;

    for (int i = 0; text_types[i]; i++)
        if (has_prefix_ci(ct, text_types[i]))
            return 1;

    for (const char *p = ct; p < end; p++)
        if (*p == '+' && (has_prefix_ci(p, "+json") || has_prefix_ci(p, "+xml")))
            return 1;

    return 0;
}

int cli_response_is_file(const mhttp_response *res, const char *url)
{
    char v[512];

    if (!res || res->status < 200 || res->status >= 300)
        return 0;

    if (mhttp_response_header(res, "Content-Disposition", v, sizeof v))
    {
        if (has_prefix_ci(v, "attachment") || strstr(v, "filename"))
            return 1;
    }

    if (mhttp_response_header(res, "Content-Type", v, sizeof v))
        return !is_text_type(v);

    return cli_url_looks_like_file(url);
}