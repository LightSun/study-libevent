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
#include <sys/queue.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <event.h>

#include "io_config.h"
#include "io.h"
#include "../sample/and_log.h"

#define io_buffer_space(x,s)	((x)->size - (x)->off >= (s))

void io_duplex_halffree(struct io_duplex *dplx, struct io_obj *obj);

void
io_buffer_free(struct io_buffer *buf)
{
	if (buf->data != NULL)
		free(buf->data);

	memset(buf, 0, sizeof (struct io_buffer));
}

void
io_obj_free(struct io_obj *obj)
{
	if (obj->io_flags & IOOBJ_HOLD) {
		obj->io_flags |= IOOBJ_DEAD;
		return;
	}

	if (obj->io_free != NULL)
		(obj->io_free)(obj, obj->io_freearg);

	switch (obj->io_type) {
	case IOTYPE_SOURCE:
	case IOTYPE_SINK:
		/* Only sink or source can be part of a duplex */
		if (obj->io_dplx != NULL)
			io_duplex_halffree(obj->io_dplx, obj);

		close(obj->io_ev.ev_fd);
		event_del(&obj->io_ev);
		break;
	case IOTYPE_FILTER:
	default:
		io_buffer_free(&((struct io_filter *)obj)->io_tmpbuf);
		break;
	}

	io_buffer_free(&obj->io_buf);

	free(obj);
}

void
io_detach_child(struct io_header *hparent, struct io_list *lchild)
{
	struct io_list *lparent;
	struct io_header *hchild = lchild->il_hdr;

	TAILQ_REMOVE(&hparent->ihdr_queue, lchild, il_next);
	free(lchild);

	TAILQ_FOREACH(lparent, &hchild->ihdr_parent, il_next) {
		if (lparent->il_hdr == hparent)
			break;
	}

	if (lparent == NULL)
		errx(1, __FUNCTION__, "data corrupt");

	TAILQ_REMOVE(&hchild->ihdr_parent, lparent, il_next);
	free(lparent);
}


void
io_detach_parent(struct io_list *lparent, struct io_header *hchild)
{
	struct io_header *hparent = lparent->il_hdr;
	struct io_list *lchild;

	TAILQ_FOREACH(lchild, &hparent->ihdr_queue, il_next) {
		if (lchild->il_hdr == hchild)
			break;
	}

	if (lchild == NULL)
		errx(1, __FUNCTION__, "data corrupt");

	TAILQ_REMOVE(&hparent->ihdr_queue, lchild, il_next);
	free(lchild);

	TAILQ_REMOVE(&hchild->ihdr_parent, lparent, il_next);
	free(lparent);
}

void
io_detach_hdr(struct io_header *parent, struct io_header *child)
{
	struct io_list *lparent;
	struct io_list *lchild;

	TAILQ_FOREACH(lchild, &parent->ihdr_queue, il_next) {
		if (lchild->il_hdr == child)
			break;
	}

	if (lchild == NULL)
		errx(1,__FUNCTION__ , ": data corrupt");
	TAILQ_REMOVE(&parent->ihdr_queue, lchild, il_next);
	free(lchild);

	TAILQ_FOREACH(lparent, &child->ihdr_parent, il_next) {
		if (lparent->il_hdr == parent)
			break;
	}

	if (lparent == NULL)
		errx(1, __FUNCTION__, ": data corrupt");
	TAILQ_REMOVE(&child->ihdr_parent, lparent, il_next);
	free(lparent);
}

void
io_terminate_forward(struct io_obj *ioobj, int reason)
{
	struct io_list *tmp;

	/* Free everything that is attached to this object */
	for (tmp = TAILQ_FIRST(&ioobj->io_hdr.ihdr_queue); tmp;
	     tmp = TAILQ_FIRST(&ioobj->io_hdr.ihdr_queue)) {
		struct io_obj *otmp;

		otmp = (struct io_obj *)tmp->il_hdr;

		/* This will free the memory pointed to by tmp */
		io_detach_child(&ioobj->io_hdr, tmp);

		/* If the child does not have a parent anymore, we
		 * need to kill it.  Sniff.
		 */
		if (TAILQ_FIRST(&otmp->io_hdr.ihdr_parent) == NULL) {
			if (otmp->io_type == IOTYPE_SINK &&
			    reason == IORES_END &&
			    otmp->io_buf.off != 0) {
				otmp->io_flags |= IOSINK_FIN;
				continue;
			}

			if (otmp->io_flags & IOOBJ_INTERM)
				continue;
			otmp->io_flags |= IOOBJ_INTERM;
			io_terminate_forward(otmp, reason);
			io_obj_free(otmp);
		}
	}
}

