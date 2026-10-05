// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "transfer.h"
#include "cli.h"
#include "fmt.h"
#include "outname.h"
#include "sighandler.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>

#define PROGRESS_UPDATE_MS 120
#define SPEED_EMA_ALPHA 0.25
#define MIN_BAR_WIDTH 10
#define MAX_BAR_WIDTH 50

size_t write_to_file(const void *data, size_t len, void *user)
{
    transfer_ctx *tc = (transfer_ctx *)user;
    size_t written;

    if (!tc->write_decided)
    {
        tc->write_decided = 1;

        if (tc->auto_detect)
        {
            if (cli_response_is_file(tc->res, tc->url))
                tc->auto_name = 1;
            else
                tc->to_stdout = 1;
        }
        else if (tc->res->status < 200 || tc->res->status >= 300)
        {
            tc->discard = 1;
        }

        if (!tc->discard && !tc->to_stdout)
        {
            if (tc->auto_name)
            {
                char *name = cli_resolve_output_name(tc->res, tc->url);
                char *unique = name ? cli_unique_path(name) : NULL;

                free(name);

                if (unique)
                {
                    tc->auto_out_path = unique;
                    tc->out_path = unique;
                    tc->label = basename_of(unique);
                    tc->f = fopen(unique, "wb");
                }

                if (!tc->f)
                {
                    if (unique)
                        perror(unique);
                    return 0;
                }
            }
            else if (tc->resume_requested && tc->resume_offset > 0 && tc->res->status != 206)
            {
                if (tc->f)
                    fclose(tc->f);

                tc->f = fopen(tc->out_path, "wb");
                if (!tc->f)
                    return 0;

                tc->resume_offset = 0;
            }
        }
    }

    if (tc->discard)
        return len;

    if (tc->to_stdout)
        return fwrite(data, 1, len, stdout);

    if (!tc->f)
        return 0;

    written = fwrite(data, 1, len, tc->f);
    if (written == len)
        tc->session_bytes += (int64_t)written;

    return written;
}

static void render_bar(char *out, size_t out_size, double frac, int width, int use_color)
{
    int filled, i;
    size_t pos = 0;

    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    filled = (int)(frac * width + 0.5);
    if (filled > width) filled = width;

    pos += (size_t)snprintf(out + pos, out_size - pos, "[");
    if (use_color)
        pos += (size_t)snprintf(out + pos, out_size - pos, C_GREEN);

    for (i = 0; i < filled && pos < out_size - 1; i++)
        out[pos++] = '#';

    if (use_color)
        pos += (size_t)snprintf(out + pos, out_size - pos, C_RESET C_DIM);

    for (i = filled; i < width && pos < out_size - 1; i++)
        out[pos++] = '-';

    if (use_color)
        pos += (size_t)snprintf(out + pos, out_size - pos, C_RESET);

    pos += (size_t)snprintf(out + pos, out_size - pos, "]");
    out[pos < out_size ? pos : out_size - 1] = 0;
}

static char spinner_frame(int64_t t)
{
    static const char frames[] = "|/-\\";
    int idx = (int)((t / 120) % 4);
    return frames[idx];
}

static void draw_known_total(transfer_ctx *tc, int64_t abs_now, int64_t abs_total)
{
    char now_str[32], total_str[32], speed_str[32], eta_str[32], bar[256];
    double frac = abs_total > 0 ? (double)abs_now / (double)abs_total : 0.0;
    int64_t remaining = abs_total - abs_now;
    int64_t eta_sec = tc->speed_ema > 1.0 ? (int64_t)((double)remaining / tc->speed_ema) : -1;
    int reserved = 46;
    int bar_width = tc->term_width - reserved;

    if (bar_width < MIN_BAR_WIDTH) 
        bar_width = MIN_BAR_WIDTH;
    if (bar_width > MAX_BAR_WIDTH) 
        bar_width = MAX_BAR_WIDTH;

    format_size(abs_now, now_str, sizeof now_str);
    format_size(abs_total, total_str, sizeof total_str);
    format_speed(tc->speed_ema, speed_str, sizeof speed_str);
    format_duration(eta_sec, eta_str, sizeof eta_str);
    render_bar(bar, sizeof bar, frac, bar_width, tc->use_color);

    if (tc->use_color)
        fprintf(stderr, "\r%s %s%6.1f%%%s  %s / %s  %s%s%s  ETA %s%s%s%s",
            bar, C_BOLD, frac * 100.0, C_RESET,
            now_str, total_str,
            C_CYAN, speed_str, C_RESET,
            C_YELLOW, eta_str, C_RESET,
            C_CLR_EOL);
    else
        fprintf(stderr, "\r%s %6.1f%%  %s / %s  %s  ETA %s%s", bar, frac * 100.0, now_str, total_str, speed_str, eta_str, C_CLR_EOL);
}

static void draw_unknown_total(transfer_ctx *tc, int64_t abs_now, int64_t t)
{
    char now_str[32], speed_str[32];
    char spin = spinner_frame(t);

    format_size(abs_now, now_str, sizeof now_str);
    format_speed(tc->speed_ema, speed_str, sizeof speed_str);

    if (tc->use_color)
        fprintf(stderr, "\r%s%c%s  %s downloaded  %s%s%s%s", C_CYAN, spin, C_RESET, now_str, C_CYAN, speed_str, C_RESET, C_CLR_EOL);
    else
        fprintf(stderr, "\r%c  %s downloaded  %s%s", spin, now_str, speed_str, C_CLR_EOL);
}

int on_progress(int64_t now, int64_t total, void *user)
{
    transfer_ctx *tc = (transfer_ctx *)user;
    int64_t t = now_ms();
    int64_t elapsed_since_last;
    int64_t abs_now = tc->resume_offset + now;
    int64_t abs_total = total > 0 ? tc->resume_offset + total : 0;
    int final = (total > 0 && now >= total);
    int is_tty = ISATTY(FILENO(stderr));

    if (g_cancelled)
        return 1;

    if (tc->discard || tc->to_stdout)
        return 0;

    if (!tc->started)
    {
        tc->start_ms = t;
        tc->last_draw_ms = t;
        tc->tick_bytes = 0;
        tc->speed_ema = 0.0;
        tc->started = 1;

        if (tc->label)
        {
            if (tc->use_color)
                fprintf(stderr, C_BOLD "\xE2\x86\x93 %s" C_RESET "\n", tc->label);
            else
                fprintf(stderr, "Downloading: %s\n", tc->label);
        }
    }

    if (!final && is_tty && t - tc->last_draw_ms < PROGRESS_UPDATE_MS)
        return 0;

    if (!is_tty && !final && t - tc->last_draw_ms < 1000)
        return 0;

    elapsed_since_last = t - tc->last_draw_ms;
    if (elapsed_since_last <= 0)
        elapsed_since_last = 1;

    {
        int64_t delta_bytes = now - tc->tick_bytes;
        double inst_speed = (double)delta_bytes * 1000.0 / (double)elapsed_since_last;

        if (tc->speed_ema <= 0.0)
            tc->speed_ema = inst_speed;
        else
            tc->speed_ema = SPEED_EMA_ALPHA * inst_speed + (1.0 - SPEED_EMA_ALPHA) * tc->speed_ema;
    }

    if (abs_total > 0)
        draw_known_total(tc, abs_now, abs_total);
    else
        draw_unknown_total(tc, abs_now, t);

    if (!is_tty)
        fputc('\n', stderr);

    fflush(stderr);

    tc->last_draw_ms = t;
    tc->tick_bytes = now;
    return 0;
}