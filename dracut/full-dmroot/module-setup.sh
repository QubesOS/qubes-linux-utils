#!/bin/bash

check() {
    if xenstore-read qubes-vm-type &>/dev/null || qubesdb-read qubes-vm-type &>/dev/null; then
        return 0
    else
        return 255
    fi
}

install() {
    inst_hook pre-udev 90 $moddir/qubes_cow_setup.sh
    inst_multiple \
        sfdisk \
        mkswap
}
