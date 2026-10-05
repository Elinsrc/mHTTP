// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MH_CLI_FMT_H
#define MH_CLI_FMT_H

#include <stddef.h>
#include <stdint.h>

int64_t now_ms(void);

void format_size(int64_t bytes, char *buf, size_t buf_size);
void format_speed(double bytes_per_sec, char *buf, size_t buf_size);
void format_duration(int64_t seconds, char *buf, size_t buf_size);

#endif