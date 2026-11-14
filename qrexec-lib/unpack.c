#define _GNU_SOURCE /* For O_NOFOLLOW. */
#define U_HIDE_DEPRECATED_API U_HIDE_DEPRECATED_API
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
#include "pure.h"
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
static conflict_handler_t *conflict_handler = NULL;
static unsigned long long skipped_files = 0;

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

static _Noreturn void do_exit(int code, const char *last_filename)
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

void register_conflict_handler(conflict_handler_t *func)
{
    conflict_handler = func;
}

void set_procfs_fd(int value)
{
    procdir_fd = value;
    use_tmpfile = 1;
}

static int wait_for_space(int fd, unsigned long how_much) {
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
static int read_all_with_crc(int fd, void *buf, int size) {
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
    /* Do not change the mode of symbolic links */
    if (!S_ISLNK(untrusted_hdr->mode) &&
            fchmod(fd, untrusted_hdr->mode & 07777))
        do_exit(errno, untrusted_name);
    if (futimens(fd, times))  /* as above */
        do_exit(errno, untrusted_name);
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
        if (next == NULL) {
            *last_segment = this_segment;
            return cur_fd;
        }
        *next = '\0';
        next_segment = next + 1;
        if ((next - this_segment <= 2) &&
            (memcmp(this_segment, "..", (size_t)(next - this_segment)) == 0)) {
            fprintf(stderr, "BUG: path component '%s' not rejected earlier!\n", this_segment);
            abort();
        }
        int new_fd = openat(cur_fd, this_segment, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NOCTTY | O_CLOEXEC);
        if (new_fd == -1)
            do_exit(errno, this_segment);
        if (cur_fd != dirfd)
            close(cur_fd);
        cur_fd = new_fd;
    }
}

enum create_mode {
    CREATE_REGULAR, /* new empty file, its fd stored in *fd */
    CREATE_LINK,    /* link of the O_TMPFILE file *fd */
    CREATE_SYMLINK, /* symlink to target */
};

/* creates an entry named "name" in dirfd; returns 0 or an errno value */
static int create_one(enum create_mode mode, int dirfd, const char *name,
                      int *fd, const char *target)
{
    char fd_str[11];

    switch (mode) {
    case CREATE_REGULAR:
        *fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NOCTTY, 0000);
        return *fd < 0 ? errno : 0;
    case CREATE_LINK:
        if ((unsigned)snprintf(fd_str, sizeof(fd_str), "%d", *fd) >= sizeof(fd_str))
            abort();
        return linkat(procdir_fd, fd_str, dirfd, name, AT_SYMLINK_FOLLOW) < 0 ? errno : 0;
    case CREATE_SYMLINK:
        return symlinkat(target, dirfd, name) < 0 ? errno : 0;
    }
    abort();
}

