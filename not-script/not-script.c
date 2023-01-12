#define _GNU_SOURCE 1
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <err.h>

#include <linux/loop.h>
#include <linux/fs.h>

#include <xen/xen.h>
#include <xenstore.h>

static int open_file(const char *path) {
    return open(path, O_RDONLY | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW);
}

/* A simple library for loop device ioctls */
struct loop_context {
    uint32_t fd;
};

static int setup_loop(struct loop_context *ctx,
                      uint32_t fd,
                      uint64_t offset,
                      uint64_t sizelimit,
                      bool writable,
                      const char *path) {
    fprintf(stderr, "Setting up path %s (via FD %" PRIu32 ") with offset 0x%" PRIx64 " and size limit 0x%" PRIx64 " as %s\n",
         path, fd, offset, sizelimit, writable ? "writable" : "read-only");
    fflush(NULL);
    struct loop_config config = {
        .fd = fd,
        .block_size = 0, /* FIXME! */
        .info = {
            .lo_offset = offset,
            .lo_sizelimit = sizelimit,
            .lo_flags = LO_FLAGS_AUTOCLEAR | LO_FLAGS_DIRECT_IO |
                (writable ? 0 : LO_FLAGS_READ_ONLY),
        },
    };

    char buf[sizeof("/dev/loop") + 10];

    int dev_file = -1, status;
    for (int retry_count = 0; retry_count < 5 /* arbitrary */; retry_count++) {
        if ((status = ioctl(ctx->fd, LOOP_CTL_GET_FREE)) == -1)
            return -errno;
        if ((unsigned)snprintf(buf, sizeof buf, "/dev/loop%u", (unsigned)status) >= sizeof buf)
            abort();
        dev_file = open(buf, (writable ? O_RDWR : O_RDONLY) |
                              O_EXCL | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
        if (dev_file > 0) {
            switch (ioctl(dev_file, LOOP_CONFIGURE, &config)) {
            case 0:
                return dev_file;
            case -1:
                if (close(dev_file))
                    abort(); /* cannot happen on Linux */
                return -1;
            default:
                abort();
            }
        }
        if (errno != EBUSY)
            break;
    }
    return -1;
}

#define DEV_MAPPER "/dev/mapper/"
#define DEV_MAPPER_SIZE (sizeof DEV_MAPPER - 1)

static void
process_blk_dev(int fd, const char *path, bool writable, dev_t *dev,
                uint64_t *diskseq, bool permissive)
{
    struct stat statbuf;
    char buf[45];

    if (fstat(fd, &statbuf))
        err(1, "fstat");

    if (S_ISBLK(statbuf.st_mode)) {
        if (permissive)
            goto skip;

        /* block device */
        if (strncmp(DEV_MAPPER, path, DEV_MAPPER_SIZE))
            errx(1, "Only device-mapper block devices are supported (got %s)", path);

        const char *const devname = path + DEV_MAPPER_SIZE;
        size_t const devname_len = strlen(devname);
        if (strcmp(devname, "control") == 0 ||
            strcmp(devname, ".") == 0 ||
            strcmp(devname, "..") == 0 ||
            memchr(devname, '/', devname_len) != NULL)
        {
            errx(1, "Forbidden path %s", devname);
        }

        if ((unsigned)snprintf(buf, sizeof buf, "/sys/dev/block/%" PRIu32 ":%" PRIu32 "/dm/name",
                               major(statbuf.st_rdev), minor(statbuf.st_rdev)) >= sizeof(buf))
            err(1, "snprintf");

        int const sysfs_fd = open(buf, O_RDONLY | O_NOCTTY | O_NOFOLLOW);
        if (sysfs_fd < 0)
            err(1, "open(%s)", buf);

        char *const cmp_ptr = malloc(devname_len + 2);
        if (!cmp_ptr)
            err(1, "malloc()");

        ssize_t const read_res = read(sysfs_fd, cmp_ptr, devname_len + 2);
        if (read_res < 0)
            err(1, "read(%s)", buf);

        if (((size_t)read_res != devname_len + 1) ||
            (memcmp(devname, cmp_ptr, devname_len) != 0) ||
            (cmp_ptr[devname_len] != '\n'))
        {
            errx(1, "Opened the wrong device-mapper device!");
        }

        free(cmp_ptr);
        if (close(sysfs_fd))
            err(1, "close()");
    } else if (S_ISREG(statbuf.st_mode)) {
        int ctrl_fd = open_file("/dev/loop-control");
        if (ctrl_fd < 0)
            err(1, "open(/dev/loop-control)");
        struct loop_context ctx = { .fd = ctrl_fd };
        int loop_fd = setup_loop(&ctx, fd, 0, (uint64_t)statbuf.st_size, writable, path);
        if (loop_fd < 0)
            err(1, "loop device setup failed");
        if (dup3(loop_fd, fd, O_CLOEXEC) != fd)
            err(1, "dup3(%d, %d, O_CLOEXEC)", loop_fd, fd);
        if (close(loop_fd))
            err(1, "close(%d)", loop_fd);
        if (close(ctx.fd))
            err(1, "close(%d)", ctx.fd);
        if (fstat(fd, &statbuf))
            err(1, "fstat");
    } else {
        errx(1, "Path %s is neither a block device nor regular file", path);
    }
skip:
    *dev = statbuf.st_rdev;
#ifndef BLKGETDISKSEQ
#define BLKGETDISKSEQ _IOR(0x12,128,__u64)
#else
    static_assert(BLKGETDISKSEQ == _IOR(0x12,128,__u64),
                  "wrong BLKGETDISKSEQ definition?");
#endif
    if (ioctl(fd, BLKGETDISKSEQ, diskseq))
        err(1, "ioctl(%d, BLKGETDISKSEQ, %p)", fd, diskseq);
}

static void validate_int_start(char **p, unsigned long *out)
{
    char const s = **p;
    if (s == '0') {
        *out = 0;
        (*p)++;
    } else if (s >= '1' && s <= '9') {
        errno = 0;
        *out = strtoul(*p, p, 10);
        if (errno)
            err(1, "strtoul()");
    } else {
        errx(1, "Bad char '%c' in XenStore path %s", s, *p);
    }
}

static void redirect_stderr(void)
{
    int const redirect_fd = open("/var/log/xen/xen-hotplug.log", O_RDWR|O_NOCTTY|O_CLOEXEC|O_APPEND|O_CREAT, 0640);
    if (redirect_fd < 0) {
        if (errno == ENOENT || errno == EACCES)
            return;
        err(1, "open()");
    }
    if (dup2(redirect_fd, 2) != 2)
        err(1, "dup2(%d, 2)", redirect_fd);
    if (close(redirect_fd))
        err(1, "close(%d)", redirect_fd);
}

int main(int argc, char **argv)
{
    bool permissive = false;
    redirect_stderr();
#define XENBUS_PATH_PREFIX "XENBUS_PATH="
#define BACKEND_VBD "backend/vbd/"
#define ARGV2_PREFIX (XENBUS_PATH_PREFIX BACKEND_VBD)

    const char *xs_path_raw = getenv("XENBUS_PATH");

    if (argc < 2 || argc > 3)
        errx(1, "Usage: [add|remove] [XENBUS_PATH=backend/vbd/REMOTE_DOMAIN/ID] (got %d arguments, expected 2 or 3)", argc);

    const char *last_slash = strrchr(argv[0], '/');
    last_slash = last_slash ? last_slash + 1 : argv[0];

    if (strcmp(last_slash, "block") == 0)
        permissive = true;

    if (strcmp(argv[1], "add") == 0) {
        if (argc >= 3) {
            if (strncmp(argv[2], ARGV2_PREFIX, sizeof(ARGV2_PREFIX) - 1))
                errx(1, "Second argument must begin with XENBUS_PATH=backend/vbd/");
            const char *const new_path = argv[2] + (sizeof(XENBUS_PATH_PREFIX) - 1);

            if ((xs_path_raw != NULL) && (strcmp(xs_path_raw, new_path) != 0))
                errx(1, "XENBUS_PATH was passed both on the command line and in"
                        " the environment, but the values were different");

            xs_path_raw = new_path;
        } else if (xs_path_raw == NULL) {
            errx(1, "Xenstore path required when adding");
        } else if (strncmp(xs_path_raw, BACKEND_VBD, sizeof(BACKEND_VBD) - 1)) {
            errx(1, "Bad Xenstore path %s", xs_path_raw);
        }
    } else if (strcmp(argv[1], "remove") == 0)
        exit(0);
    else
        errx(1, "Bad command (expected \"add\" or \"remove\")");

    const char *const xs_path = xs_path_raw;
    size_t xs_path_len;

    {
        /* strtoul() is not const-correct, sorry... */
        char *xs_path_extra = (char *)(xs_path + (sizeof(BACKEND_VBD) - 1));
        unsigned long peer_domid, tmp;
        validate_int_start(&xs_path_extra, &peer_domid);
        if (*xs_path_extra++ != '/')
            errx(1, "Peer domain ID %lu not followed by '/'", peer_domid);
        validate_int_start(&xs_path_extra, &tmp);
        if (*xs_path_extra)
            errx(1, "Junk after XenStore device ID %lu", tmp);
        if (peer_domid >= DOMID_FIRST_RESERVED)
            errx(1, "Peer domain ID %lu too large (limit %d)", peer_domid, DOMID_FIRST_RESERVED);
        xs_path_len = (size_t)(xs_path_extra - xs_path);
    }

    struct xs_handle *const h = xs_open(0);
    if (!h)
        err(1, "Cannot connect to XenStore");

    size_t const buffer_size = xs_path_len + sizeof("/physical-device-path");
    char *const xenstore_path_buffer = malloc(buffer_size);
    if (!xenstore_path_buffer)
        err(1, "malloc()");
    memcpy(xenstore_path_buffer, xs_path, xs_path_len);
    xenstore_path_buffer[xs_path_len] = '/';
    /* Buffer to copy extra data into */
    char *const extra_path = xenstore_path_buffer + xs_path_len + 1;
    unsigned int len, path_len;

    strcpy(extra_path, "params");
    char *data = xs_read(h, 0, xenstore_path_buffer, &path_len);
    if (data == NULL)
        err(1, "Cannot obtain parameters from XenStore path %s", xenstore_path_buffer);

    if (strlen(data) != path_len)
        errx(1, "NUL in parameters");

    if (data[0] != '/')
        errx(1, "Parameters not an absolute path");

    unsigned int writable;

    {
        strcpy(extra_path, "mode");
        char *rw = xs_read(h, 0, xenstore_path_buffer, &len);
        if (rw == NULL) {
            if (errno != ENOENT)
                err(1, "xs_read(%s)", xenstore_path_buffer);
            writable = false;
        } else {
            switch (rw[0]) {
            case 'r':
                writable = false;
                fprintf(stderr, "XenStore key %s specifies read-only\n", xenstore_path_buffer);
                fflush(NULL);
                break;
            case 'w':
                writable = true;
                fprintf(stderr, "XenStore key %s specifies writable\n", xenstore_path_buffer);
                fflush(NULL);
                break;
            default:
                len = 0;
                break;
            }
            if (len != 1)
                errx(1, "Bad data in XenStore key %s: expected 'r' or 'w'", xenstore_path_buffer);
            free(rw);
        }
    }

    int fd;
    dev_t dev;
    uint64_t diskseq;

    if ((fd = open(data, O_RDONLY | O_NOCTTY | O_CLOEXEC | O_NONBLOCK)) < 0)
        err(1, "open(%s)", data);
    char phys_dev[18], hex_diskseq[17];

    process_blk_dev(fd, data, writable, &dev, &diskseq, permissive);
    unsigned const int l =
        (unsigned)snprintf(phys_dev, sizeof phys_dev, "%lx:%lx",
                           (unsigned long)major(dev), (unsigned long)minor(dev));

    if (l >= sizeof(phys_dev))
        err(1, "snprintf");
    if (snprintf(hex_diskseq, sizeof(hex_diskseq), "%016llx", (unsigned long long)diskseq) != 16)
        err(1, "snprintf");

    const char *watch_token = "state";
    strcpy(extra_path, watch_token);

    if (!xs_watch(h, xenstore_path_buffer, watch_token))
        err(1, "Cannot setup XenStore watch on %s", xenstore_path_buffer);

    for (;;) {
        xs_transaction_t t = xs_transaction_start(h);
        if (!t)
            err(1, "Cannot start XenStore transaction");

        strcpy(extra_path, "physical-device");
        if (!xs_write(h, t, xenstore_path_buffer, phys_dev, l))
            err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, phys_dev);

        strcpy(extra_path, "physical-device-path");
        if (!xs_write(h, t, xenstore_path_buffer, data, path_len))
            err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, data);

        strcpy(extra_path, "diskseq");
        if (!xs_write(h, t, xenstore_path_buffer, hex_diskseq, 16))
            err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, hex_diskseq);

        if (xs_transaction_end(h, t, false))
            break;

        if (errno != EAGAIN)
            err(1, "xs_transaction_end");
    }

    unsigned int num;
    char **watch_res = xs_read_watch(h, &num);
    if (!watch_res)
        err(1, "xs_read_watch");

    free(watch_res);
    xs_close(h);
}
