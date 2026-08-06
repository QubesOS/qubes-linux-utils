#!/bin/sh
#
# This file should be places in pre-pivot directory in dracut's initramfs
#


kver="`uname -r`"
if ! [ -d "$NEWROOT/lib/modules/$kver/kernel" ]; then
    echo "Waiting for /dev/xvdd device..."
    while ! [ -e /dev/xvdd ]; do sleep 0.1; done

    # Mount only `uname -r` subdirectory, to leave the rest of /lib/modules writable
    mkdir -p /tmp/modules
    mount -r -n -t ext3 /dev/xvdd /tmp/modules
    if ! [ -d "$NEWROOT/lib/modules/$kver" ]; then
        mount "$NEWROOT" -o remount,rw
        mkdir -p "$NEWROOT/lib/modules/$kver"
        mount "$NEWROOT" -o remount,ro
    fi
    mount --bind "/tmp/modules/$kver" "$NEWROOT/lib/modules/$kver"
    umount /tmp/modules
    rmdir /tmp/modules
fi

killall udevd systemd-udevd
