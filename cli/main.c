// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "mHTTP.h"
#include "cli.h"
#include "fmt.h"
#include "outname.h"
#include "sighandler.h"
#include "term.h"
#include "transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(const cli_options *user_opts)
{
    cli_options local = *user_opts;
    const cli_options *opts = &local;
    mhttp_request req;
    mhttp_response res;
    mhttp_error err;
    transfer_ctx tc;
    const char *combined_headers[MAX_HEADERS + 2];
    char range_value[64];
    int save_to_file;
    int show_progress;
    int auto_mode = 0;
    int exit_code = 0;

    memset(&req, 0, sizeof req);
    memset(&res, 0, sizeof res);
    memset(&tc, 0, sizeof tc);

    if (opts->resume && !opts->out_file)
    {
        fprintf(stderr, "mhttp: -C requires -o FILE\n");
        return 2;
    }

    if (opts->auto_save && opts->out_file)
    {
        fprintf(stderr, "mhttp: -O cannot be combined with -o\n");
        return 2;
    }

    if (opts->auto_save && opts->resume)
    {
        fprintf(stderr, "mhttp: -O cannot be combined with -C (file name is not known in advance)\n");
        return 2;
    }

    if (!local.out_file && !local.auto_save && !local.resume && !local.force_stdout && ISATTY(FILENO(stdout)))
    {
        auto_mode = 1;

        if (cli_url_looks_like_file(local.url))
            local.auto_save = 1;
        else
            tc.auto_detect = 1;
    }

    save_to_file = (opts->out_file != NULL) || opts->auto_save;
    show_progress = save_to_file && !opts->silent;

    tc.use_color = term_supports_color();
    tc.term_width = term_width();
    tc.res = &res;
    tc.url = opts->url;

    if (!opts->insecure)
        setup_ca(opts);

    fill_request(&req, opts);
    req.is_cancelled = is_cancelled_cb;
    req.is_cancelled_user = NULL;

    if (auto_mode)
        req.follow_redirects = 1;

    if (save_to_file)
    {
        if (opts->auto_save)
        {
            tc.auto_name = 1;
        }
        else
        {
            tc.out_path = opts->out_file;
            tc.label = basename_of(opts->out_file);
            tc.resume_requested = opts->resume;

            if (opts->resume)
            {
                FILE *probe = fopen(opts->out_file, "rb");
                if (probe)
                {
                    fseek(probe, 0, SEEK_END);
                    long sz = ftell(probe);
                    fclose(probe);
                    if (sz > 0)
                        tc.resume_offset = (int64_t)sz;
                }
            }

            tc.f = fopen(opts->out_file, tc.resume_offset > 0 ? "ab" : "wb");
            if (!tc.f)
            {
                perror(opts->out_file);
                return 1;
            }

            if (tc.resume_offset > 0)
            {
                int i;
                char sz_buf[32];

                for (i = 0; i < opts->num_headers; i++)
                    combined_headers[i] = opts->headers[i];

                snprintf(range_value, sizeof range_value, "Range: bytes=%lld-", (long long)tc.resume_offset);
                combined_headers[i++] = range_value;
                combined_headers[i] = NULL;

                req.headers = combined_headers;

                if (!opts->silent)
                {
                    format_size(tc.resume_offset, sz_buf, sizeof sz_buf);
                    if (tc.use_color)
                        fprintf(stderr, C_CYAN "\xE2\x86\xBB Resuming" C_RESET " %s from %s\n", tc.label, sz_buf);
                    else
                        fprintf(stderr, "Resuming %s from %s\n", tc.label, sz_buf);
                }
            }
        }

        req.on_data = write_to_file;
        req.on_data_user = &tc;
        req.max_body_size = 0;

        if (show_progress)
        {
            req.on_progress = on_progress;
            req.on_progress_user = &tc;
        }
    }
    else if (tc.auto_detect)
    {
        req.on_data = write_to_file;
        req.on_data_user = &tc;
        req.max_body_size = 0;

        if (!opts->silent)
        {
            req.on_progress = on_progress;
            req.on_progress_user = &tc;
        }
    }

    err = mhttp_perform(&req, &res);

    if (tc.f)
        fclose(tc.f);

    if (tc.auto_detect && tc.auto_name)
    {
        save_to_file = 1;
        show_progress = !opts->silent;
    }

    if (err != MHTTP_OK && err != MHTTP_ERR_CANCELLED && opts->out_file
        && tc.resume_offset == 0 && tc.session_bytes == 0)
    {
        remove(opts->out_file);
    }

    if (show_progress && tc.started && ISATTY(FILENO(stderr)))
        fputc('\n', stderr);

    if (err == MHTTP_ERR_CANCELLED)
    {
        if (!opts->silent)
        {
            if (tc.out_path)
            {
                int64_t saved = tc.resume_offset + tc.session_bytes;
                char sz_buf[32];

                format_size(saved, sz_buf, sizeof sz_buf);

                if (tc.use_color)
                    fprintf(stderr, "\n" C_YELLOW "\xE2\x9A\xA0 Interrupted" C_RESET " \xE2\x80\x94 %s saved", sz_buf);
                else
                    fprintf(stderr, "\nInterrupted - %s saved", sz_buf);

                fprintf(stderr, " to %s", tc.out_path);

                if (opts->out_file)
                    fprintf(stderr, " (resume with -C)\n");
                else
                    fputc('\n', stderr);
            }
            else
            {
                fprintf(stderr, "\nInterrupted\n");
            }
        }

        exit_code = 130;
    }
    else if (err == MHTTP_ERR_HTTP && res.status == 416 && opts->resume)
    {
        if (!opts->silent)
        {
            if (tc.use_color)
                fprintf(stderr, C_GREEN "\xE2\x9C\x94 Already complete" C_RESET " \xE2\x80\x94 %s\n", tc.label ? tc.label : opts->out_file);
            else
                fprintf(stderr, "Already complete - %s\n", tc.label ? tc.label : opts->out_file);
        }

        exit_code = 0;
    }
    else if (err != MHTTP_OK)
    {
        if (!opts->silent)
        {
            if (tc.use_color)
                fprintf(stderr, C_RED "\xE2\x9C\x98 Failed:" C_RESET " %s\n", res.error);
            else
                fprintf(stderr, "Failed: %s\n", res.error);
        }

        exit_code = 1;
    }
    else if (save_to_file && !opts->silent)
    {
        int64_t total_bytes = tc.resume_offset + tc.session_bytes;
        int64_t elapsed_ms = tc.started ? (now_ms() - tc.start_ms) : 0;
        double avg_speed = elapsed_ms > 0 ? (double)tc.session_bytes * 1000.0 / (double)elapsed_ms : 0.0;
        char total_str[32], avg_str[32], time_str[32];

        format_size(total_bytes, total_str, sizeof total_str);
        format_speed(avg_speed, avg_str, sizeof avg_str);
        format_duration(elapsed_ms / 1000, time_str, sizeof time_str);

        if (tc.use_color)
            fprintf(stderr, C_GREEN "\xE2\x9C\x94 Done" C_RESET " \xE2\x80\x94 %s", tc.out_path ? tc.out_path : "(empty response, nothing saved)");
        else
            fprintf(stderr, "Done - %s", tc.out_path ? tc.out_path : "(empty response, nothing saved)");

        if (tc.started)
            fprintf(stderr, " (%s in %s, avg %s)\n", total_str, time_str, avg_str);
        else
            fputc('\n', stderr);
    }

    if (res.headers && opts->show_headers)
        fputs(res.headers, stdout);

    if (!save_to_file && res.body)
        fwrite(res.body, 1, res.body_len, stdout);

    free(tc.auto_out_path);
    mhttp_response_free(&res);
    return exit_code;
}

int main(int argc, char **argv)
{
    cli_options opts;
    int exit_code;

    term_init();
    install_signal_handlers();

    if (parse_args(argc, argv, &opts) != 0)
    {
        print_usage();
        return 2;
    }

    if (mhttp_global_init() != MHTTP_OK)
    {
        fprintf(stderr, "Error: Failed to initialize mHTTP library\n");
        return 1;
    }

    exit_code = run(&opts);

    mhttp_global_cleanup();
    return exit_code;
}