/* suggest a free name like "name_1.ext" in dirfd, or "" if none found */
static void suggest_name(int dirfd, const char *name, char *out, size_t size)
{
    const char *dot = strrchr(name, '.');
    int base_len = (dot && dot != name) ? (int)(dot - name) : (int)strlen(name);
    const char *ext;
    struct stat st;

    if (base_len > 4 && strncmp(name + base_len - 4, ".tar", 4) == 0)
        base_len -= 4;
    ext = name + base_len;
    for (unsigned int i = 1; i < 1000; i++) {
        int len = snprintf(out, size, "%.*s_%u%s", base_len, name, i, ext);
        if (len < 0)
            break;
        if ((size_t)len >= size) {
            /* shorten the base, without cutting a UTF-8 character */
            int cut = base_len - (len - (int)size + 1);
            while (cut > 0 && (name[cut] & 0xC0) == 0x80)
                cut--;
            if (cut <= 0)
                break;
            snprintf(out, size, "%.*s_%u%s", cut, name, i, ext);
        }
        if (fstatat(dirfd, out, &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT)
            return;
    }
    out[0] = '\0';
}

static int valid_new_name(const char *name)
{
    return name[0] && !strchr(name, '/') &&
        strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

/*
 * Like create_one(), but if the entry already exists, ask the conflict handler
 * what to do. Returns 0 if the file is skipped, 1 if created.
 */
static int create_entry(int dirfd, const char *name, const char *untrusted_name,
                        enum create_mode mode, int *fd, const char *target)
{
    char new_name[NAME_MAX + 1];
    const char *error = NULL;
    struct stat st;
    int can_overwrite, ret;

    ret = create_one(mode, dirfd, name, fd, target);
    if (ret == 0)
        return 1;
    if (ret != EEXIST || !conflict_handler)
        do_exit(ret, untrusted_name);

    suggest_name(dirfd, name, new_name, sizeof(new_name));
    for (;;) {
        can_overwrite = !(fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                          S_ISDIR(st.st_mode));
        switch (conflict_handler(untrusted_name, can_overwrite, new_name,
                                 sizeof(new_name), error)) {
        case CONFLICT_OVERWRITE:
            /* with !can_overwrite, a bug in the handler: abort */
            if (!can_overwrite)
                do_exit(EEXIST, untrusted_name);
            if (unlinkat(dirfd, name, 0) < 0)
                do_exit(errno, untrusted_name);
            ret = create_one(mode, dirfd, name, fd, target);
            if (ret)
                do_exit(ret, untrusted_name);
            fprintf(stderr, "%s: replaced the existing file\n", untrusted_name);
            return 1;
        case CONFLICT_SKIP:
            skipped_files++;
            fprintf(stderr, "%s: skipped, a file with this name already exists\n",
                    untrusted_name);
            return 0;
        case CONFLICT_RENAME:
            new_name[sizeof(new_name) - 1] = '\0';
            if (!valid_new_name(new_name)) {
                error = "Invalid name.";
                continue;
            }
            ret = create_one(mode, dirfd, new_name, fd, target);
            if (ret == EEXIST) {
                error = "A file with this name already exists too.";
                continue;
            }
            if (ret)
                do_exit(ret, untrusted_name);
            fprintf(stderr, "%s: saved as %s\n", untrusted_name, new_name);
            return 1;
        case CONFLICT_ABORT:
        default:
            do_exit(EEXIST, untrusted_name);
        }
    }
}

static void discard_data(unsigned long long len, const char *untrusted_name)
{
    char buf[4096];
    while (len) {
        int count = len > sizeof(buf) ? (int)sizeof(buf) : (int)len;
        if (!read_all_with_crc(0, buf, count))
            do_exit(LEGAL_EOF, untrusted_name);
        len -= count;
    }
}

static void process_one_file_reg(struct file_header *untrusted_hdr,
                                 const char *untrusted_name,
                                 uint32_t flags)
{
    int ret;
    int fdout = -1, safe_dirfd;
    const char *last_segment;
    char *path_dup;

    ret = qubes_pure_validate_file_name_v2((const uint8_t *)untrusted_name, flags);
    if (ret != 0)
        do_exit(-ret, untrusted_name); /* FIXME: better error message */
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

    /* sizes are signed elsewhere */
    if (untrusted_hdr->filelen > LLONG_MAX || (bytes_limit && untrusted_hdr->filelen > bytes_limit))
        do_exit(EDQUOT, untrusted_name);
    if (bytes_limit && total_bytes > bytes_limit - untrusted_hdr->filelen)
        do_exit(EDQUOT, untrusted_name);
    total_bytes += untrusted_hdr->filelen;

    if (fdout < 0 && !create_entry(safe_dirfd, last_segment, untrusted_name,
                                   CREATE_REGULAR, &fdout, NULL)) {
        discard_data(untrusted_hdr->filelen, untrusted_name);
        goto out;
    }
    if (opt_wait_for_space_margin) {
        wait_for_space(fdout,
            untrusted_hdr->filelen + opt_wait_for_space_margin);
    }
    ret = copy_file(fdout, 0, untrusted_hdr->filelen, &crc32_sum);
    if (ret != COPY_FILE_OK) {
        if (ret == COPY_FILE_READ_EOF
                || ret == COPY_FILE_READ_ERROR)
            do_exit(LEGAL_EOF, untrusted_name); // hopefully remote will produce error message
        else
            do_exit(errno, untrusted_name);
    }
    if (use_tmpfile && !create_entry(safe_dirfd, last_segment, untrusted_name,
                                     CREATE_LINK, &fdout, NULL))
        goto out;
    fix_times_and_perms(fdout, untrusted_hdr, untrusted_name);
out:
    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    if (fdout >= 0)
        close(fdout);
    free(path_dup);
}


static void process_one_file_dir(struct file_header *untrusted_hdr,
                                 const char *untrusted_name,
                                 uint32_t flags)
{
    int safe_dirfd;
    const char *last_segment;
    char *path_dup;
    int rc = qubes_pure_validate_file_name_v2((const uint8_t *)untrusted_name, flags);
    if (rc != 0)
        do_exit(rc, untrusted_name); /* FIXME: better error message */
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
    int new_dirfd = openat(safe_dirfd, last_segment, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY);
    if (new_dirfd < 0 || fstat(new_dirfd, &buf) < 0)
        do_exit(errno, untrusted_name);
    total_bytes += buf.st_size;
    /* size accumulated after the fact, so don't check limit here */
    fix_times_and_perms(new_dirfd, untrusted_hdr, untrusted_name);
    close(new_dirfd);
    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    free(path_dup);
}

static void process_one_file_link(struct file_header *untrusted_hdr,
                                  const char *untrusted_name,
                                  uint32_t flags)
{
    char untrusted_content[MAX_PATH_LENGTH];
    const char *last_segment;
    char *path_dup;
    unsigned int filelen;
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
    /*
     * Sanitize both the path of the symbolic link and its target.
     * Ensure that no immediate subdirectory of ~/QubesIncoming/VMNAME
     * may have symlinks that point out of it.
     */
    int rc = qubes_pure_validate_symbolic_link_v2((const uint8_t *)untrusted_name,
                                                  (const uint8_t *)untrusted_content,
                                                  flags);
    if (rc != 0)
        do_exit(-rc, untrusted_content);

    if ((path_dup = strdup(untrusted_name)) == NULL)
        do_exit(ENOMEM, untrusted_name);
    safe_dirfd = opendir_safe(AT_FDCWD, path_dup, &last_segment);

    create_entry(safe_dirfd, last_segment, untrusted_name,
                 CREATE_SYMLINK, NULL, untrusted_content);

    if (safe_dirfd != AT_FDCWD)
        close(safe_dirfd);
    free(path_dup);
}

static void process_one_file(struct file_header *untrusted_hdr, int flags)
{
    unsigned int namelen;
    if (untrusted_hdr->namelen > MAX_PATH_LENGTH - 1)
        do_exit(ENAMETOOLONG, NULL); /* filename too long so not received at all */
    namelen = untrusted_hdr->namelen; /* sanitized above */
    // Never set QUBES_PURE_ALLOW_NON_CANONICAL_PATHS -- paths from qfile-agent
    // will always be canonical.
    uint32_t validate_flags = ((uint32_t)flags >> 2) &
        (QUBES_PURE_ALLOW_UNSAFE_CHARACTERS | QUBES_PURE_ALLOW_UNSAFE_SYMLINKS |
         QUBES_PURE_ALLOW_NON_CANONICAL_SYMLINKS);
    if (!read_all_with_crc(0, untrusted_namebuf, namelen))
        do_exit(LEGAL_EOF, NULL); // hopefully remote has produced error message
    untrusted_namebuf[namelen] = 0;
    if (S_ISREG(untrusted_hdr->mode))
        process_one_file_reg(untrusted_hdr, untrusted_namebuf, validate_flags);
    else if (S_ISLNK(untrusted_hdr->mode) && (flags & COPY_ALLOW_SYMLINKS))
        process_one_file_link(untrusted_hdr, untrusted_namebuf, validate_flags);
    else if (S_ISDIR(untrusted_hdr->mode) && (flags & COPY_ALLOW_DIRECTORIES))
        process_one_file_dir(untrusted_hdr, untrusted_namebuf, validate_flags);
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
    int end_of_transfer_marker_seen = 0;
    int cwd_fd;
    int saved_errno;

    total_bytes = total_files = skipped_files = 0;
    /* initialize checksum */
    crc32_sum = 0;
    while (read_all_with_crc(0, &untrusted_hdr, sizeof untrusted_hdr)) {
        if (untrusted_hdr.namelen == 0) {
            end_of_transfer_marker_seen = 1;
            errno = 0;
            break;
        }
        total_files++;
        if (files_limit && total_files > files_limit)
            do_exit(EDQUOT, untrusted_namebuf);
        process_one_file(&untrusted_hdr, flags);
    }
    if (!end_of_transfer_marker_seen && !errno)
        errno = EREMOTEIO;
    else if (end_of_transfer_marker_seen && skipped_files)
        errno = FILECOPY_SKIPPED;

    saved_errno = errno;
    cwd_fd = open(".", O_RDONLY);
    if (cwd_fd >= 0 && syncfs(cwd_fd) == 0 && close(cwd_fd) == 0)
        errno = saved_errno;

    send_status_and_crc(errno, untrusted_namebuf);
    return errno;
}
