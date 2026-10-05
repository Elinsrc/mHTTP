// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Elinsrc

#ifndef MHTTP_PLATFORM_H
#define MHTTP_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET mh_sock;
#define MH_INVALID_SOCK INVALID_SOCKET
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int mh_sock;
#define MH_INVALID_SOCK (-1)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int64_t mh_now_ms(void);

int  mh_net_init(void);
void mh_net_cleanup(void);

int  mh_last_error(void);
int  mh_is_wouldblock(int err);
int  mh_is_inprogress(int err);

void mh_sock_close(mh_sock s);
void mh_sock_set_nonblock(mh_sock s);
int  mh_sock_send(mh_sock s, const void *buf, size_t len);
int  mh_sock_recv(mh_sock s, void *buf, size_t len);

int  mh_sock_wait(mh_sock s, int for_write, int timeout_ms);

void mh_mkdir(const char *path);

#endif
