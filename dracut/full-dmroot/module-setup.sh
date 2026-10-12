#!/bin/bash

check() {
    if [ -f /usr/share/qubes/marker-vm ]; then
        return 0
    else
        return 255
    fi
}

depends() {
    echo dm
    return 0
}

install() {
    inst_hook pre-trigger 90 $moddir/qubes_cow_setup.sh
    inst_multiple \
        grep \
        sort \
        tail \
        sfdisk \
        swapon \
        mkswap \
        gptfix
}
