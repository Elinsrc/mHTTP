// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "fmt.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

int64_t now_ms(void)
{
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void format_size(int64_t bytes, char *buf, size_t buf_size)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double count = (double)bytes;

    while (count >= 1024.0 && i < 4)
    {
        count /= 1024.0;
        i++;
    }

    if (i == 0)
        snprintf(buf, buf_size, "%lld B", (long long)bytes);
    else
        snprintf(buf, buf_size, "%.2f %s", count, units[i]);
}

void format_speed(double bytes_per_sec, char *buf, size_t buf_size)
{
    char size_str[32];

    if (bytes_per_sec < 0)
        bytes_per_sec = 0;

    format_size((int64_t)bytes_per_sec, size_str, sizeof size_str);
    snprintf(buf, buf_size, "%s/s", size_str);
}

void format_duration(int64_t seconds, char *buf, size_t buf_size)
{
    int64_t h, m, s;

    if (seconds < 0)
    {
        snprintf(buf, buf_size, "--:--");
        return;
    }

    h = seconds / 3600;
    m = (seconds % 3600) / 60;
    s = seconds % 60;

    if (h > 0)
        snprintf(buf, buf_size, "%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
    else
        snprintf(buf, buf_size, "%lld:%02lld", (long long)m, (long long)s);
}