void
io_terminate_backward(struct io_obj *ioobj, int reason)
{
	struct io_list *tmp;

	/* Free everything that is attached to this object */
	for (tmp = TAILQ_FIRST(&ioobj->io_hdr.ihdr_parent); tmp;
	     tmp = TAILQ_FIRST(&ioobj->io_hdr.ihdr_parent)) {
		struct io_obj *otmp;

		otmp = (struct io_obj *)tmp->il_hdr;

		/* This will free the memory pointed to by tmp */
		io_detach_parent(tmp, &ioobj->io_hdr);

		/* If the parent does not have a child anymore, we
		 * need to kill it.  Sniff.
		 */
		if (TAILQ_FIRST(&otmp->io_hdr.ihdr_queue) == NULL) {
			if (otmp->io_flags & IOOBJ_INTERM)
				continue;
			otmp->io_flags |= IOOBJ_INTERM;

			io_terminate_backward(otmp, reason);
			io_obj_free(otmp);
		}
	}
}

void
io_terminate(struct io_obj *obj, int reason)
{
	if (obj->io_flags & IOOBJ_INTERM)
		return;
	obj->io_flags |= IOOBJ_INTERM;
	
	io_terminate_forward(obj, reason);
	io_terminate_backward(obj, reason);

	io_obj_free(obj);
}

/*
 * Add an IO event callback
 */

void
io_add_event(struct io_obj *obj)
{
	struct timeval tv;

	if (obj->io_timeout == 0) {
		event_add(&obj->io_ev, NULL);
		return;
	}

	timerclear(&tv);
	tv.tv_sec = obj->io_timeout;
	event_add(&obj->io_ev, &tv);
}

/*
 * Sets a timeout for an io object
 */

void
io_set_timeout(struct io_obj *obj, int timeout)
{
	short which = 0;

	switch(obj->io_type) {
	case IOTYPE_SOURCE:
		which = EV_READ;
		break;
	case IOTYPE_SINK:
		which = EV_WRITE;
		break;
	default:
		return;
	}

	obj->io_timeout = timeout;
	if (event_pending(&obj->io_ev, which, NULL))
		io_add_event(obj);
}

/* Gracefully remove a source or sink.
 * If it is a sink or a duplex and there is stuff to write
 * let it finish.
 */

void
io_obj_close(struct io_obj *obj)
{
	struct io_obj *sink = NULL;

	if (obj->io_type == IOTYPE_SINK)
		sink = obj;
	else if (obj->io_dplx != NULL)
		sink = obj->io_dplx->io_dst;

	if (sink != NULL && sink->io_buf.off != 0) {
		sink->io_flags |= IOSINK_FIN;
		return;
	}

	io_terminate(obj, IORES_END);
}

int
io_buffer_extend(struct io_buffer *buf, size_t size)
{
	u_char *p;

	if (io_buffer_space(buf, size))
		return (0);

	p = realloc(buf->data, buf->off + size);
	if (p == NULL) {
		warn(__FUNCTION__, ": realloc");
		return (-1);
	}

	buf->data = p;
	buf->size = buf->off + size;

	memset(buf->data + buf->off, 0, size);

	return (0);
}

void
io_buffer_empty(struct io_buffer *buf)
{
	buf->off = buf->ready = 0;
}

void
io_buffer_drain(struct io_buffer *buf, size_t size)
{
	if (buf->off <= size) {
		io_buffer_empty(buf);
		return;
	}

	memmove(buf->data, buf->data + size, buf->off - size);
	buf->off -= size;

	if (buf->ready <= size)
		buf->ready = 0;
	else
		buf->ready -= size;
}

int io_buffer_data(struct io_buffer *buf, void *data, size_t size)
{
	if (io_buffer_extend(buf, size) == -1)
		return (-1);

	/* Copy data */
	memcpy(buf->data + buf->off, data, size);
	buf->off += size;

	return (0);
}

