#!/bin/bash

check() {
    if [ -r /usr/share/qubes/marker-vm ]; then
        return 0
    else
        return 255
    fi
}

depends() {
    return 0
}

install() {
    inst_hook pre-trigger 60 $moddir/scrub_pages.sh
}
