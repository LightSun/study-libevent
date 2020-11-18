/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Niels Provos.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include <event.h>

#include "config.h"
#include "io.h"

#ifndef HAVE_SOCKADDR_STORAGE
struct sockaddr_storage {
	u_char iamasuckyoperatingsystem[2048];
};
#endif

/* An libio method that creates a new file descriptor from a listening
 * socket.x
 */

ssize_t
io_method_accept(int fd, void *buf, size_t size)
{
	struct sockaddr_storage from;
	size_t fromlen = sizeof(from);

	if (size < sizeof(int))
		return (0);

	fd = accept(fd, (struct sockaddr *)&from, &fromlen);
	if (fd == -1)
		return (-1);

	*(int *)buf = fd;
	buf += sizeof(int);

	if (size < sizeof(int) + fromlen + sizeof(fromlen))
		return (sizeof (int));

	memcpy(buf, &fromlen, sizeof(fromlen));
	buf += sizeof(fromlen);

	memcpy(buf, &from, fromlen);

	return (sizeof (int) + sizeof(fromlen) + fromlen);
}

/*
 * Create a new socket with the given address and port, the method
 * is either bind or connect
 */

int
io_make_socket(int (*f)(int, const struct sockaddr *, socklen_t),
	      char *address, short port)
{
        struct linger linger;
        struct addrinfo ai, *aitop;
        int fd, on = 1;
        char strport[NI_MAXSERV];

        memset(&ai, 0, sizeof (ai));
        ai.ai_family = AF_INET;
        ai.ai_socktype = SOCK_STREAM;
        ai.ai_flags = f != connect ? AI_PASSIVE : 0;
        snprintf(strport, sizeof (strport), "%d", port);
        if (getaddrinfo(address, strport, &ai, &aitop) != 0) {
                warn("getaddrinfo", __FUNCTION__);
                return (-1);
        }
        
        /* Create listen socket */
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1) {
                warn(__FUNCTION__, "socket");
                return (-1);
        }

        if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
                warn(__FUNCTION__,"fcntl(O_NONBLOCK)");
                goto out;
        }

        if (fcntl(fd, F_SETFD, 1) == -1) {
                warn(__FUNCTION__,"fcntl(F_SETFD)");
                goto out;
        }

        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &on, sizeof(on));
        linger.l_onoff = 1;
        linger.l_linger = 5;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));

        if ((f)(fd, aitop->ai_addr, aitop->ai_addrlen) == -1) {
		if (errno != EINPROGRESS) {
			warn(__FUNCTION__,"bind");
			goto out;
		}
        }

	return (fd);

 out:
	close(fd);
	return (-1);
}

struct io_obj *
io_new_listener(char *address, short port)
{
        struct io_obj *obj;
	int fd;

	if ((fd = io_make_socket(bind, address, port)) == -1)
		return (NULL);

	if (listen(fd, 10) == -1) {
		warn(__FUNCTION__,"listen");
		goto out;
	}

	/* Create a source object that will get us a new fd on each accept */
	obj = io_new_obj(fd, IOTYPE_SOURCE, io_method_accept, IO_ACCEPTSIZE);
	if (obj == NULL)
		goto out;

	return (obj);

 out:
	close(fd);
	return (NULL);
}

ssize_t
io_method_connect(struct io_obj *obj, int fd, void *buf, size_t size)
{
	int error;
	socklen_t errsz = sizeof(error);

	/* Check if the connection completed */
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errsz) == -1) {
		warn(": getsockopt for %d", fd);
		return (-1);
	}

	if (error) {
		warnx("getsockopt: %s", strerror(error));
		errno = error;
		return (-1);
	}

	/* We are done with using this special method, the socket
	 * is connected now.  We can fall back to normal io_method
	 */
	obj->io_special = NULL;

	return (0);
}

struct io_duplex *
io_new_connector(char *address, short port)
{
        struct io_duplex *dplx;
	int fd;

	if ((fd = io_make_socket(connect, address, port)) == -1)
		return (NULL);

	/* Create a source object that will get us a new fd on each accept */
	dplx = io_new_socket(fd);
	if (dplx == NULL)
		goto out;

	dplx->io_dst->io_special = io_method_connect;

	/* Writers need to be scheduled by hand */
	event_add(&dplx->io_dst->io_ev, NULL);

	return (dplx);

 out:
	close(fd);
	return (NULL);
}
