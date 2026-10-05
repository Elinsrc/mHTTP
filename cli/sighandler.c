// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "sighandler.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

volatile sig_atomic_t g_cancelled = 0;

#if defined(_WIN32)

static BOOL WINAPI console_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT)
    {
        g_cancelled = 1;
        return TRUE;
    }
    return FALSE;
}

void install_signal_handlers(void)
{
    SetConsoleCtrlHandler(console_handler, TRUE);
}

#else

static void handle_sigint(int signo)
{
    (void)signo;
    g_cancelled = 1;
}

void install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

#endif

int is_cancelled_cb(void *user)
{
    (void)user;
    return g_cancelled;
}