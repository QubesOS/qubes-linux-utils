#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/types.h>
#include "libqubes-rpc-filecopy.h"

static unsigned long crc32_sum;
static int ignore_quota_error = 0;
error_handler_t *error_handler = NULL;

void register_error_handler(error_handler_t *value) {
    error_handler = value;
}

_Noreturn static void call_error_handler(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (error_handler) {
        error_handler(fmt, args);
    } else {
        vfprintf(stderr, fmt, args);
    }
    va_end(args);
    exit(1);
}

static int write_all_with_crc(int fd, const void *buf, int size)
{
    crc32_sum = Crc32_ComputeBuf(crc32_sum, buf, size);
    return write_all(fd, buf, size);
}

void notify_end_and_wait_for_result(void)
{
    struct file_header end_hdr;

    /* notify end of transfer */
    memset(&end_hdr, 0, sizeof(end_hdr));
    end_hdr.namelen = 0;
    end_hdr.filelen = 0;
    write_all_with_crc(1, &end_hdr, sizeof(end_hdr));

    set_block(0);
    wait_for_result();
}

static void sanitize_remote_filename(char *untrusted_filename)
{
    for (; *untrusted_filename; ++untrusted_filename) {
        if (*untrusted_filename < ' ' ||
            *untrusted_filename > '~' ||
            *untrusted_filename == '"')
            *untrusted_filename = '_';
    }
}

void wait_for_result(void)
{
    struct result_header hdr;
    struct result_header_ext hdr_ext;
    char last_filename[MAX_PATH_LENGTH + 1];
    char last_filename_prefix[] = "; Last file: ";

    if (!read_all(0, &hdr, sizeof(hdr))) {
        if (errno == EAGAIN) {
            // no result sent and stdin still open
            return;
        } else {
            // other read error or EOF
            exit(1);  // hopefully remote has produced error message
        }
    }
    if (!read_all(0, &hdr_ext, sizeof(hdr_ext))) {
        // remote used old result_header struct
        hdr_ext.last_namelen = 0;
    }
    if (hdr_ext.last_namelen > MAX_PATH_LENGTH) {
        // read only at most MAX_PATH_LENGTH chars
        hdr_ext.last_namelen = MAX_PATH_LENGTH;
    }
    if (!read_all(0, last_filename, hdr_ext.last_namelen)) {
        fprintf(stderr, "Failed to get last filename\n");
        hdr_ext.last_namelen = 0;
    }
    last_filename[hdr_ext.last_namelen] = '\0';
    if (!hdr_ext.last_namelen)
        /* set prefix to empty string */
        last_filename_prefix[0] = '\0';

    /* sanitize the remote filename */
    sanitize_remote_filename(last_filename);

    errno = hdr.error_code;
    if (hdr.error_code != 0) {
        switch (hdr.error_code) {
            case EEXIST:
                call_error_handler("A file named \"%s\" already exists in QubesIncoming dir", last_filename);
                break;
            case EINVAL:
                call_error_handler("File copy: Corrupted data from packer%s\"%s\"", last_filename_prefix, last_filename);
                break;
            case EDQUOT:
                if (ignore_quota_error) {
                    /* skip also CRC check as sender and receiver might be
                     * desynchronized in this case */
                    return;
                }
		/* fallthrough */
            default:
                call_error_handler("File copy: \"%s%s%s\"",
                        strerror(hdr.error_code), last_filename_prefix, last_filename);
        }
    }
    if (hdr.crc32 != crc32_sum) {
        call_error_handler("File transfer failed: checksum mismatch");
    }
}

void write_headers(const struct file_header *hdr, const char *filename)
{
    if (!write_all_with_crc(1, hdr, sizeof(*hdr))
            || !write_all_with_crc(1, filename, hdr->namelen)) {
        set_block(0);
        wait_for_result();
        exit(1);
    }
}

int copy_file_with_crc(int outfd, int infd, long long size) {
    return copy_file(outfd, infd, size, &crc32_sum);
}

int single_file_processor(const char *filename, const struct stat *st)
{
    struct file_header hdr;
    int fd;
    mode_t mode = st->st_mode;

    hdr.namelen = strlen(filename) + 1;
    hdr.mode = mode;
    hdr.atime = st->st_atim.tv_sec;
    hdr.atime_nsec = st->st_atim.tv_nsec;
    hdr.mtime = st->st_mtim.tv_sec;
    hdr.mtime_nsec = st->st_mtim.tv_nsec;

    if (S_ISREG(mode)) {
        int ret;
        fd = open(filename, O_RDONLY);
        if (fd < 0)
            call_error_handler("open %s", filename);
        hdr.filelen = st->st_size;
        write_headers(&hdr, filename);
        ret = copy_file(1, fd, hdr.filelen, &crc32_sum);
        if (ret != COPY_FILE_OK) {
            if (ret != COPY_FILE_WRITE_ERROR)
                call_error_handler("Copying file %s: %s", filename,
                        copy_file_status_to_str(ret));
            else {
                set_block(0);
                wait_for_result();
                exit(1);
            }
        }
        close(fd);
    }
    if (S_ISDIR(mode)) {
        hdr.filelen = 0;
        write_headers(&hdr, filename);
    }
    if (S_ISLNK(mode)) {
        char name[st->st_size + 1];
        if (readlink(filename, name, sizeof(name)) != st->st_size)
            call_error_handler("readlink %s", filename);
        hdr.filelen = st->st_size;
        write_headers(&hdr, filename);
        if (!write_all_with_crc(1, name, st->st_size)) {
            set_block(0);
            wait_for_result();
            exit(1);
        }
    }
    // check for possible error from qfile-unpacker
    wait_for_result();
    return 0;
}

int do_fs_walk(const char *file, int ignore_symlinks)
{
    char *newfile;
    struct stat st;
    struct dirent *ent;
    DIR *dir;

    if (lstat(file, &st))
        call_error_handler("stat %s", file);
    if (S_ISLNK(st.st_mode) && ignore_symlinks)
        return 0;
    single_file_processor(file, &st);
    if (!S_ISDIR(st.st_mode))
        return 0;
    dir = opendir(file);
    if (!dir)
        call_error_handler("opendir %s", file);
    while ((ent = readdir(dir))) {
        char *fname = ent->d_name;
        if (!strcmp(fname, ".") || !strcmp(fname, ".."))
            continue;
        if (asprintf(&newfile, "%s/%s", file, fname) >= 0) {
            do_fs_walk(newfile, ignore_symlinks);
            free(newfile);
        } else {
            fprintf(stderr, "asprintf failed\n");
            exit(1);
        }
    }
    closedir(dir);
    // directory metadata is resent; this makes the code simple,
    // and the atime/mtime is set correctly at the second time
    single_file_processor(file, &st);
    return 0;
}

void qfile_pack_init(void) {
    crc32_sum = 0;
    ignore_quota_error = 0;
    // this will allow checking for possible feedback packet in the middle of transfer
    set_nonblock(0);
    signal(SIGPIPE, SIG_IGN);
    error_handler = NULL;
}

void set_ignore_quota_error(int value) {
    ignore_quota_error = value;
}
