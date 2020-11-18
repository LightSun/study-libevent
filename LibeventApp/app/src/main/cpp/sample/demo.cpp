//
// Created by Administrator on 2020/11/18 0018.
//
#include "demo.h"

event *
event_new(event_base *pBase, int fd, int events, void (*cb)(int, short, void *), void *ctx) {
    struct event* ev = new event();
    event_set(ev, fd, events, cb, ctx);
    ev->ev_base = pBase;
    return ev;
}

bufferevent *bufferevent_socket_new(event_base *pBase, int fd, int flag) {
    struct bufferevent *bufev;
    bufev = bufferevent_new(fd, nullptr, nullptr, nullptr, pBase);
    return bufev;
}
int
evutil_make_listen_socket_reuseable(int sock)
{
#if defined(SO_REUSEADDR) && !defined(_WIN32)
    int one = 1;
    /* REUSEADDR on Unix means, "don't hang on to this address after the
     * listener is closed."  On Windows, though, it means "don't keep other
     * processes from binding to this address while we're using it. */
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*) &one,
                      (ev_socklen_t)sizeof(one));
#else
    return 0;
#endif
}

int
evutil_make_listen_socket_reuseable_port(evutil_socket_t sock)
{
#if defined __linux__ && defined(SO_REUSEPORT)
    int one = 1;
    /* REUSEPORT on Linux 3.9+ means, "Multiple servers (processes or
     * threads) can bind to the same port if they each set the option. */
    return setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void*) &one,
                      (ev_socklen_t)sizeof(one));
#else
    return 0;
#endif
}

int
evutil_closesocket(evutil_socket_t sock)
{
#ifndef _WIN32
    return close(sock);
#else
    return closesocket(sock);
#endif
}