#!/bin/sh
#
# This file should be places in pre-mount directory in dracut's initramfs
#

echo "Qubes initramfs script here:"

if [ -e /dev/mapper/dmroot ] ; then 
    die "Qubes: FATAL error: /dev/mapper/dmroot already exists?!"
fi

modprobe xenblk || modprobe xen-blkfront || echo "Qubes: Cannot load Xen Block Frontend..."

echo "Waiting for /dev/xvda* devices..."
while ! [ -e /dev/xvda ]; do sleep 0.1; done

if [ `cat /sys/block/xvda/ro` = 1 ] ; then
    echo "Qubes: Doing COW setup for AppVM..."

    while ! [ -e /dev/xvdc ]; do sleep 0.1; done
    VOLATILE_SIZE=$(sfdisk -s /dev/xvdc)
    ROOT_SIZE=$(sfdisk -s /dev/xvda) # kbytes
    SWAP_SIZE=1024 # kbytes
    if [ $VOLATILE_SIZE -lt $(($ROOT_SIZE + $SWAP_SIZE)) ]; then
        ROOT_SIZE=$(($VOLATILE_SIZE - $SWAP_SIZE))
    fi
    sfdisk -q --unit B /dev/xvdc >/dev/null <<EOF
0,$SWAP_SIZE,S
,$ROOT_SIZE,L
EOF
    if [ $? -ne 0 ]; then
        echo "Qubes: failed to setup partitions on volatile device"
        exit 1
    fi
    while ! [ -e /dev/xvdc1 ]; do sleep 0.1; done
    mkswap /dev/xvdc1
    while ! [ -e /dev/xvdc2 ]; do sleep 0.1; done

    echo "0 `cat /sys/block/xvda/size` snapshot /dev/xvda /dev/xvdc2 N 16" | \
        dmsetup create dmroot || { echo "Qubes: FATAL: cannot create dmroot!"; }
    echo Qubes: done.
else
    echo "Qubes: Doing R/W setup for TemplateVM..."
    echo "0 `cat /sys/block/xvda/size` linear /dev/xvda 0" | \
        dmsetup create dmroot || { echo "Qubes: FATAL: cannot create dmroot!"; exit 1; }
    echo Qubes: done.
fi
dmsetup mknodes dmroot