/* Append the data from one buffer to the other, without removing
 * it in the source.  The source might be used by multiple filters.
 */

int
io_buffer_append(struct io_buffer *dst, struct io_buffer *src)
{
	if ((src->flags & IOBUFFLAG_FAST) && dst->off == 0) {
		u_char *tmp = dst->data;
		size_t tmpsz = dst->size;

		dst->data = src->data;
		dst->size = src->size;
		dst->off = src->off;

		src->data = tmp;
		src->size = tmpsz;
		src->off = 0;
		src->ready = 0;

		return (0);
	}

	return (io_buffer_data(dst, src->data, src->ready));

	return (0);
}

ssize_t
io_buffer_read(ssize_t (*f)(int, void *, size_t),
	       int fd, struct io_buffer *buf, size_t blocksize)
{
	ssize_t n;

	/* Check if we have enough space, get it if we don't */
	if (io_buffer_extend(buf, blocksize) == -1)
		return (-1);

	n = (f) (fd, buf->data + buf->off, blocksize);
	if (n > 0) {
		buf->off += n;
		buf->ready = buf->off;
	}

	return (n);
}

ssize_t
io_buffer_write(struct io_obj *obj, struct io_buffer *buf)
{
	struct io_filter *filt;
	int res;

	if (obj->io_type == IOTYPE_SINK) {
		/* Sink requires a write */
		if (io_buffer_append(&obj->io_buf, buf) == -1)
			return (IOFILT_ERROR);

		/* Schedule the write */
		io_add_event(obj);

		return (0);
	}

	filt = (struct io_filter *)obj;
	if (obj->io_type != IOTYPE_FILTER || filt->io_filter == NULL)
		errx(1, ": data corrupt: %p\n", obj);

	res = filt->io_filter(filt->io_state, filt, buf, 0);
	switch (res) {
	case IOFILT_ERROR:
		io_terminate(obj, IORES_ERROR);
		return (res);

	case IOFILT_NEEDMORE:
		return (res);

	default:
		return (io_process_filter(obj, &obj->io_buf, 0));
	}
}

ssize_t
io_data_write(struct io_obj *obj, void *data, size_t size)
{
	struct io_buffer buf;

	buf.data = data;
	buf.ready = buf.off = buf.size = size;
	buf.flags = 0;

	return (io_buffer_write(obj, &buf));
}

int
io_wouldblock(struct io_obj *io_obj)
{
	struct io_obj *obj;
	struct io_list *tmp;

	TAILQ_FOREACH(tmp, &io_obj->io_hdr.ihdr_queue, il_next) {
		obj = (struct io_obj *)tmp->il_hdr;

		if (obj->io_type == IOTYPE_SINK) {
			if (obj->io_buf.off >= IO_MAXSIZE)
				return (1);
			continue;
		}

		if (obj->io_type == IOTYPE_FILTER &&
		    ((struct io_filter *)obj)->io_filter == NULL)
			continue;

		if (io_wouldblock(obj))
			return (1);
	}

	return (0);
}

int
io_process_filter(struct io_obj *io_obj, struct io_buffer *buf, int flags)
{
	struct io_obj *obj;
	struct io_list *tmp, *next;
	struct io_filter *filt;
	int res = 0, filtret;

	next = NULL;
	for (tmp = TAILQ_FIRST(&io_obj->io_hdr.ihdr_queue); tmp;
	     tmp = next) {
		next = TAILQ_NEXT(tmp, il_next);

		/* Check if we can do a fastpath */
		if (next == NULL)
			buf->flags |= IOBUFFLAG_FAST;
		else
			buf->flags &= ~IOBUFFLAG_FAST;
		obj = (struct io_obj *)tmp->il_hdr;
		filt = (struct io_filter *)tmp->il_hdr;

		/* Write the data, either sink or filter */
		if (obj->io_type == IOTYPE_SINK) {
			if (io_buffer_write(obj, buf) == -1) {
				if (res == 0)
					res = -1;
				io_terminate(obj, IORES_ERROR);
			} else
				res = 1;
			continue;
		}

		/* This can be an anchor object */
		if (filt->io_filter == NULL)
			continue;

		/* Now we are talking to a filter */
		filtret = filt->io_filter(filt->io_state, filt, buf, flags);

		if (filtret == IOFILT_ERROR) {
			if (res == 0)
				res = -1;

			io_terminate(obj, IORES_ERROR);
			continue;
		}

		/* Filter was successful, start off subsequent filters */
		if (filtret == IOFILT_DONE) {
			int err;
			IO_OBJ_HOLD(obj);
			err = io_process_filter(obj, &obj->io_buf, flags);
			IO_OBJ_RELEASE(obj);
			if (obj->io_flags & IOOBJ_DEAD) {
				if (res == 0)
					res = -1;
				io_obj_free(obj);
				continue;
			}
			if (err == -1) {
				if (res == 0)
					res = -1;
				io_terminate(obj, IORES_ERROR);
				continue;
			}
		}

		/* We successfully processed at least one filter */
		res = 1;

		if (filtret == IOFILT_END)
			io_terminate(obj, IORES_END);
	}

	/* If at least one filter read the data, it vanishes */
	if (res == 1)
		io_buffer_drain(buf, buf->ready);
		
	return (res);
}

