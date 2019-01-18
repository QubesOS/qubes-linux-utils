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

if [ -w /sys/devices/system/xen_memory/xen_memory0/scrub_pages ]; then
    # re-enable xen-balloon pages scrubbing, after initial balloon down
    echo 1 > /sys/devices/system/xen_memory/xen_memory0/scrub_pages
fi
