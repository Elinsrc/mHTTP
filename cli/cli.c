// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CA_CACHE_PATH "certs/cacert.pem"
#define CA_MAX_AGE_SEC (30 * 24 * 3600)

void print_usage(void)
{
    fprintf(stderr,
        "mHTTP v%s (%s / %s)\n"
        "Usage: mhttp [options] URL\n"
        "\n"
        "Options:\n"
        "  -X METHOD    HTTP method (GET, POST, PUT, DELETE, etc.)\n"
        "  -H \"N: V\"    Extra HTTP header (can be repeated, max %d)\n"
        "  -d DATA      Request body data (sets default method to POST)\n"
        "  -o FILE      Write response body to FILE\n"
        "  -O           Save to a file using the remote file name (from\n"
        "               Content-Disposition or the URL path); cannot be\n"
        "               combined with -o or -C\n"
        "  -C           Resume/continue a partially downloaded FILE (requires -o)\n"
        "  --stdout     Never auto-save, always print the body to stdout\n"
        "  -L           Follow HTTP redirects\n"
        "  -k           Insecure mode (do not verify SSL/TLS certificates)\n"
        "  -i           Include HTTP response headers in output\n"
        "  -s           Do not show the download progress bar\n"
        "  --cacert F   Path to CA bundle file\n"
        "  -m SEC       Idle/stall timeout in seconds (0 = no limit, default: 30)\n"
        "\n"
        "If URL is a direct link to a file and stdout is a terminal, the file is\n"
        "saved automatically (like -O); Use --stdout to print to the terminal\n"
        "\n"
        "Press Ctrl+C to interrupt a download gracefully (partial file is kept)\n",
        MHTTP_VERSION, mhttp_tls_version(), mhttp_zlib_version(), MAX_HEADERS);
}

int parse_args(int argc, char **argv, cli_options *opts)
{
    memset(opts, 0, sizeof *opts);
    opts->timeout_sec = 30;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (!strcmp(arg, "-X"))
        {
            if (++i >= argc)
                return -1;
            opts->method = argv[i];
        }
        else if (!strcmp(arg, "-H"))
        {
            if (++i >= argc)
                return -1;
            if (opts->num_headers < MAX_HEADERS)
                opts->headers[opts->num_headers++] = argv[i];
        }
        else if (!strcmp(arg, "-d"))
        {
            if (++i >= argc)
                return -1;
            opts->data = argv[i];
        }
        else if (!strcmp(arg, "-o"))
        {
            if (++i >= argc)
                return -1;
            opts->out_file = argv[i];
        }
        else if (!strcmp(arg, "-O"))
        {
            opts->auto_save = 1;
        }
        else if (!strcmp(arg, "-m"))
        {
            if (++i >= argc)
                return -1;
            opts->timeout_sec = atoi(argv[i]);
        }
        else if (!strcmp(arg, "--cacert"))
        {
            if (++i >= argc)
                return -1;
            opts->ca_file = argv[i];
        }
        else if (!strcmp(arg, "--stdout"))
        {
            opts->force_stdout = 1;
        }
        else if (!strcmp(arg, "-L"))
        {
            opts->follow_redirects = 1;
        }
        else if (!strcmp(arg, "-k"))
        {
            opts->insecure = 1;
        }
        else if (!strcmp(arg, "-i"))
        {
            opts->show_headers = 1;
        }
        else if (!strcmp(arg, "-s"))
        {
            opts->silent = 1;
        }
        else if (!strcmp(arg, "-C"))
        {
            opts->resume = 1;
        }
        else if (arg[0] != '-')
        {
            opts->url = arg;
        }
        else
        {
            return -1;
        }
    }

    opts->headers[opts->num_headers] = NULL;
    return opts->url ? 0 : -1;
}

void setup_ca(const cli_options *opts)
{
    int ok;

    if (opts->ca_file)
        ok = (mhttp_set_ca_file(opts->ca_file) == MHTTP_OK);
    else
        ok = (mhttp_load_system_ca() == MHTTP_OK) || (mhttp_ca_update(CA_CACHE_PATH, CA_MAX_AGE_SEC) == MHTTP_OK);

    if (!ok)
        fprintf(stderr, "Warning: No CA certificates loaded. HTTPS requests may fail (use -k or --cacert)\n");
}

void fill_request(mhttp_request *req, const cli_options *opts)
{
    mhttp_request_init(req, opts->url);

    if (opts->method)
        req->method = opts->method;
    else
        req->method = opts->data ? "POST" : "GET";

    req->headers = opts->num_headers ? opts->headers : NULL;
    req->follow_redirects = opts->follow_redirects;
    req->insecure = opts->insecure;
    req->timeout_ms = opts->timeout_sec * 1000;

    if (opts->data)
    {
        req->body = opts->data;
        req->body_len = strlen(opts->data);
    }
}

const char *basename_of(const char *path)
{
    const char *slash1 = strrchr(path, '/');
    const char *slash2 = strrchr(path, '\\');
    const char *slash = slash1 > slash2 ? slash1 : slash2;
    return slash ? slash + 1 : path;
}