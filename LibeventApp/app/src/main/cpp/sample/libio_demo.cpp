//
// Created by Administrator on 2020/11/19 0019.
//

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
#include <sys/time.h>
#include <sys/queue.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <event.h>
#include <io.h>
#include <ctype.h>

#include "../libio/io.h"

int
filter_uppercase(void *arg, struct io_filter *filter, struct io_buffer *buf,
                 int flags)
{
    //arg 透传参数
    size_t i, curoff = filter->io_buf.off;
    struct io_buffer *mybuf = &filter->io_buf;

    if (io_buffer_append(mybuf, buf) == -1)
        return (-1);

    for (i = curoff; i < mybuf->off; i++)
        mybuf->data[i] = toupper(mybuf->data[i]);

    mybuf->ready = mybuf->off;

    return (IOFILT_DONE);
}

int
filter_reverse(void *arg, struct io_filter *filter, struct io_buffer *buf,
               int flags)
{
    struct io_buffer *mybuf = &filter->io_buf;
    size_t i, curoff = mybuf->off, size;

    if (io_buffer_append(mybuf, buf) == -1)
        return (-1);

    size = mybuf->off - curoff;
    if (mybuf->off && mybuf->data[mybuf->off - 1] == '\n')
        size--;

    for (i = 0; i < size/2; i++) {
        char c = mybuf->data[curoff + i];

        mybuf->data[curoff + i] = mybuf->data[curoff + size - 1 - i];
        mybuf->data[curoff + size - 1 - i] = c;
    }

    mybuf->ready = mybuf->off;

    return (IOFILT_DONE);
}

int
main_libio(int argc, char **argv)
{
    struct io_obj *src, *dst;
    struct io_filter *filt, *filt2;

    //event_init();
    auto pBase = event_base_new();

    src = io_new_source_base(pBase, fileno(stdin));
    dst = io_new_sink_base(pBase, fileno(stdout));
    filt = io_new_filter(filter_uppercase, NULL);
    filt2 = io_new_filter(filter_reverse, NULL);

    io_attach(src, filt);
    io_attach(filt, dst);
    io_attach(filt, filt2);
    io_attach(filt2, dst);

    //event_dispatch();
    event_base_dispatch(pBase);

    exit(0);
}
