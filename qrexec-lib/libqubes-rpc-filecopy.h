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

#ifndef _LIBQUBES_RPC_FILECOPY_H
#define _LIBQUBES_RPC_FILECOPY_H

#define FILECOPY_VMNAME_SIZE 32
#define PROGRESS_NOTIFY_DELTA (15*1000*1000)
#define MAX_PATH_LENGTH 16384

#define LEGAL_EOF 31415926

#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

struct file_header {
	unsigned int namelen;
	unsigned int mode;
	unsigned long long filelen;
	unsigned int atime;
	unsigned int atime_nsec;
	unsigned int mtime;
	unsigned int mtime_nsec;
};

struct result_header {
	uint32_t error_code;
	uint32_t _pad;
	uint64_t crc32;
} __attribute__((packed));

/* optional info about last processed file */
struct result_header_ext {
	uint32_t last_namelen;
	char last_name[0];
} __attribute__((packed));

enum {
	COPY_FILE_OK,
	COPY_FILE_READ_EOF,
	COPY_FILE_READ_ERROR,
	COPY_FILE_WRITE_ERROR
};

/* feedback handling */
typedef void (notify_progress_t)(int, int);
typedef void (error_handler_t)(const char *fmt, va_list args);
void register_notify_progress(notify_progress_t *func);
void register_error_handler(error_handler_t *func);

/* common functions */
int copy_file(int outfd, int infd, long long size, unsigned long *crc32);
const char *copy_file_status_to_str(int status);
void set_size_limit(unsigned long long new_bytes_limit, unsigned long long new_files_limit);
void set_verbose(int value);
/* register open fd to /proc/PID/fd of this process */
void set_procfs_fd(int value);
int write_all(int fd, const void *buf, int size);
int read_all(int fd, void *buf, int size);
int copy_fd_all(int fdout, int fdin);
void set_nonblock(int fd);
void set_block(int fd);

/* unpacking */
extern unsigned long Crc32_ComputeBuf( unsigned long inCrc32, const void *buf,
        size_t bufLen );
extern int do_unpack(void);

/* packing */
int single_file_processor(const char *filename, const struct stat *st);
int do_fs_walk(const char *file, int ignore_symlinks);
/* used in tar2qfile to alter only headers, but keep original file stream */
void write_headers(const struct file_header *hdr, const char *filename);
int copy_file_with_crc(int outfd, int infd, long long size);
/* MUST be called before first do_fs_walk/single_file_processor */
void qfile_pack_init(void);
void set_ignore_quota_error(int value);
/* those two will call registered error handler if needed */
void wait_for_result(void);
void notify_end_and_wait_for_result(void);

#endif /* _LIBQUBES_RPC_FILECOPY_H */
