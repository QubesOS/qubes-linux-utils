#define _GNU_SOURCE /* For O_NOFOLLOW. */
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include "libqubes-rpc-filecopy.h"
#include "ioall.h"
#include "crc32.h"

static char untrusted_namebuf[MAX_PATH_LENGTH];
static unsigned long long bytes_limit = 0;
static unsigned long long files_limit = 0;
static unsigned long long total_bytes = 0;
static unsigned long long total_files = 0;
static int verbose = 0;
/*
 * If positive, wait for disk space before extracting a file,
 * keeping this much extra space (in bytes).
 */
static unsigned long opt_wait_for_space_margin;
static int use_tmpfile = 0;
static int procdir_fd = -1;

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

_Noreturn void do_exit(int code, const char *last_filename)
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

void set_wait_for_space(unsigned long value)
{
    opt_wait_for_space_margin = value;
}

void set_procfs_fd(int value)
{
    procdir_fd = value;
    use_tmpfile = 1;
}

int wait_for_space(int fd, unsigned long how_much) {
    int counter = 0;
    struct statvfs fs_space;
    do {
        if (fstatvfs(fd, &fs_space) == -1) {
            perror("fstatvfs");
            return -1;
        }
        // TODO: timeout?
        if (counter > 0)
            usleep(1000000);
        counter++;
    } while (fs_space.f_bsize * fs_space.f_bavail < how_much);
    return 0;
}

static unsigned long crc32_sum = 0;
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
    hdr._pad = 0;
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

static long validate_utime_nsec(uint32_t untrusted_nsec)
{
    enum { MAX_NSEC = 999999999L };
    if (untrusted_nsec > MAX_NSEC)
        errx(1, "Invalid nanoseconds value %" PRIu32, untrusted_nsec);
    return (long)untrusted_nsec;
}

static void fix_times_and_perms(const int fd,
        const struct file_header *const untrusted_hdr,
        const char *const last_segment,
        const char *const untrusted_name)
{
    const struct timespec times[2] =
    { 
        {
            .tv_sec = untrusted_hdr->atime,
            .tv_nsec = validate_utime_nsec(untrusted_hdr->atime_nsec)
        },
        {
            .tv_sec = untrusted_hdr->mtime,
            .tv_nsec = validate_utime_nsec(untrusted_hdr->mtime_nsec)
        },
    };
    if (last_segment == NULL) {
        /* Do not change the mode of symbolic links */
        if (!S_ISLNK(untrusted_hdr->mode) &&
            fchmod(fd, untrusted_hdr->mode & 07777))
            do_exit(errno, untrusted_name);
        if (futimens(fd, times))  /* as above */
            do_exit(errno, untrusted_name);
    } else {
        /* Do not change the mode of what a symbolic link points to */
        if (!S_ISLNK(untrusted_hdr->mode) &&
            fchmodat(fd, last_segment, untrusted_hdr->mode & 07777, 0))
            do_exit(errno, untrusted_name);
        if (utimensat(fd, last_segment, times, AT_SYMLINK_NOFOLLOW))  /* as above */
            do_exit(errno, untrusted_name);
    }
}

/* validate single UTF-8 character
 * return bytes count of this character, or 0 if the character is invalid */
