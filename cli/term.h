// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MH_CLI_TERM_H
#define MH_CLI_TERM_H

#if defined(_WIN32)
#include <io.h>
#define ISATTY(fd) _isatty(fd)
#define FILENO(f)  _fileno(f)
#else
#include <unistd.h>
#define ISATTY(fd) isatty(fd)
#define FILENO(f)  fileno(f)
#endif

#define C_RESET "\033[0m"
#define C_DIM "\033[2m"
#define C_BOLD "\033[1m"
#define C_GREEN "\033[32m"
#define C_CYAN "\033[36m"
#define C_YELLOW "\033[33m"
#define C_RED "\033[31m"
#define C_CLR_EOL "\033[K"

#define DEFAULT_TERM_WIDTH 80

void term_init(void);
int term_supports_color(void);
int term_width(void);

#endif