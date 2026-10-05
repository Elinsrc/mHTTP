// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MH_CLI_OUTNAME_H
#define MH_CLI_OUTNAME_H

#include "mHTTP.h"

char *cli_resolve_output_name(const mhttp_response *res, const char *url);

char *cli_unique_path(const char *path);

int cli_url_looks_like_file(const char *url);

int cli_response_is_file(const mhttp_response *res, const char *url);

#endif