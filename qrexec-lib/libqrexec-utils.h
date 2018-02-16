/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 * Copyright (C) 2013  Marek Marczykowski  <marmarek@invisiblethingslab.com>
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

#include <sys/select.h>
#include <libvchan.h>

struct buffer {
    char *data;
    int buflen;
};

/* return codes for buffered writes */
#define WRITE_STDIN_OK        0 /* all written */
#define WRITE_STDIN_BUFFERED  1 /* something still in the buffer */
#define WRITE_STDIN_ERROR     2 /* write error, errno set */

typedef void (do_exec_t)(char *);
void register_exec_func(do_exec_t *func);
/*
 * exec() qubes-rpc-multiplexer if *prog* starts with magic "QUBESRPC" keyword,
 * do not return in that case; pass *envp* to execve() as en environment
 * otherwise, return false without any action
 */
void exec_qubes_rpc_if_requested(char *prog, char *const envp[]);

void buffer_init(struct buffer *b);
void buffer_free(struct buffer *b);
void buffer_append(struct buffer *b, const char *data, int len);
void buffer_remove(struct buffer *b, int len);
int buffer_len(struct buffer *b);
void *buffer_data(struct buffer *b);

int flush_client_data(int fd, struct buffer *buffer);
int write_stdin(int fd, const char *data, int len, struct buffer *buffer);
int fork_and_flush_stdin(int fd, struct buffer *buffer);

void do_fork_exec(const char *cmdline, int *pid, int *stdin_fd, int *stdout_fd,
        int *stderr_fd);
void wait_for_vchan_or_argfd(libvchan_t *vchan, int max, fd_set * rdset, fd_set * wrset);
int read_vchan_all(libvchan_t *vchan, void *data, size_t size);
int write_vchan_all(libvchan_t *vchan, const void *data, size_t size);
int read_all(int fd, void *buf, int size);
int write_all(int fd, const void *buf, int size);
void fix_fds(int fdin, int fdout, int fderr);
void set_nonblock(int fd);
void set_block(int fd);

int get_server_socket(const char *);
int do_accept(int s);

void set_nonblock(int fd);
