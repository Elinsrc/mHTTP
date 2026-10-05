// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MH_CLI_SIGHANDLER_H
#define MH_CLI_SIGHANDLER_H

#include <signal.h>

extern volatile sig_atomic_t g_cancelled;

void install_signal_handlers(void);

int is_cancelled_cb(void *user);

#endif