#!/usr/bin/bash

build() {

  add_module "xen-blkfront"
  add_binary "/usr/bin/sfdisk"
  add_binary "/usr/bin/mkswap"
  add_binary "/usr/bin/swapon"
  add_binary "/usr/bin/dmsetup"
  add_binary "/usr/bin/gptfix"
  add_binary "/usr/lib/qubes/qubes_cow_setup.sh"
  
  add_runscript
  
}

help() {
  cat <<HELPEOF
This hook enables Qubes COW Setup (using lvm) in initramfs.
HELPEOF
}