void
io_source_reschedule(struct io_obj *obj)
{
	struct io_list *tmp;

	if (obj->io_type == IOTYPE_FILTER &&
	    ((struct io_filter *)obj)->io_filter == NULL)
		return;

	tmp = TAILQ_FIRST(&obj->io_hdr.ihdr_parent);

	if (tmp == NULL) {
		if (obj->io_type != IOTYPE_SOURCE)
			return;

		/* Reschedule read, if it not pending yet */
		if (!event_pending(&obj->io_ev, EV_READ, NULL) &&
		    !io_wouldblock(obj))
			io_add_event(obj);

		return;
	}

	/* Find the source of this filter */
	TAILQ_FOREACH(tmp, &obj->io_hdr.ihdr_parent, il_next)
		io_source_reschedule((struct io_obj *)tmp->il_hdr);
}

void
io_handle_write(int fd, short event, void *arg)
{
	struct io_obj *obj = arg;
	ssize_t n;

	/* If the event did timeout, terminate it */
	if (event == EV_TIMEOUT) {
		io_terminate(obj, IORES_ERROR);
		return;
	}
	
	/* If there is a special method, try it */
	if (obj->io_special != NULL)
		n = (obj->io_special)(obj, fd,
				      obj->io_buf.data, obj->io_buf.off);
	else {
		n = (obj->io_method)(fd, obj->io_buf.data, obj->io_buf.off);
		if (n == 0) {
			io_terminate(obj, IORES_END);
			return;
		}
	}
		
	if (n == -1) {
		if (errno == EAGAIN || errno == EINTR) {
			io_add_event(obj);
			return;
		}
			
		io_terminate(obj, IORES_ERROR);
		return;
	}

	/* Allow complete call backs, implemented as filters */
        obj->io_buf.ready = n;
	if (io_process_filter(obj, &obj->io_buf, 0) == -1)
		return;

	io_buffer_drain(&obj->io_buf, n);

	/* Reschedule write if we have data to go */
	if (obj->io_buf.off)
		io_add_event(obj);

	/* Do not look at read, if we still have too much data going */
	if (obj->io_buf.off > IO_HIWATER)
		return;

	/* Terminate when we are done with the write */
	if (obj->io_buf.off == 0 &&
	    (obj->io_flags & IOSINK_FIN)) {
		io_terminate(obj, IORES_END);
		return;
	}

	io_source_reschedule(obj);
}

void
io_handle_read(int fd, short event, void *arg)
{
	struct io_obj *obj = arg;
	struct event *ev = &obj->io_ev;
	int flags = 0;
	ssize_t n;

	/* If the event did timeout, terminate it */
	if (event == EV_TIMEOUT) {
		io_terminate(obj, IORES_ERROR);
		return;
	}
	
	/* Read data from buffer */
	n = io_buffer_read(obj->io_method, ev->ev_fd,
			   &obj->io_buf, obj->io_blocksize);
	if (n == -1) {
		if (errno == EAGAIN || errno == EINTR)
			io_add_event(obj);
		else
			io_terminate(obj, IORES_ERROR);
		return;
	}

	if (n == 0)
		flags = IOFLAG_FLUSH;

	if (io_process_filter(obj, &obj->io_buf, flags) == -1)
		return;

	/* Check if we are done with this filter */
	if (n == 0) {
		io_terminate(obj, IORES_END);
		return;
	}

	/* Don't proceed if our IO would block, we need to wait
	 * for write to free up space.
	 */
	if (io_wouldblock(obj))
		return;

	io_add_event(obj);
}

