#!/bin/bash

check() {
    if xenstore-read qubes-vm-type &>/dev/null || qubesdb-read /qubes-vm-type &>/dev/null; then
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
        sfdisk \
        swapon \
        mkswap
}
