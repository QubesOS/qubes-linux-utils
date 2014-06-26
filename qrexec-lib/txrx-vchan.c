/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <libvchan.h>

int wait_for_vchan_or_argfd_once(libvchan_t *ctrl, int max, fd_set * rdset, fd_set * wrset)
{
	int vfd, ret;
	struct timespec tv = { 1, 100000000 };
	sigset_t empty_set;

	sigemptyset(&empty_set);

	vfd = libvchan_fd_for_select(ctrl);
	FD_SET(vfd, rdset);
	if (vfd > max)
		max = vfd;
	max++;
	ret = pselect(max, rdset, wrset, NULL, &tv, &empty_set);
	if (ret < 0) {
		if (errno != EINTR) {
			perror("select");
			exit(1);
		} else {
			FD_ZERO(rdset);
			FD_ZERO(wrset);
			fprintf(stderr, "eintr\n");
			return 1;
		}

	}
	if (!libvchan_is_open(ctrl)) {
		fprintf(stderr, "libvchan_is_eof\n");
		exit(0);
	}
	if (FD_ISSET(vfd, rdset))
		// the following will never block; we need to do this to
		// clear libvchan_fd pending state 
		libvchan_wait(ctrl);
	if (libvchan_data_ready(ctrl))
		return 1;
	return ret;
}

void wait_for_vchan_or_argfd(libvchan_t *ctrl, int max, fd_set * rdset, fd_set * wrset)
{
	fd_set r = *rdset, w = *wrset;
	do {
		*rdset = r;
		*wrset = w;
	}
	while (wait_for_vchan_or_argfd_once(ctrl, max, rdset, wrset) == 0);
}

int write_vchan_all(libvchan_t *vchan, const void *data, size_t size) {
    size_t pos;
    int ret;

    pos = 0;
    while (pos < size) {
        ret = libvchan_write(vchan, data+pos, size-pos);
        if (ret < 0)
            return 0;
        pos += ret;
    }
    return 1;
}

int read_vchan_all(libvchan_t *vchan, void *data, size_t size) {
    size_t pos;
    int ret;

    pos = 0;
    while (pos < size) {
        ret = libvchan_read(vchan, data+pos, size-pos);
        if (ret < 0)
            return 0;
        pos += ret;
    }
    return 1;
}
