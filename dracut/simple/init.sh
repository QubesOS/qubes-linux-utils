#!/bin/sh
echo "Qubes initramfs script here:"

mkdir -p /proc /sys /dev
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

if [ -w /sys/devices/system/xen_memory/xen_memory0/scrub_pages ]; then
    # re-enable xen-balloon pages scrubbing, after initial balloon down
    echo 1 > /sys/devices/system/xen_memory/xen_memory0/scrub_pages
fi

# If device-mapper is built-in then Linux creates /dev/mapper,
# but if it is a module then this script must do that.
if [ ! -d /dev/mapper ]; then
    mkdir -m 0755 /dev/mapper
elif [ -e /dev/mapper/dmroot ]; then
    echo "Qubes: FATAL error: /dev/mapper/dmroot already exists?!" >&2
fi

/sbin/modprobe xenblk || /sbin/modprobe xen-blkfront || echo "Qubes: Cannot load Xen Block Frontend..."

die() {
    echo "$@" >&2
    exit 1
}

echo "Waiting for /dev/xvda* devices..."
while ! [ -e /dev/xvda ]; do sleep 0.1; done
# Fix up partition tables
if /usr/sbin/gptfix fix /dev/xvda; then
    while ! [ -e /dev/xvda1 ]; do sleep 0.01; done
    ROOT_DEV=$(grep -l "PARTNAME=Root filesystem" /sys/block/xvda/xvda*/uevent 2>/dev/null | sort -V | tail -n 1)
    if [ -n "$ROOT_DEV" ]; then
        ROOT_DEV=${ROOT_DEV%/uevent}
        ROOT_DEV=${ROOT_DEV##*/}
    else
        # fallback to third partition
        ROOT_DEV=xvda3
    fi
    while ! [ -b "/dev/$ROOT_DEV" ]; do sleep 0.01; done
else
    case $? in
    (1|2) # EIO, ENOMEM, or bug.  Fatal.
        die 'Fatal error reading partition table';;
    (4|5|8) # Bad or no partition table
        ROOT_DEV=xvda;;
    (*)
        # TODO: what should be done?
        # - "Partition table not supported"
        # - "Disk truncated"
        die 'GPT cannot be fixed or disk truncated';;
    esac
fi

SWAP_SIZE_GiB=1
SWAP_SIZE_512B=$(( SWAP_SIZE_GiB * 1024 * 1024 * 2 ))

if [ `cat /sys/class/block/$ROOT_DEV/ro` = 1 ] ; then
    echo "Qubes: Doing COW setup for AppVM..."

    while ! [ -e /dev/xvdc ]; do sleep 0.1; done
    VOLATILE_SIZE_512B=$(cat /sys/class/block/xvdc/size)
    if [ $VOLATILE_SIZE_512B -lt $SWAP_SIZE_512B ]; then
        die "volatile.img smaller than $SWAP_SIZE_GiB GiB, cannot continue"
    fi
    /sbin/sfdisk -q /dev/xvdc >/dev/null <<EOF
xvdc1: type=82,start=1MiB,size=${SWAP_SIZE_GiB}GiB
xvdc2: type=83
EOF
    if [ $? -ne 0 ]; then
        echo "Qubes: failed to setup partitions on volatile device"
        exit 1
    fi
    while ! [ -e /dev/xvdc1 ]; do sleep 0.1; done
    /sbin/mkswap /dev/xvdc1
    while ! [ -e /dev/xvdc2 ]; do sleep 0.1; done

    echo "0 `cat /sys/class/block/$ROOT_DEV/size` snapshot /dev/$ROOT_DEV /dev/xvdc2 N 16" | \
        /sbin/dmsetup create dmroot || { echo "Qubes: FATAL: cannot create dmroot!"; exit 1; }
    /sbin/dmsetup mknodes dmroot
    echo Qubes: done.
else
    echo "Qubes: Doing R/W setup for TemplateVM..."
    while ! [ -e /dev/xvdc ]; do sleep 0.1; done
    /sbin/sfdisk -q /dev/xvdc >/dev/null <<EOF
xvdc1: type=82,start=1MiB,size=${SWAP_SIZE_GiB}GiB
xvdc3: type=83
EOF
    if [ $? -ne 0 ]; then
        die "Qubes: failed to setup partitions on volatile device"
    fi
    while ! [ -e /dev/xvdc1 ]; do sleep 0.1; done
    /sbin/mkswap /dev/xvdc1
    ln -sf "../$ROOT_DEV" /dev/mapper/dmroot
    # Keep the mapping for the udev instance of the booted system: /run (unlike
    # the rest of the initramfs) is moved over by switch_root.
    mkdir -p /run
    grep -q ' /run ' /proc/mounts || mount -t tmpfs -o mode=0755 tmpfs /run
    mkdir -p /run/udev/rules.d
    printf 'KERNEL=="%s", SYMLINK+="mapper/dmroot"\n' "$ROOT_DEV" > \
        /run/udev/rules.d/99-qubes-root.rules
    echo Qubes: done.
fi

/sbin/modprobe ext4

mkdir -p /sysroot
mount /dev/mapper/dmroot /sysroot -o rw
NEWROOT=/sysroot

kver="`uname -r`"
if ! [ -d "$NEWROOT/lib/modules/$kver/kernel" ]; then
    echo "Waiting for /dev/xvdd device..."
    while ! [ -e /dev/xvdd ]; do sleep 0.1; done

    mkdir -p /tmp/modules
    mount -n -t ext3 /dev/xvdd /tmp/modules
    if /sbin/modprobe overlay; then
        # if overlayfs is supported, use that to provide fully writable /lib/modules
        if ! [ -d "$NEWROOT/lib/.modules_work" ]; then
            mkdir -p "$NEWROOT/lib/.modules_work"
        fi
        mount -t overlay none "$NEWROOT/lib/modules" -o "lowerdir=/tmp/modules,upperdir=$NEWROOT/lib/modules,workdir=$NEWROOT/lib/.modules_work"
    else
        # otherwise mount only `uname -r` subdirectory, to leave the rest of
        # /lib/modules writable
        if ! [ -d "$NEWROOT/lib/modules/$kver" ]; then
            mkdir -p "$NEWROOT/lib/modules/$kver"
        fi
        mount --bind "/tmp/modules/$kver" "$NEWROOT/lib/modules/$kver"
    fi
    umount /tmp/modules
    rmdir /tmp/modules
fi

umount /dev /sys /proc
mount "$NEWROOT" -o remount,ro

exec /sbin/switch_root $NEWROOT /sbin/init