static int validate_utf8_char(const unsigned char *untrusted_c) {
    int tails_count = 0;
    int total_size = 0;
    uint32_t code_point;
    /* it is safe to access byte pointed by the parameter,
     * but every next byte can access only if previous byte was not NUL.
     */

    /* According to http://www.ietf.org/rfc/rfc3629.txt:
     *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
     *   UTF8-1      = %x00-7F
     *   UTF8-2      = %xC2-DF UTF8-tail
     *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
     *                 %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
     *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
     *                 %xF4 %x80-8F 2( UTF8-tail )
     *   UTF8-tail   = %x80-BF
     *
     * This code uses a slightly different grammar:
     *
     *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
     *   UTF8-1      = %x20-7F
     *   UTF8-2      = %xC2-DF UTF8-tail
     *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EF 2( UTF8-tail )
     *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F4 3( UTF8-tail ) /
     *   UTF8-tail   = %x80-BF
     *
     * The differences are:
     *
     * - ASCII control characters are rejected, allowing a fast path for other
     *   ASCII characters.
     * - Surrogates and some values above 0x10FFFF are accepted here, but are
     *   rejected as forbidden code points later.
     */

    if (*untrusted_c >= 0x20 && *untrusted_c < 0x7F) {
        return 1; // non-control ASCII
    } else if (*untrusted_c >= 0xC2 && *untrusted_c <= 0xDF) {
        total_size = 2;
        tails_count = 1;
        code_point = *untrusted_c & 0x1F;
    } else switch (*untrusted_c) {
        case 0xE0:
            untrusted_c++;
            total_size = 3;
            if (*untrusted_c >= 0xA0 && *untrusted_c <= 0xBF)
                tails_count = 1;
            else
                return 0;
            code_point = *untrusted_c & 0x3F;
            break;
        case 0xE1 ... 0xEF:
            total_size = 3;
            tails_count = 2;
            code_point = *untrusted_c & 0xF;
            break;
        case 0xF0:
            untrusted_c++;
            total_size = 4;
            if (*untrusted_c >= 0x90 && *untrusted_c <= 0xBF)
                tails_count = 2;
            else
                return 0;
            code_point = *untrusted_c & 0x3F;
            break;
        case 0xF1 ... 0xF4:
            total_size = 4;
            tails_count = 3;
            code_point = *untrusted_c & 0x7;
            break;
        default:
            return 0; // control ASCII or invalid UTF-8
    }

    while (tails_count-- > 0) {
        untrusted_c++;
        if (!(*untrusted_c >= 0x80 && *untrusted_c <= 0xBF))
            return 0;
        code_point = code_point << 6 | (*untrusted_c & 0x3F);
    }

    switch (code_point) {
    case 0x0 ... 0x1F: // category Cc
    case 0x7F ... 0x9F: // category Cc
    case 0xA0: // category Zs
    case 0xAD: // category Cf
    case 0x600 ... 0x605: // category Cf
    case 0x61C: // category Cf
    case 0x6DD: // category Cf
    case 0x70F: // category Cf
    case 0x8E2: // category Cf
    case 0x1680: // category Zs
    case 0x180E: // category Cf
    case 0x2000 ... 0x200A: // category Zs
    case 0x200B ... 0x200F: // category Cf
    case 0x2028: // category Zl
    case 0x2029: // category Zp
    case 0x202A ... 0x202E: // category Cf
    case 0x202F: // category Zs
    case 0x205F: // category Zs
    case 0x2060 ... 0x2064: // category Cf
    case 0x2066 ... 0x206F: // category Cf
    case 0x3000: // category Zs
    case 0xD800 ... 0xDFFF: // surrogates
    case 0xE000 ... 0xF8FF: // category Co
    case 0xFEFF: // category Cf
    case 0xFFF9 ... 0xFFFB: // category Cf
    case 0x110BD: // category Cf
    case 0x110CD: // category Cf
    case 0x13430 ... 0x13438: // category Cf
    case 0x1BCA0 ... 0x1BCA3: // category Cf
    case 0x1D173 ... 0x1D17A: // category Cf
    case 0xE0001: // category Cf
    case 0xE0020 ... 0xE007F: // category Cf
    case 0xF0000 ... 0xFFFFD: // category Co
    case 0x100000 ... 0x10FFFD: // category Co
    case 0x110000 ... UINT32_MAX: // too large
        return 0; // Invalid UTF-8 or forbidden codepoint

    default:
        return total_size;
    }
}

