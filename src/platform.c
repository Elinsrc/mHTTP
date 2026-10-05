// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#include "platform.h"

#include <limits.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#endif

int64_t mh_now_ms(void)
{
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

int mh_net_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void mh_net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int mh_last_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

int mh_is_wouldblock(int err)
{
#ifdef _WIN32
    return err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
#endif
}

int mh_is_inprogress(int err)
{
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return err == EINPROGRESS || err == EINTR;
#endif
}

void mh_sock_close(mh_sock s)
{
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

void mh_sock_set_nonblock(mh_sock s)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

int mh_sock_send(mh_sock s, const void *buf, size_t len)
{
    int n = len > INT_MAX ? INT_MAX : (int)len;
    return (int)send(s, (const char *)buf, n, MSG_NOSIGNAL);
}

int mh_sock_recv(mh_sock s, void *buf, size_t len)
{
    int n = len > INT_MAX ? INT_MAX : (int)len;
    return (int)recv(s, (char *)buf, n, 0);
}

int mh_sock_wait(mh_sock s, int for_write, int timeout_ms)
{
#ifdef _WIN32
    fd_set rd, wr, ex;
    struct timeval tv;
    int r;

    FD_ZERO(&rd); FD_ZERO(&wr); FD_ZERO(&ex);
    if (for_write) FD_SET(s, &wr); else FD_SET(s, &rd);
    FD_SET(s, &ex);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    r = select(0, &rd, &wr, &ex, &tv);
    return r < 0 ? -1 : (r == 0 ? 0 : 1);
#else
    struct pollfd p;
    int r;

    p.fd = s;
    p.events = for_write ? POLLOUT : POLLIN;
    p.revents = 0;

    r = poll(&p, 1, timeout_ms);
    if (r < 0) 
        return errno == EINTR ? 0 : -1;
    return r == 0 ? 0 : 1;
#endif
}

void mh_mkdir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}
