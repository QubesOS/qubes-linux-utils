#!/bin/sh
#
# This file should be placed in pre-trigger directory in dracut's initramfs, or
# scripts/local-top in case of initramfs-tools
#

# initramfs-tools (Debian) API
PREREQS=""
case "$1" in
    prereqs)
        # This runs during initramfs creation
        echo "$PREREQS"
        exit 0
        ;;
esac

# This runs inside real initramfs
if [ -r /scripts/functions ]; then
    # We're running in Debian's initramfs
    . /scripts/functions
    alias die=panic
    alias info=true
    alias warn=log_warning_msg
    alias log_begin=log_begin_msg
    alias log_end=log_end_msg
elif [ -r /lib/dracut-lib.sh ]; then
    . /lib/dracut-lib.sh
    alias log_begin=info
    alias log_end=true
else
    die() {
        echo "$@"
        exit 1
    }
    alias info=echo
    alias warn=echo
    alias log_begin=echo
    alias log_end=true
fi


info "Qubes initramfs script here:"

if ! grep -q 'root=[^ ]*dmroot' /proc/cmdline; then
    warn "dmroot not requested, probably not a Qubes VM"
    exit 0
fi

if [ -e /dev/mapper/dmroot ] ; then 
    die "Qubes: FATAL error: /dev/mapper/dmroot already exists?!"
fi

modprobe xenblk || modprobe xen-blkfront || warn "Qubes: Cannot load Xen Block Frontend..."

log_begin "Waiting for /dev/xvda* devices..."
udevadm settle --exit-if-exists=/dev/xvda
log_end

# prefer partition if exists
if [ -b /dev/xvda1 ]; then
    if [ -e "/dev/disk/by-partlabel/Root\\x20filesystem" ]; then
        ROOT_DEV=$(readlink "/dev/disk/by-partlabel/Root\\x20filesystem")
        ROOT_DEV=${ROOT_DEV##*/}
    else
        ROOT_DEV=xvda3
    fi
else
    ROOT_DEV=xvda
fi

SWAP_SIZE_GiB=1
SWAP_SIZE_512B=$(( SWAP_SIZE_GiB * 1024 * 1024 * 2 ))

if [ `cat /sys/class/block/$ROOT_DEV/ro` = 1 ] ; then
    log_begin "Qubes: Doing COW setup for AppVM..."

    while ! [ -e /dev/xvdc ]; do sleep 0.1; done
    VOLATILE_SIZE_512B=$(cat /sys/class/block/xvdc/size)
    if [ $VOLATILE_SIZE_512B -lt $SWAP_SIZE_512B ]; then
        die "volatile.img smaller than $SWAP_SIZE_GiB GiB, cannot continue"
    fi
    sfdisk -q /dev/xvdc >/dev/null <<EOF
xvdc1: type=82,start=1MiB,size=${SWAP_SIZE_GiB}GiB
xvdc2: type=83
EOF
    if [ $? -ne 0 ]; then
        die "Qubes: failed to setup partitions on volatile device"
    fi
    while ! [ -e /dev/xvdc1 ]; do sleep 0.1; done
    mkswap /dev/xvdc1
    # enable swap right now, because systemd may want to run fsck in initramfs
    # already and it require some more memory
    swapon /dev/xvdc1
    while ! [ -e /dev/xvdc2 ]; do sleep 0.1; done

    echo "0 `cat /sys/class/block/$ROOT_DEV/size` snapshot /dev/$ROOT_DEV /dev/xvdc2 N 16" | \
        dmsetup --noudevsync create dmroot || die "Qubes: FATAL: cannot create dmroot!"
    dmsetup mknodes dmroot
    log_end
else
    log_begin "Qubes: Doing R/W setup for TemplateVM..."
    while ! [ -e /dev/xvdc ]; do sleep 0.1; done
    sfdisk -q /dev/xvdc >/dev/null <<EOF
xvdc1: type=82,start=1MiB,size=${SWAP_SIZE_GiB}GiB
xvdc3: type=83
EOF
    if [ $? -ne 0 ]; then
        die "Qubes: failed to setup partitions on volatile device"
    fi
    while ! [ -e /dev/xvdc1 ]; do sleep 0.1; done
    mkswap /dev/xvdc1
    # enable swap right now, because systemd may want to run fsck in initramfs
    # already and it require some more memory
    swapon /dev/xvdc1
    mkdir -p /etc/udev/rules.d
    printf 'KERNEL=="%s", SYMLINK+="mapper/dmroot"\n' "$ROOT_DEV" >> \
        /etc/udev/rules.d/99-root.rules
    udevadm control -R
    udevadm trigger
    log_end
fi
