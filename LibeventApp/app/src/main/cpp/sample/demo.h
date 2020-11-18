//
// Created by Administrator on 2020/11/18 0018.
//

#ifndef LIBEVENTAPP_DEMO_H
#define LIBEVENTAPP_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include<stdio.h>
#include<string.h>
#include<errno.h>

#include<event.h>
//#include<event2/bufferevent.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef _WIN32
#include <sys/queue.h>
#include <unistd.h>
#include <sys/time.h>
#else
#include <windows.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <malloc.h>

typedef int ev_socklen_t;
typedef int evutil_socket_t;

 struct event * event_new(struct event_base *pBase, int fd, int event, void (*cb)(int, short, void *), void *ctx);

struct bufferevent *bufferevent_socket_new(struct event_base *pBase, int sockfd, int flag);

int
evutil_make_listen_socket_reuseable(int sock);

int
evutil_make_listen_socket_reuseable_port(evutil_socket_t sock);

int
evutil_closesocket(evutil_socket_t sock);


#ifdef __cplusplus
}
#endif

#endif //LIBEVENTAPP_DEMO_H