static size_t validate_path(const char *const untrusted_name, size_t allowed_leading_dotdot)
{
    if (untrusted_name[0] == 0)
        do_exit(ENOENT, untrusted_name); // same error as Linux
    size_t non_dotdot_components = 0, i = 0;
    do {
        if (i == 0 || untrusted_name[i - 1] == '/') {
            switch (untrusted_name[i]) {
            case '/':
            case '\0':
                do_exit(EILSEQ, untrusted_name); // repeated, initial, or trailing slash
            case '.':
                if (untrusted_name[i + 1] == '\0' || untrusted_name[i + 1] == '/')
                    do_exit(EILSEQ, untrusted_name); // path component is "."
                if ((untrusted_name[i + 1] == '.') &&
                    (untrusted_name[i + 2] == '\0' || untrusted_name[i + 2] == '/')) {
                    if (allowed_leading_dotdot) {
                        allowed_leading_dotdot--;
                        break;
                    }
                    do_exit(EILSEQ, untrusted_name); // too many ".." components
                }
                __attribute__((fallthrough));
            default:
                allowed_leading_dotdot = 0; // do not allow further ".." components
                non_dotdot_components++;
                break;
            }
        }
        int utf8_ret = validate_utf8_char((const unsigned char *)(untrusted_name + i));
        if (utf8_ret > 0) {
            i += utf8_ret;
        } else {
            do_exit(EILSEQ, untrusted_name); // not valid UTF-8
        }
    } while (untrusted_name[i]);
    return non_dotdot_components;
}

// Open the second-to-last component of a path, enforcing O_NOFOLLOW for every
// path component.  *last_segment will be set to the last segment of the path,
// and points into the original path.  The original path is modified in-place,
// so one should probably pass a copy.  The return value is either dirfd (if the
// path has no / in it) or a newly opened file descriptor that must be closed by
// the caller.  dirfd can be AT_FDCWD to indicate the current_directory.
static int opendir_safe(int dirfd, char *path, const char **last_segment)
{
    assert(path && *path); // empty paths rejected earlier
    char *this_segment = path, *next_segment = NULL;
    *last_segment = NULL;
    int cur_fd = dirfd;
    for (;;this_segment = next_segment) {
        assert(this_segment);
        char *next = strchr(this_segment, '/');
        if (next) {
            *next = '\0';
            next_segment = next + 1;
        } else {
            *last_segment = this_segment;
            return cur_fd;
        }
        assert(this_segment[0]);
        assert(strcmp(this_segment, "."));
        assert(strcmp(this_segment, ".."));
        assert(strchr(this_segment, '/') == NULL);

        int new_fd = openat(cur_fd, this_segment, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NOCTTY | O_CLOEXEC);
        if (new_fd == -1)
            do_exit(errno, this_segment);
        if (cur_fd != dirfd)
            close(cur_fd);
        cur_fd = new_fd;
    }
}

void process_one_file_reg(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    int ret;
    int fdout = -1, safe_dirfd;
    const char *last_segment;
    char *path_dup;


    validate_path(untrusted_name, 0);
    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    /* make the file inaccessible until fully written */
    if (use_tmpfile) {
        fdout = openat(safe_dirfd, ".", O_WRONLY | O_TMPFILE | O_CLOEXEC | O_NOCTTY, 0700);
        if (fdout < 0) {
            if (errno==ENOENT || /* most likely, kernel too old for O_TMPFILE */
                    errno==EOPNOTSUPP) /* filesystem has no support for O_TMPFILE */
                use_tmpfile = 0;
            else
                do_exit(errno, untrusted_name);
        }
    }

    if (fdout < 0)
        fdout = openat(safe_dirfd, last_segment, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, 0000);
    if (fdout < 0)
        do_exit(errno, untrusted_name);
    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);

    /* sizes are signed elsewhere */
    if (untrusted_hdr->filelen > LLONG_MAX || (bytes_limit && untrusted_hdr->filelen > bytes_limit))
        do_exit(EDQUOT, untrusted_name);
    if (bytes_limit && total_bytes > bytes_limit - untrusted_hdr->filelen)
        do_exit(EDQUOT, untrusted_name);
    if (opt_wait_for_space_margin) {
        wait_for_space(fdout,
            untrusted_hdr->filelen + opt_wait_for_space_margin);
    }
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
        char fd_str[11];
        snprintf(fd_str, sizeof(fd_str), "%d", fdout);
        if (linkat(procdir_fd, fd_str, AT_FDCWD, untrusted_name, AT_SYMLINK_FOLLOW) < 0)
            do_exit(errno, untrusted_name);
    }
    fix_times_and_perms(fdout, untrusted_hdr, NULL, untrusted_name);
    close(fdout);
    free(path_dup);
}


