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

#include <linux/major.h>
#include <linux/loop.h>
#include <linux/fs.h>

#include <xen/xen.h>
#include <xen/io/xenbus.h>
#include <xenstore.h>

static int open_file(const char *path) {
    return open(path, O_RDONLY | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW);
}

/* A simple library for loop device ioctls */
struct loop_context {
    uint32_t fd;
};

static int open_loop_dev(uint32_t devnum, bool writable)
{
    char buf[sizeof("/dev/loop") + 10];
    if ((unsigned)snprintf(buf, sizeof buf, "/dev/loop%u", (unsigned)devnum) >= sizeof buf)
        abort();
    int res = open(buf, (writable ? O_RDWR : O_RDONLY) |
                   O_EXCL | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
    return res == -1 ? -errno : res;
}

static int setup_loop(struct loop_context *ctx,
                      uint32_t fd,
                      uint64_t offset,
                      uint64_t sizelimit,
                      bool writable, bool autoclear) {
    int dev_file = -1, status;

    struct loop_config config = {
        .fd = fd,
        .block_size = 0, /* FIXME! */
        .info = {
            .lo_offset = offset,
            .lo_sizelimit = sizelimit,
            .lo_flags = LO_FLAGS_DIRECT_IO | (autoclear ? LO_FLAGS_AUTOCLEAR : 0) | (writable ? 0 : LO_FLAGS_READ_ONLY),
        },
    };

    for (int retry_count = 0; retry_count < 5 /* arbitrary */; retry_count++) {
        if ((status = ioctl(ctx->fd, LOOP_CTL_GET_FREE)) == -1)
            return -errno;
        dev_file = open_loop_dev(status, writable);
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
        if (errno != EBUSY && errno != ENXIO)
            break;
    }
    return -1;
}

#define DEV_MAPPER "/dev/mapper/"
#define DEV_MAPPER_SIZE (sizeof DEV_MAPPER - 1)

static void
process_blk_dev(int fd, const char *path, bool writable, dev_t *dev,
                uint64_t *diskseq, bool autoclear)
{
    struct stat statbuf;
    char buf[45];

    if (fstat(fd, &statbuf))
        err(1, "fstat");

    if (S_ISBLK(statbuf.st_mode)) {
        /* block device */
        if (strncmp(DEV_MAPPER, path, DEV_MAPPER_SIZE) != 0)
            goto skip;

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
        int loop_fd = setup_loop(&ctx, fd, 0, (uint64_t)statbuf.st_size, writable, autoclear);
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

static void validate_int_start(char **p, unsigned long *out, bool path)
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
    } else if (path) {
        errx(1, "Bad byte %d in XenStore path %s", s, *p);
    } else {
        errx(1, "Bad byte %d at start of Xenbus state", s);
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

static bool strict_strtoul_hex(char *p, char **endp, char expected, unsigned long *res)
{
    if (*p == '0') {
        *res = 0;
        *endp = p + 1;
    } else if ((*p < '1' || *p > '9') && (*p < 'a' && *p > 'f')) {
        return false;
    } else {
        errno = 0;
        *res = strtoul(p, endp, 16);
        if (*res > UINT32_MAX || errno)
            return false;
    }

    return **endp == expected;
}

static const char *const opened_key = "opened";

static bool get_opened(struct xs_handle *const h, char *const extra_path,
                       const char *const xenstore_path_buffer, char expected)
{
    strcpy(extra_path, opened_key);
    unsigned int value_len;
    char *value = xs_read(h, 0, xenstore_path_buffer, &value_len);
    if (value) {
        if ((value_len != 1) || (*value != expected))
            err(1, "Expected \"opened\" entry to be missing or contain \"%c\", but it contained %s",
                expected, value);
        free(value);
        return true;
    } else {
        if (errno != ENOENT) {
            err(1, "Error reading Xenstore key %s", xenstore_path_buffer);
        }
        return false;
    }
}

static void remove_device(struct xs_handle *const h, char *xenstore_path_buffer,
                          char *extra_path)
{
    char *physdev, *end_path, *diskseq_str;
    unsigned int path_len, diskseq_len;
    int loop_control_fd, loop_fd;
    uint64_t actual_diskseq;
    unsigned long _major, _minor;

    /*
     * Order matters here: the kernel only cares about "physical-device" for
     * now, so ensure that it gets removed first.
     */
    strcpy(extra_path, "physical-device");
    physdev = xs_read(h, 0, xenstore_path_buffer, &path_len);
    if (physdev == NULL) {
        err(1, "Cannot obtain physical device from XenStore path %s", xenstore_path_buffer);
    }

    if (!xs_rm(h, 0, xenstore_path_buffer))
        err(1, "xs_rm(\"%s\")", xenstore_path_buffer);

    strcpy(extra_path, "diskseq");
    diskseq_str = xs_read(h, 0, xenstore_path_buffer, &diskseq_len);
    if (diskseq_str == NULL) {
        err(1, "Cannot obtain diskseq from XenStore path %s", xenstore_path_buffer);
    }

    if (!xs_rm(h, 0, xenstore_path_buffer))
        err(1, "xs_rm(\"%s\")", xenstore_path_buffer);

    errno = 0;

    if ((!strict_strtoul_hex(physdev, &end_path, ':', &_major)) ||
        (!strict_strtoul_hex(end_path + 1, &end_path, '\0', &_minor)) ||
        (end_path != physdev + path_len)) {
        goto bad_physdev;
    }

    if (_major != LOOP_MAJOR)
        return; /* Not a loop device */

    if (diskseq_len != 16)
        goto bad_diskseq;

    for (unsigned i = 0; i < diskseq_len; ++i) {
        if ((diskseq_str[i] < '0' || diskseq_str[i] > '9') &&
            (diskseq_str[i] < 'a' || diskseq_str[i] > 'f')) {
            goto bad_diskseq;
        }
    }

    unsigned long long const diskseq = strtoull(diskseq_str, &end_path, 16);
    if (errno || end_path != diskseq_str + diskseq_len)
        abort(); /* cannot happen due to above check */
    if ((loop_control_fd = open_file("/dev/loop-control")) < 0)
        err(1, "open(/dev/loop-control)");

    if ((loop_fd = open_loop_dev(_minor, false)) < 0)
        err(1, "open(/dev/loop%lu)", _minor);

    struct stat statbuf;
    if (fstat(loop_fd, &statbuf))
        err(1, "fstat(\"/dev/loop%lu\")", _minor);

    if (statbuf.st_rdev != makedev(_major, _minor)) {
        errx(1, "Opened wrong loop device: expected (%lu, %lu) but got (%u, %u)!",
             _major, _minor, major(statbuf.st_rdev), minor(statbuf.st_rdev));
    }

    if (ioctl(loop_fd, BLKGETDISKSEQ, &actual_diskseq))
        err(1, "ioctl(%d, BLKGETDISKSEQ, %p)", loop_fd, &actual_diskseq);

    if (diskseq != actual_diskseq)
        errx(1, "Loop device diskseq mismatch!");

    if (ioctl(loop_fd, LOOP_CLR_FD))
        err(1, "ioctl(\"/dev/loop%lu\", LOOP_CLR_FD)", _minor);

    int res = ioctl(loop_control_fd, LOOP_CTL_REMOVE, (long)_minor);
    if (res != 0 && !(res == -1 && errno == EBUSY))
        err(1, "ioctl(%d, LOOP_CONTROL_REMOVE, %ld)", loop_control_fd, (long)_minor);

    if (close(loop_fd))
        err(1, "close(\"/dev/loop%lu\")", _minor);

    if (close(loop_control_fd))
        err(1, "close(\"/dev/loop-control\")");

    return;
bad_physdev:
    errx(1, "Bad physical device value %s", physdev);
bad_diskseq:
    errx(1, "Bad diskseq %s", diskseq_str);
}


int main(int argc, char **argv)
{
    enum {
        ADD,
        REMOVE,
    } action;
    redirect_stderr();
#define XENBUS_PATH_PREFIX "XENBUS_PATH="
#define BACKEND_VBD "backend/vbd/"
#define ARGV2_PREFIX (XENBUS_PATH_PREFIX BACKEND_VBD)

    const char *xs_path_raw = getenv("XENBUS_PATH");

    if (argc < 2 || argc > 3)
        errx(1, "Usage: [add|remove] [XENBUS_PATH=backend/vbd/REMOTE_DOMAIN/ID] (got %d arguments, expected 2 or 3)", argc);

    if (argc >= 3) {
        if (strncmp(argv[2], ARGV2_PREFIX, sizeof(ARGV2_PREFIX) - 1))
            errx(1, "Second argument must begin with XENBUS_PATH=backend/vbd/");
        const char *const new_path = argv[2] + (sizeof(XENBUS_PATH_PREFIX) - 1);

        if ((xs_path_raw != NULL) && (strcmp(xs_path_raw, new_path) != 0))
            errx(1, "XENBUS_PATH was passed both on the command line and in"
                    " the environment, but the values were different");

        xs_path_raw = new_path;
    } else if (xs_path_raw == NULL) {
        errx(1, "Xenstore path required");
    } else if (strncmp(xs_path_raw, BACKEND_VBD, sizeof(BACKEND_VBD) - 1)) {
        errx(1, "Bad Xenstore path %s", xs_path_raw);
    }

    if (strcmp(argv[1], "add") == 0) {
        action = ADD;
    } else if (strcmp(argv[1], "remove") == 0) {
        action = REMOVE;
    } else {
        errx(1, "Bad command (expected \"add\" or \"remove\")");
    }

    const char *const xs_path = xs_path_raw;
    size_t xs_path_len;

    {
        /* strtoul() is not const-correct, sorry... */
        char *xs_path_extra = (char *)(xs_path + (sizeof(BACKEND_VBD) - 1));
        unsigned long peer_domid, tmp;
        validate_int_start(&xs_path_extra, &peer_domid, true);
        if (*xs_path_extra++ != '/')
            errx(1, "Peer domain ID %lu not followed by '/'", peer_domid);
        validate_int_start(&xs_path_extra, &tmp, true);
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
    bool const autoclear = get_opened(h, extra_path, xenstore_path_buffer,
                                      action == REMOVE ? '1' : '0');

    if (action == REMOVE) {
        if (!autoclear)
            remove_device(h, xenstore_path_buffer, extra_path);
        return 0;
    }

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
                break;
            case 'w':
                writable = true;
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

    if ((fd = open(data, (writable ? O_RDWR : O_RDONLY) | O_NOCTTY | O_CLOEXEC | O_NONBLOCK)) < 0)
        err(1, "open(%s)", data);
    char phys_dev[16 + 8 + 8 + 2 + 1];

    process_blk_dev(fd, data, writable, &dev, &diskseq, autoclear);
    unsigned const int l =
        (unsigned)(autoclear ?
                   snprintf(phys_dev, sizeof phys_dev, "%" PRIx64 "@%lx:%lx",
                            diskseq, (unsigned long)major(dev), (unsigned long)minor(dev)) :
                   snprintf(phys_dev, sizeof phys_dev, "%lx:%lx",
                            (unsigned long)major(dev), (unsigned long)minor(dev)));


    if (l >= sizeof(phys_dev))
        err(1, "snprintf");

    if (autoclear) {
        strcpy(extra_path, opened_key);
        if (!xs_watch(h, xenstore_path_buffer, opened_key))
            err(1, "Cannot setup XenStore watch on %s", xenstore_path_buffer);
    }

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

        if (!autoclear) {
            char hex_diskseq[17];
            if (snprintf(hex_diskseq, sizeof(hex_diskseq), "%016llx", (unsigned long long)diskseq) != 16)
                err(1, "snprintf");
            strcpy(extra_path, "diskseq");
            if (!xs_write(h, t, xenstore_path_buffer, hex_diskseq, 16))
                err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, hex_diskseq);
        }

        if (xs_transaction_end(h, t, false))
            break;

        if (errno != EAGAIN)
            err(1, "xs_transaction_end");
    }

    if (autoclear) {
        strcpy(extra_path, opened_key);
        for (;;) {
            unsigned int num, state_len;
            char *value, **watch_res = xs_read_watch(h, &num);
            if (!watch_res)
                err(1, "xs_read_watch");
            warnx("Xenstore watch for %s triggered", watch_res[0]);
            free(watch_res);
            value = xs_read(h, 0, xenstore_path_buffer, &state_len);
            if (value) {
                if (state_len != 1 || (value[0] != '1' && value[0] != '0'))
                    errx(1, "bad value in Xenstore entry %s", xenstore_path_buffer);
                if (value[0] == '1')
                    break;
            } else {
                err(1, "xs_read(\"%s\")", xenstore_path_buffer);
            }
        }
    }
    xs_close(h);
}
