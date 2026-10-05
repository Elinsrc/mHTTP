// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MH_CLI_OPTIONS_H
#define MH_CLI_OPTIONS_H

#include "mHTTP.h"

#define MAX_HEADERS 32

typedef struct {
    const char *url;
    const char *out_file;
    const char *data;
    const char *method;
    const char *ca_file;
    const char *headers[MAX_HEADERS + 1];
    int num_headers;
    int insecure;
    int follow_redirects;
    int show_headers;
    int silent;
    int resume;
    int auto_save;
    int force_stdout;
    int timeout_sec;
} cli_options;

void print_usage(void);

int parse_args(int argc, char **argv, cli_options *opts);

void setup_ca(const cli_options *opts);

void fill_request(mhttp_request *req, const cli_options *opts);

const char *basename_of(const char *path);

#endif