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
#ifndef _IO_H_
#define _IO_H_

#include "io_config.h"

#define IOTYPE_SOURCE	0x0001
#define IOTYPE_SINK	0x0002
#define IOTYPE_FILTER	0x0003

#define IORES_ERROR	-1
#define IORES_END	0

#define IOFILT_DONE	0
#define IOFILT_NEEDMORE	1
#define IOFILT_END	2	/* causes destruction of this filter */
#define IOFILT_ERROR	-1

#define IOFLAG_FLUSH	0x0001

#define IOBUFFLAG_FAST	0x0001

#define IO_BLOCKSIZE	4096
#define IO_MAXSIZE	65536
#define IO_HIWATER	50000

#define IO_ACCEPTSIZE	(sizeof (int) + sizeof(size_t) + sizeof (struct sockaddr_storage))

TAILQ_HEAD(io_queue, io_list);

struct io_obj;

struct io_buffer {
	u_char *data;
	size_t size;
	size_t off;
	size_t ready;

	int flags;
};

struct io_list {
	TAILQ_ENTRY(io_list) il_next;

	struct io_header *il_hdr;
};

struct io_header {
	struct io_queue ihdr_queue;
	struct io_queue ihdr_parent;
	int ihdr_type;
	struct io_buffer ihdr_buf;
	int ihdr_flags;

	void (*ihdr_free)(struct io_obj *, void *);
	void *ihdr_freearg;
};

#define io_type		io_hdr.ihdr_type
#define io_buf		io_hdr.ihdr_buf
#define io_flags	io_hdr.ihdr_flags
#define io_free		io_hdr.ihdr_free
#define io_freearg	io_hdr.ihdr_freearg

#define IOSINK_FIN	0x0001	/* die after completing write */
#define IOOBJ_INTERM	0x0002	/* this object is terminating already */
#define IOOBJ_HOLD	0x0004	/* this object has been referenced, no free */
#define IOOBJ_DEAD	0x0008	/* free this object, when loosing reference */

#define IO_OBJ_HOLD(x)	\
	do { \
		if ((x)->io_flags & IOOBJ_HOLD) \
			errx(1, __FUNCTION__": %p already held", x); \
		(x)->io_flags |= IOOBJ_HOLD; \
	} while (0)

#define IO_OBJ_RELEASE(x)	\
	do { \
		if (!((x)->io_flags & IOOBJ_HOLD)) \
			errx(1, __FUNCTION__": %p not held", x); \
		(x)->io_flags &= ~IOOBJ_HOLD; \
	} while (0)


struct io_obj {
	struct io_header io_hdr;

	struct io_duplex *io_dplx;
	ssize_t (*io_special)(struct io_obj *, int, void *, size_t);
	ssize_t (*io_method)(int, void *, size_t);
	size_t io_blocksize;

	int io_timeout;
	struct event io_ev;
};

struct io_duplex {
	struct io_obj *io_src;
	struct io_obj *io_dst;
};

struct io_filter {
	struct io_header io_hdr;
	/* keep above same as io_obj */

	int (*io_filter)(void *arg, struct io_filter *filt, 
			 struct io_buffer *in, int flags);
	void *io_state;

	struct io_buffer io_tmpbuf;
};

/* Prototypes */
void io_set_timeout(struct io_obj *, int);

void io_terminate(struct io_obj *, int);
void io_obj_close(struct io_obj *);
struct io_obj *io_new_obj(int, int, ssize_t (*)(int, void *, size_t), size_t);

#define io_new_source(x)	io_new_obj(x, IOTYPE_SOURCE, read, IO_BLOCKSIZE)
#define io_new_sink(x)		io_new_obj(x, IOTYPE_SINK, \
					(ssize_t (*)(int, void *, size_t))write, \
					IO_BLOCKSIZE)

struct io_obj *io_new_listener(char *, short);
struct io_duplex *io_new_connector(char *, short);

struct io_duplex *io_new_duplex(int, ssize_t (*)(int, void *, size_t),
				ssize_t (*)(int, const void *, size_t),
				size_t);

#define io_new_socket(x)	io_new_duplex(x, read, write, IO_BLOCKSIZE)

/* Filter functions */
struct io_filter *io_new_filter(int (*)(void *, struct io_filter *,
				struct io_buffer *, int), void *arg);

ssize_t io_data_write(struct io_obj *, void *, size_t);

void io_buffer_free(struct io_buffer *);
ssize_t io_buffer_write(struct io_obj *, struct io_buffer *);
int io_buffer_append(struct io_buffer *dst, struct io_buffer *src);
int io_buffer_data(struct io_buffer *, void *, size_t);
void io_buffer_drain(struct io_buffer *, size_t);
void io_buffer_empty(struct io_buffer *);
int io_buffer_extend(struct io_buffer *, size_t);

int io_process_filter(struct io_obj *, struct io_buffer *, int);

void io_detach_hdr(struct io_header *, struct io_header *);
#define io_detach(x,y)	io_detach_hdr(&(x)->io_hdr, &(y)->io_hdr)

int io_attach_hdr(struct io_header *, struct io_header *);
#define io_attach(x,y)	io_attach_hdr(&(x)->io_hdr, &(y)->io_hdr)

/* Proto-types for specalized methods */
ssize_t io_method_accept(int, void *, size_t);

#endif /* _IO_H_ */
