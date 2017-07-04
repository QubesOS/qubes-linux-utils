#define _GNU_SOURCE /* For O_NOFOLLOW. */
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include "libqubes-rpc-filecopy.h"
#include "ioall.h"
#include "crc32.h"

char untrusted_namebuf[MAX_PATH_LENGTH];
unsigned long long bytes_limit = 0;
unsigned long long files_limit = 0;
unsigned long long total_bytes = 0;
unsigned long long total_files = 0;
int verbose = 0;
int use_tmpfile = 0;
int procdir_fd = -1;

void send_status_and_crc(int code, const char *last_filename);

/* copy from asm-generic/fcntl.h */
#ifndef __O_TMPFILE
#define __O_TMPFILE 020000000
#endif
#ifndef O_TMPFILE
/* a horrid kludge trying to make sure that this will fail on old kernels */
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#define O_TMPFILE_MASK (__O_TMPFILE | O_DIRECTORY | O_CREAT)
#endif

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 14)
#define HAVE_SYNCFS
#endif

void do_exit(int code, const char *last_filename)
{
    close(0);
    send_status_and_crc(code, last_filename);
    exit(code);
}

void set_size_limit(unsigned long long new_bytes_limit, unsigned long long new_files_limit)
{
    bytes_limit = new_bytes_limit;
    files_limit = new_files_limit;
}

void set_verbose(int value)
{
    verbose = value;
}

void set_procfs_fd(int value)
{
    procdir_fd = value;
    use_tmpfile = 1;
}

unsigned long crc32_sum = 0;
int read_all_with_crc(int fd, void *buf, int size) {
    int ret;
    ret = read_all(fd, buf, size);
    if (ret)
        crc32_sum = Crc32_ComputeBuf(crc32_sum, buf, size);
    return ret;
}

void send_status_and_crc(int code, const char *last_filename) {
    struct result_header hdr;
    struct result_header_ext hdr_ext;
    int saved_errno;

    saved_errno = errno;
    hdr.error_code = code;
    hdr.crc32 = crc32_sum;
    if (!write_all(1, &hdr, sizeof(hdr)))
        perror("write status");
    if (last_filename) {
        hdr_ext.last_namelen = strlen(last_filename);
        if (!write_all(1, &hdr_ext, sizeof(hdr_ext)))
            perror("write status ext");
        if (!write_all(1, last_filename, hdr_ext.last_namelen))
            perror("write last_filename");
    }
    errno = saved_errno;
}

void fix_times_and_perms(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    struct timeval times[2] =
    { 
        {untrusted_hdr->atime, untrusted_hdr->atime_nsec / 1000},
        {untrusted_hdr->mtime, untrusted_hdr->mtime_nsec / 1000}
    };
    if (chmod(untrusted_name, untrusted_hdr->mode & 07777))  /* safe because of chroot */
        do_exit(errno, untrusted_name);
    if (utimes(untrusted_name, times))  /* as above */
        do_exit(errno, untrusted_name);
}



void process_one_file_reg(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    int ret;
    int fdout = -1;

    /* make the file inaccessible until fully written */
    if (use_tmpfile) {
        fdout = open(".", O_WRONLY | O_TMPFILE, 0700);
        if (fdout < 0) {
            if (errno==ENOENT || /* most likely, kernel too old for O_TMPFILE */
                    errno==EOPNOTSUPP) /* filesystem has no support for O_TMPFILE */
                use_tmpfile = 0;
            else
                do_exit(errno, untrusted_name);
        }
    }
    if (fdout < 0)
        fdout = open(untrusted_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0000); /* safe because of chroot */
    if (fdout < 0)
        do_exit(errno, untrusted_name);
    /* sizes are signed elsewhere */
    if (untrusted_hdr->filelen > LLONG_MAX || (bytes_limit && untrusted_hdr->filelen > bytes_limit))
        do_exit(EDQUOT, untrusted_name);
    if (bytes_limit && total_bytes > bytes_limit - untrusted_hdr->filelen)
        do_exit(EDQUOT, untrusted_name);
    total_bytes += untrusted_hdr->filelen;
    ret = copy_file(fdout, 0, untrusted_hdr->filelen, &crc32_sum);
    if (ret != COPY_FILE_OK) {
        if (ret == COPY_FILE_READ_EOF
                || ret == COPY_FILE_READ_ERROR)
            do_exit(LEGAL_EOF, untrusted_name); // hopefully remote will produce error message
        else
            do_exit(errno, untrusted_name);
    }
    if (use_tmpfile) {
        char fd_str[7];
        snprintf(fd_str, sizeof(fd_str), "%d", fdout);
        if (linkat(procdir_fd, fd_str, AT_FDCWD, untrusted_name, AT_SYMLINK_FOLLOW) < 0)
            do_exit(errno, untrusted_name);
    }
    close(fdout);
    fix_times_and_perms(untrusted_hdr, untrusted_name);
}