void
io_header_init(struct io_header *hdr, int type)
{
	hdr->ihdr_type = type;

	TAILQ_INIT(&hdr->ihdr_queue);
	TAILQ_INIT(&hdr->ihdr_parent);
}

struct io_obj *io_new_obj(int fd, int type,
						  ssize_t (*method)(int, void *, size_t), size_t blocksize)
{
	struct io_obj *obj;

	if (type != IOTYPE_SOURCE && type != IOTYPE_SINK) {
		warn(": bad type: %d", __FUNCTION__, type);
		return (NULL);
	}

	obj = calloc(1, sizeof(struct io_obj));
	if (obj == NULL) {
		warn(__FUNCTION__, "calloc");
		return (NULL);
	}

	io_header_init(&obj->io_hdr, type);

	/* Method with which we get the data */
	obj->io_method = method;
	obj->io_blocksize = blocksize;

	/* If we are a source enable reading, the reader will
	 * enable the write call back.
	 */
	if (type == IOTYPE_SOURCE) {
		event_set(&obj->io_ev, fd, EV_READ, io_handle_read, obj);
		io_add_event(obj);
	} else
		event_set(&obj->io_ev, fd, EV_WRITE, io_handle_write, obj);

	return (obj);
}

void
io_duplex_halffree(struct io_duplex *dplx, struct io_obj *obj)
{
	dplx->io_dst->io_dplx = NULL;
	dplx->io_src->io_dplx = NULL;

	if (dplx->io_src == obj)
		io_terminate(dplx->io_dst, IORES_END);
	else
		io_terminate(dplx->io_src, IORES_END);

	dplx->io_dst = NULL;
	dplx->io_src = NULL;
	
	free(dplx);
}

/*
 * Creates an object that contains both a src and a destination.
 */

struct io_duplex *
io_new_duplex(int fd, ssize_t (*mthd_read)(int, void *, size_t),
	   ssize_t (*mthd_write)(int, const void *, size_t), size_t blocksize)
{
	struct io_duplex *dplx;

	dplx = calloc(1, sizeof (struct io_duplex));
	if (dplx == NULL)
		return (NULL);

	dplx->io_src = io_new_obj(fd, IOTYPE_SOURCE, mthd_read, blocksize);
	if (dplx->io_src == NULL)
		goto out;

	dplx->io_dst = io_new_obj(fd, IOTYPE_SINK, mthd_write, blocksize);
	if (dplx->io_dst == NULL)
		goto out;

	dplx->io_dst->io_dplx = dplx;
	dplx->io_src->io_dplx = dplx;

	return (dplx);

 out:
	if (dplx->io_src)
		free(dplx->io_src);
	if (dplx->io_dst)
		free(dplx->io_dst);
	free(dplx);

	return (NULL);
}

int
io_attach_hdr(struct io_header *first, struct io_header *second)
{
	struct io_list *
			lst_p, *lst_c;

	if ((lst_p = malloc(sizeof(struct io_list))) == NULL)
		return (-1);
	if ((lst_c = malloc(sizeof(struct io_list))) == NULL) {
		free(lst_p);
		return (-1);
	}
	lst_p->il_hdr = second;
	lst_c->il_hdr = first;

	TAILQ_INSERT_TAIL(&first->ihdr_queue, lst_p, il_next);
	TAILQ_INSERT_TAIL(&second->ihdr_parent, lst_c, il_next);

	return (0);
}

struct io_filter *
io_new_filter(int (*filter)(void *, struct io_filter *, struct io_buffer *,
			    int), void *arg)
{
	struct io_filter *obj;

	obj = calloc(1, sizeof(struct io_obj));
	if (obj == NULL) {
		warn(__FUNCTION__, "calloc");
		return (NULL);
	}

	io_header_init(&obj->io_hdr, IOTYPE_FILTER);

	obj->io_filter = filter;
	obj->io_state = arg;

	return (obj);
}
