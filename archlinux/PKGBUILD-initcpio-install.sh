#!/usr/bin/bash

build() {

  add_module "xen-blkfront"
  add_binary "/usr/bin/sfdisk"
  add_binary "/usr/bin/mkswap"
  add_binary "/usr/bin/swapon"
  add_binary "/usr/bin/dmsetup"
  add_binary "/usr/bin/gptfix"
  add_binary "/usr/lib/qubes/scrub_pages.sh"
  add_binary "/usr/lib/qubes/qubes_cow_setup.sh"

  map add_module \
    'dm-mod' \
    'dm-snapshot'
  
  add_runscript

  # Mark it's safe to add scrub_pages=0 to the kernel cmdline now
  echo 1 > /var/lib/qubes/initramfs-updated
}

help() {
  cat <<HELPEOF
This hook enables Qubes COW Setup (using lvm) in initramfs.
HELPEOF
}

