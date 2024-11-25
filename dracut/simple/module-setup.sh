#!/bin/bash

check() {
    return 255
}

depends() {
    echo dm
    return 0
}

installkernel() {
    hostonly='' instmods overlay
}

install() {
    inst $moddir/init.sh /init
    inst_multiple \
        readlink \
        uname \
        grep \
        kmod \
        modprobe \
        ln \
        switch_root \
        mount \
        umount \
        mkdir \
        rmdir \
        sleep \
        sfdisk \
        mkswap \
        gptfix
}
