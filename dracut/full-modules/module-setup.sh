#!/bin/bash

check() {
    return 255
}

install() {
    inst_hook pre-pivot 50 $moddir/mount_modules.sh
}
