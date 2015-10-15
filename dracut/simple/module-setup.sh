#!/bin/bash

check() {
    return 255
}

depends() {
    echo busybox dm
    return 0
}

install() {
    inst $moddir/init.sh /init
    inst_multiple \
        sfdisk \
        mkswap
}
