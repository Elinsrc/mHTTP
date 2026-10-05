// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MH_CLI_TRANSFER_H
#define MH_CLI_TRANSFER_H

#include "mHTTP.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    const char *out_path;
    char *auto_out_path;
    int resume_requested;
    int write_decided;
    int64_t resume_offset;
    int64_t session_bytes;

    int auto_name;
    int auto_detect;
    int to_stdout;
    int discard;
    const char *url;

    const mhttp_response *res;
    int64_t start_ms;
    int64_t last_draw_ms;
    int64_t tick_bytes;
    double speed_ema;
    int started;
    int use_color;
    int term_width;
    const char *label;
} transfer_ctx;

size_t write_to_file(const void *data, size_t len, void *user);
int on_progress(int64_t now, int64_t total, void *user);

#endif