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
/* result code: the receiver skipped some existing files */
#define FILECOPY_SKIPPED ECANCELED

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

struct file_header {
    uint32_t namelen;
    uint32_t mode;
    uint64_t filelen;
    uint32_t atime;
    uint32_t atime_nsec;
    uint32_t mtime;
    uint32_t mtime_nsec;
};

struct result_header {
    uint32_t error_code;
    uint32_t _pad;
    uint64_t crc32;
} __attribute__((packed));

/* optional info about last processed file */
struct result_header_ext {
    uint32_t last_namelen;
    char last_name[];
} __attribute__((packed));

enum {
    COPY_FILE_OK,
    COPY_FILE_READ_EOF,
    COPY_FILE_READ_ERROR,
    COPY_FILE_WRITE_ERROR
};

enum copy_flags {
    COPY_DEFAULT = 0,
    COPY_ALLOW_SYMLINKS = (1 << 0),
    COPY_ALLOW_DIRECTORIES = (1 << 1),
    COPY_ALLOW_UNSAFE_CHARACTERS = (1 << 2),
    COPY_ALLOW_NON_CANONICAL_SYMLINKS = (1 << 3),
    COPY_ALLOW_UNSAFE_SYMLINKS = (1 << 4),
};

/* what to do when a file to unpack already exists */
enum conflict_action {
    CONFLICT_ABORT,
    CONFLICT_OVERWRITE,
    CONFLICT_SKIP,
    CONFLICT_RENAME,
};

/*
 * Called when a received regular file or symlink cannot be created because
 * something with that name already exists. Without O_TMPFILE support, it is
 * called before the file data is received; otherwise after.
 *
 * untrusted_name: path of the file, as sent (already validated)
 * can_overwrite: 0 if the existing entry is a directory; CONFLICT_OVERWRITE
 *   then aborts unpacking
 * new_name, new_name_size: on input, a free name (single path component) in
 *   the same directory to suggest, or "" if none was found; for
 *   CONFLICT_RENAME, the handler writes the chosen name there
 * error: NULL on the first call for a file; if the name given with
 *   CONFLICT_RENAME is invalid or also taken, the handler is called again for
 *   the same file, with an error message to show
 *
 * CONFLICT_OVERWRITE removes the existing entry and creates the new one,
 * CONFLICT_SKIP drops the file (unpacking then ends with FILECOPY_SKIPPED),
 * CONFLICT_ABORT stops unpacking with EEXIST.
 */
typedef enum conflict_action (conflict_handler_t)(const char *untrusted_name,
        int can_overwrite, char *new_name, size_t new_name_size,
        const char *error);

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
/*
 * Delay extracting a file if there is no enough space for it - wait for space
 * for this file, plus a given margin.
 */
void set_wait_for_space(unsigned long margin);
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
extern int do_unpack_ext(int flags);
/*
 * Set the function asking what to do with existing files (see
 * conflict_handler_t). Without one, an existing file stops unpacking with
 * EEXIST.
 */
void register_conflict_handler(conflict_handler_t *func);

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