void process_one_file_dir(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    int safe_dirfd;
    const char *last_segment;
    char *path_dup;
    validate_path(untrusted_name, 0);
    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    // fix perms only when the directory is sent for the second time
    // it allows to transfer r.x directory contents, as we create it rwx initially
    struct stat buf;
    if (!mkdirat(safe_dirfd, last_segment, 0700)) {
        close(safe_dirfd);
        return;
    }
    if (errno != EEXIST)
        do_exit(errno, untrusted_name);
    if (stat(untrusted_name,&buf) < 0)
        do_exit(errno, untrusted_name);
    total_bytes += buf.st_size;
    /* size accumulated after the fact, so don't check limit here */
    fix_times_and_perms(safe_dirfd, untrusted_hdr, last_segment, untrusted_name);
    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    free(path_dup);
}

void process_one_file_link(struct file_header *untrusted_hdr,
        const char *untrusted_name)
{
    char untrusted_content[MAX_PATH_LENGTH];
    const char *last_segment;
    char *path_dup;
    unsigned int filelen;
    size_t path_components = validate_path(untrusted_name, 0);
    int safe_dirfd;
    if (untrusted_hdr->filelen > MAX_PATH_LENGTH - 1)
        do_exit(ENAMETOOLONG, untrusted_name);

    filelen = untrusted_hdr->filelen; /* sanitized above */
    total_bytes += filelen;
    if (bytes_limit && total_bytes > bytes_limit)
        do_exit(EDQUOT, untrusted_name);
    if (!read_all_with_crc(0, untrusted_content, filelen))
        do_exit(LEGAL_EOF, untrusted_name); // hopefully remote has produced error message
    untrusted_content[filelen] = 0;
    if (path_components < 1)
        abort(); // validate_path() should not have returned
    validate_path(untrusted_content, path_components - 1);

    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    if (symlinkat(untrusted_content, safe_dirfd, last_segment))
        do_exit(errno, untrusted_name);

    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    free(path_dup);
}

void process_one_file(struct file_header *untrusted_hdr, int flags)
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
    else if (S_ISLNK(untrusted_hdr->mode) && (flags & COPY_ALLOW_SYMLINKS))
        process_one_file_link(untrusted_hdr, untrusted_namebuf);
    else if (S_ISDIR(untrusted_hdr->mode) && (flags & COPY_ALLOW_DIRECTORIES))
        process_one_file_dir(untrusted_hdr, untrusted_namebuf);
    else
        do_exit(EINVAL, untrusted_namebuf);
    if (verbose && !S_ISDIR(untrusted_hdr->mode))
        fprintf(stderr, "%s\n", untrusted_namebuf);
}

int do_unpack(void) {
    return do_unpack_ext(COPY_ALLOW_DIRECTORIES | COPY_ALLOW_SYMLINKS);
}

int do_unpack_ext(int flags)
{
    struct file_header untrusted_hdr;
    int cwd_fd;
    int saved_errno;

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
        process_one_file(&untrusted_hdr, flags);
    }

    saved_errno = errno;
    cwd_fd = open(".", O_RDONLY);
    if (cwd_fd >= 0 && syncfs(cwd_fd) == 0 && close(cwd_fd) == 0)
        errno = saved_errno;

    send_status_and_crc(errno, untrusted_namebuf);
    return errno;
}
