//
// Created by Administrator on 2020/11/19 0019.
//

#ifndef LIBEVENTAPP_IO_CONFIG_H
#define LIBEVENTAPP_IO_CONFIG_H

#include <sys/types.h>

#include "../sys/queue.h"
#include "event.h"
#include "../sample/and_log.h"
//#include <err.h>
#include <errno.h>

#define errx(c, s, ...)\
LOGE("%s "#s, __VA_ARGS__)
#define warn(s, ...) \
LOGD("%s "#s,__VA_ARGS__)


#define HAVE_SOCKADDR_STORAGE 1


#endif //LIBEVENTAPP_IO_CONFIG_H
