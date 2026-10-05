// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "term.h"

#include <stdlib.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/ioctl.h>
#endif

int term_supports_color(void)
{
    const char *no_color = getenv("NO_COLOR");

    if (no_color && no_color[0])
        return 0;

    return ISATTY(FILENO(stderr));
}

int term_width(void)
{
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);

    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi))
    {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (w > 0)
            return w;
    }
    return DEFAULT_TERM_WIDTH;
#else
    struct winsize w;

    if (ISATTY(FILENO(stderr)) && ioctl(FILENO(stderr), TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;

    return DEFAULT_TERM_WIDTH;
#endif
}

#if defined(_WIN32)
static void enable_vt_mode(DWORD std_handle)
{
    HANDLE h = GetStdHandle(std_handle);
    DWORD mode = 0;

    if (h == INVALID_HANDLE_VALUE)
        return;
    if (!GetConsoleMode(h, &mode))
        return;

    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#endif

void term_init(void)
{
#if defined(_WIN32)
    enable_vt_mode(STD_OUTPUT_HANDLE);
    enable_vt_mode(STD_ERROR_HANDLE);
#endif
}