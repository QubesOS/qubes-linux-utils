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

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <err.h>

#include <linux/major.h>
#include <linux/loop.h>
#include <linux/fs.h>
#include <linux/dm-ioctl.h>

#include <xen/xen.h>
#include <xen/io/xenbus.h>
#include <xenstore.h>

#define DEV_MAPPER "/dev/mapper/"
#define DEV_MAPPER_SIZE (sizeof DEV_MAPPER - 1)
#define LOCK_FILE "/run/lock/qubes-script.lock"

static int open_file(const char *path) {
    return open(path, O_RDONLY | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW);
}


#define APPEND_TO_XENSTORE_PATH(extra_path, x) do {                                       \
    static_assert(__builtin_types_compatible_p(__typeof__(x), const char[sizeof(x)]),     \
                  "Only string literals supported, got " #x);                             \
    static_assert(__builtin_types_compatible_p(__typeof__("" x), const char[sizeof(x)]),  \
                  "Only string literals supported, got " #x);                             \
    static_assert(sizeof(x) <= sizeof("physical-device-path"),                            \
                  "buffer overflow averted");                                             \
    strcpy(extra_path, (x));                                                              \
} while (0)


static int open_loop_dev(uint32_t devnum, bool writable)
{
    char buf[sizeof("/dev/loop") + 10];
    if ((unsigned)snprintf(buf, sizeof buf, "/dev/loop%u", (unsigned)devnum) >= sizeof buf)
        abort();
    int flags = (writable ? O_RDWR : O_RDONLY) |
                   O_EXCL | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW;
    return open(buf, flags);
}

static int setup_loop(int ctrl_fd, uint32_t fd, bool writable, bool autoclear) {
    int dev_file = -1;
    int retry_count = 0;
    const int retry_limit = 5; /* arbitrary */
    int lock = open(LOCK_FILE, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (lock < 0)
        err(1, "open(\"%s\")", LOCK_FILE);
    if (flock(lock, LOCK_EX))
        err(1, "flock(\"%s\")", LOCK_FILE);

    struct loop_config config = {
        .fd = fd,
        .block_size = 512, /* FIXME! */
        .info = {
            .lo_flags = LO_FLAGS_DIRECT_IO | (autoclear ? LO_FLAGS_AUTOCLEAR : 0) | (writable ? 0 : LO_FLAGS_READ_ONLY),
        },
    };

    for (;; retry_count++) {
        int status = ioctl(ctrl_fd, retry_count ? LOOP_CTL_ADD : LOOP_CTL_GET_FREE, -1);
        if (status < 0)
            err(1, "ioctl(%d, LOOP_CTL_%s)", ctrl_fd, retry_count ? "ADD" : "GET_FREE");
        dev_file = open_loop_dev(status, writable);
        if (dev_file < 0) {
            assert(dev_file == -1);
            if ((errno != EAGAIN && errno != EBUSY && errno != ENXIO && errno != ENOENT) ||
                (retry_count >= retry_limit))
            {
                err(1, "open(\"/dev/loop%d\"): retry count %d, retry limit %d",
                    status, retry_count, retry_limit);
            }
            else
            {
                warn("open(\"/dev/loop%d\"): retry count %d, retry limit %d",
                     status, retry_count, retry_limit);
            }
            continue;
        }
        switch (ioctl(dev_file, LOOP_CONFIGURE, &config)) {
        case 0:
            close(lock);
            return dev_file;
        case -1:
            if ((errno != EBUSY && errno != ENXIO) || (retry_count >= retry_limit))
                err(1, "ioctl(%d, LOOP_CONFIGURE, %p): retry count %d, retry limit %d",
                    dev_file, &config, retry_count, retry_limit);
            if (close(dev_file))
                abort(); /* cannot happen on Linux */
            break;
        default:
            abort();
        }
    }
}

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
        if (strcmp(devname, DM_CONTROL_NODE) == 0 ||
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
        int loop_fd = setup_loop(ctrl_fd, fd, writable, autoclear);
        if (loop_fd < 0)
            err(1, "loop device setup failed");
        if (dup3(loop_fd, fd, O_CLOEXEC) != fd)
            err(1, "dup3(%d, %d, O_CLOEXEC)", loop_fd, fd);
        if (close(loop_fd))
            err(1, "close(%d)", loop_fd);
        if (close(ctrl_fd))
            err(1, "close(%d)", ctrl_fd);
        if (fstat(fd, &statbuf))
            err(1, "fstat");
    } else {
        errx(1, "Path %s is neither a block device nor regular file", path);
    }
skip:
    *dev = statbuf.st_rdev;
#ifndef BLKGETDISKSEQ
#define BLKGETDISKSEQ _IOR(0x12,128,__u64)
#endif
    if (ioctl(fd, BLKGETDISKSEQ, diskseq))
        err(1, "ioctl(%d, BLKGETDISKSEQ, %p)", fd, diskseq);
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

static bool strict_strtoul_hex(char *p, char **endp, char expected, uint64_t *res, uint64_t max)
{
    switch (*p) {
    case '0':
        *res = 0;
        *endp = p + 1;
        break;
    case '1'...'9':
    case 'a'...'f':
    case 'A'...'F':
        errno = 0;
        unsigned long long out = strtoull(p, endp, 16);
        if (out > max || errno)
            return false;
        *res = out;
        break;
    default:
        return false;
    }

    return **endp == expected;
}

#define OPENED_KEY "opened"

static bool get_opened(struct xs_handle *const h,
                       const char *const xenstore_path_buffer,
                       char expected)
{
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
    int loop_control_fd, loop_fd;
    uint64_t actual_diskseq, expected_diskseq;
    uint64_t _major, _minor;

    /*
     * Order matters here: the kernel only cares about "physical-device" for
     * now, so ensure that it gets removed first.
     */
    {
        APPEND_TO_XENSTORE_PATH(extra_path, "physical-device");
        unsigned int path_len;
        char *physdev_end;
        char *physdev = xs_read(h, 0, xenstore_path_buffer, &path_len);
        if (physdev == NULL) {
            err(1, "Cannot obtain physical device from XenStore path %s", xenstore_path_buffer);
        }

        if (!xs_rm(h, 0, xenstore_path_buffer)) {
            err(1, "xs_rm(\"%s\")", xenstore_path_buffer);
        }

        if ((!strict_strtoul_hex(physdev, &physdev_end, ':', &_major, UINT32_MAX)) ||
            (!strict_strtoul_hex(physdev_end + 1, &physdev_end, '\0', &_minor, UINT32_MAX)) ||
            (physdev_end != physdev + path_len))
        {
            errx(1, "Bad physical device value %s", physdev);
        }
        free(physdev);
    }

    {
        APPEND_TO_XENSTORE_PATH(extra_path, "diskseq");
        unsigned int diskseq_len;
        char *diskseq_end;
        char *diskseq_str = xs_read(h, 0, xenstore_path_buffer, &diskseq_len);
        if (diskseq_str == NULL) {
            err(1, "Cannot obtain diskseq from XenStore path %s", xenstore_path_buffer);
        }

        if (!xs_rm(h, 0, xenstore_path_buffer)) {
            err(1, "xs_rm(\"%s\")", xenstore_path_buffer);
        }

        if ((!strict_strtoul_hex(diskseq_str, &diskseq_end, '\0', &expected_diskseq, UINT64_MAX)) ||
            (diskseq_end != diskseq_str + diskseq_len))
        {
            errx(1, "Bad diskseq %s", diskseq_str);
        }
        free(diskseq_str);
    }

    errno = 0;

    if (_major != LOOP_MAJOR)
        return; /* Not a loop device */

    {
        /* Check if the device was created by us */
        APPEND_TO_XENSTORE_PATH(extra_path, "physical-device-path");
        unsigned int dev_len, params_len;
        char *physical_device_path = xs_read(h, 0, xenstore_path_buffer, &dev_len);
        if (physical_device_path == NULL)
            err(1, "xs_read(%s)", xenstore_path_buffer);
        APPEND_TO_XENSTORE_PATH(extra_path, "params");
        char *params = xs_read(h, 0, xenstore_path_buffer, &params_len);
        if (params == NULL)
            err(1, "xs_read(%s)", xenstore_path_buffer);
        if (params_len == dev_len && memcmp(physical_device_path, params, dev_len) == 0)
            return; /* Not created by this code */
        free(params);
        free(physical_device_path);
    }

    if ((loop_control_fd = open_file("/dev/loop-control")) < 0)
        err(1, "open(/dev/loop-control)");

    loop_fd = open_loop_dev(_minor, false);
    if (loop_fd < 0)
        err(1, "open(\"/dev/loop/%lu\")", _minor);

    struct stat statbuf;
    if (fstat(loop_fd, &statbuf))
        err(1, "fstat(\"/dev/loop%lu\")", _minor);

    if (statbuf.st_rdev != makedev(_major, _minor)) {
        errx(1, "Opened wrong loop device: expected (%lu, %lu) but got (%u, %u)!",
             _major, _minor, major(statbuf.st_rdev), minor(statbuf.st_rdev));
    }

    if (ioctl(loop_fd, BLKGETDISKSEQ, &actual_diskseq))
        err(1, "ioctl(%d, BLKGETDISKSEQ, %p)", loop_fd, &actual_diskseq);

    if (expected_diskseq != actual_diskseq)
        errx(1, "Loop device diskseq mismatch!");

    if (ioctl(loop_fd, LOOP_CLR_FD) && errno != EBUSY)
        err(1, "ioctl(\"/dev/loop%" PRIu64 "\", LOOP_CLR_FD)", _minor);

    if (close(loop_fd))
        err(1, "close(\"/dev/loop%" PRIu64 "\")", _minor);

    if (0) {
        warnx("About to destroy loop device %" PRIu64, _minor);
        int res = ioctl(loop_control_fd, LOOP_CTL_REMOVE, (long)_minor);
        if (res != 0 && !(res == -1 && errno == EBUSY))
            err(1, "ioctl(%d, LOOP_CTL_REMOVE, %ld)", loop_control_fd, (long)_minor);
    }

    if (close(loop_control_fd))
        err(1, "close(\"/dev/loop-control\")");

    return;
}


int main(int argc, char **argv)
{
    enum {
        ADD,
        REMOVE,
    } action;
    redirect_stderr();

    const char *const xs_path = getenv("XENBUS_PATH");

    if (argc != 2)
        errx(1, "Usage: [add|remove] (got %d arguments, expected 2)", argc);

    if (!xs_path)
        errx(1, "XENBUS_PATH not set");

    if (strcmp(argv[1], "add") == 0) {
        action = ADD;
    } else if (strcmp(argv[1], "remove") == 0) {
        action = REMOVE;
    } else {
        errx(1, "Bad command (expected \"add\" or \"remove\")");
    }

    size_t xs_path_len = strlen(xs_path);
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

    if (action == REMOVE) {
        remove_device(h, xenstore_path_buffer, extra_path);
        free(xenstore_path_buffer);
        return 0;
    }

    unsigned int path_len;
    APPEND_TO_XENSTORE_PATH(extra_path, OPENED_KEY);
    bool const autoclear = getenv("QUBES_EXPERIMENTAL_XENSTORE_UAPI") ?
        get_opened(h, xenstore_path_buffer, action == REMOVE ? '1' : '0') : false;

    APPEND_TO_XENSTORE_PATH(extra_path, "params");
    char *data = xs_read(h, 0, xenstore_path_buffer, &path_len);
    if (data == NULL)
        err(1, "Cannot obtain parameters from XenStore path %s", xenstore_path_buffer);

    if (strlen(data) != path_len)
        errx(1, "NUL in parameters");

    if (data[0] != '/')
        errx(1, "Parameters not an absolute path");

    unsigned int writable;

    {
        APPEND_TO_XENSTORE_PATH(extra_path, "mode");
        unsigned int len;
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
    // Both major and minor are 8 hex digits, while colon and NUL
    // terminator are one byte each.
    char phys_dev[18];
    char hex_diskseq[17];

    process_blk_dev(fd, data, writable, &dev, &diskseq, autoclear);
    int const physdev_len =
        snprintf(phys_dev, sizeof phys_dev, "%lx:%lx",
                 (unsigned long)major(dev), (unsigned long)minor(dev));
    if (physdev_len < 3 || physdev_len >= (int)sizeof(phys_dev))
        err(1, "snprintf");

    int const diskseq_len =
        snprintf(hex_diskseq, sizeof(hex_diskseq), "%llx",
                 (unsigned long long)diskseq);
    if (diskseq_len < 1 || diskseq_len >= (int)sizeof(hex_diskseq))
        err(1, "snprintf");

    if (autoclear) {
        APPEND_TO_XENSTORE_PATH(extra_path, OPENED_KEY);
        if (!xs_watch(h, xenstore_path_buffer, OPENED_KEY))
            err(1, "Cannot setup XenStore watch on %s", xenstore_path_buffer);
    }
    char buf[sizeof("/dev/loop") + 10];
    char *physdev_path = data;
    if (major(dev) == LOOP_MAJOR) {
        path_len = (unsigned)snprintf(buf, sizeof buf, "/dev/loop%" PRIu32,
                                      (unsigned)minor(dev));
        if (path_len >= sizeof buf)
            abort();
        physdev_path = buf;
    }

    for (;;) {
        xs_transaction_t t = xs_transaction_start(h);
        if (!t)
            err(1, "Cannot start XenStore transaction");

        APPEND_TO_XENSTORE_PATH(extra_path, "physical-device");
        if (!xs_write(h, t, xenstore_path_buffer, phys_dev, physdev_len))
            err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, phys_dev);

        APPEND_TO_XENSTORE_PATH(extra_path, "physical-device-path");
        if (!xs_write(h, t, xenstore_path_buffer, physdev_path, path_len))
            err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, physdev_path);

        {
            char hex_diskseq[17];
            int diskseq_len =
                snprintf(hex_diskseq, sizeof(hex_diskseq), "%llx",
                         (unsigned long long)diskseq);
            if (diskseq_len < 1 || diskseq_len > 16)
                err(1, "snprintf");
            APPEND_TO_XENSTORE_PATH(extra_path, "diskseq");
            if (!xs_write(h, t, xenstore_path_buffer, hex_diskseq, diskseq_len))
                err(1, "xs_write(\"%s\", \"%s\")", xenstore_path_buffer, hex_diskseq);
        }

        if (xs_transaction_end(h, t, false))
            break;

        if (errno != EAGAIN)
            err(1, "xs_transaction_end");
    }

    if (autoclear) {
        APPEND_TO_XENSTORE_PATH(extra_path, OPENED_KEY);
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
    free(xenstore_path_buffer);
}