void process_one_file_dir(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    // fix perms only when the directory is sent for the second time
    // it allows to transfer r.x directory contents, as we create it rwx initially
    struct stat buf;
    if (!mkdir(untrusted_name, 0700)) /* safe because of chroot */
        return;
    if (errno != EEXIST)
        do_exit(errno, untrusted_name);
    if (stat(untrusted_name,&buf) < 0)
        do_exit(errno, untrusted_name);
    total_bytes += buf.st_size;
    /* size accumulated after the fact, so don't check limit here */
    fix_times_and_perms(untrusted_hdr, untrusted_name);
}

void process_one_file_link(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    char untrusted_content[MAX_PATH_LENGTH];
    unsigned int filelen;
    if (untrusted_hdr->filelen > MAX_PATH_LENGTH - 1)
        do_exit(ENAMETOOLONG, untrusted_name);
    filelen = untrusted_hdr->filelen; /* sanitized above */
    total_bytes += filelen;
    if (bytes_limit && total_bytes > bytes_limit)
        do_exit(EDQUOT, untrusted_name);
    if (!read_all_with_crc(0, untrusted_content, filelen))
        do_exit(LEGAL_EOF, untrusted_name); // hopefully remote has produced error message
    untrusted_content[filelen] = 0;
    if (symlink(untrusted_content, untrusted_name)) /* safe because of chroot */
        do_exit(errno, untrusted_name);

}

void process_one_file(struct file_header *untrusted_hdr)
{
    unsigned int namelen;
    if (untrusted_hdr->namelen > MAX_PATH_LENGTH - 1)
        do_exit(ENAMETOOLONG, NULL); /* filename too long so not received at all */
    namelen = untrusted_hdr->namelen; /* sanitized above */
    if (!read_all_with_crc(0, untrusted_namebuf, namelen))
        do_exit(LEGAL_EOF, NULL); // hopefully remote has produced error message
    untrusted_namebuf[namelen] = 0;
    if (S_ISREG(untrusted_hdr->mode))
        process_one_file_reg(untrusted_hdr, untrusted_namebuf);
    else if (S_ISLNK(untrusted_hdr->mode))
        process_one_file_link(untrusted_hdr, untrusted_namebuf);
    else if (S_ISDIR(untrusted_hdr->mode))
        process_one_file_dir(untrusted_hdr, untrusted_namebuf);
    else
        do_exit(EINVAL, untrusted_namebuf);
    if (verbose && !S_ISDIR(untrusted_hdr->mode))
        fprintf(stderr, "%s\n", untrusted_namebuf);
}

int do_unpack(void)
{
    struct file_header untrusted_hdr;
#ifdef HAVE_SYNCFS
    int cwd_fd;
    int saved_errno;
#endif

    total_bytes = total_files = 0;
    /* initialize checksum */
    crc32_sum = 0;
    while (read_all_with_crc(0, &untrusted_hdr, sizeof untrusted_hdr)) {
        /* check for end of transfer marker */
        if (untrusted_hdr.namelen == 0) {
            errno = 0;
            break;
        }
        total_files++;
        if (files_limit && total_files > files_limit)
            do_exit(EDQUOT, untrusted_namebuf);
        process_one_file(&untrusted_hdr);
    }

#ifdef HAVE_SYNCFS
    saved_errno = errno;
    cwd_fd = open(".", O_RDONLY);
    if (cwd_fd >= 0 && syncfs(cwd_fd) == 0 && close(cwd_fd) == 0)
        errno = saved_errno;
#else
    sync();
#endif

    send_status_and_crc(errno, untrusted_namebuf);
    return errno;
}
