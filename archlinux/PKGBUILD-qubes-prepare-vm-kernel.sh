#!/bin/sh
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2015  Marek Marczykowski-Górecki
#                       <marmarekp@invisiblethingslab.com>
#
# Copyright © 2016 Sébastien Luttringer
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
#

set -e

basedir=/var/lib/qubes/vm-kernels

function build_modules_img() {
    kver=$1
    output_file=$2

    mkdir /tmp/qubes-modules-$kver
    truncate -s 400M /tmp/qubes-modules-$kver.img
    mkfs -t ext3 -F /tmp/qubes-modules-$kver.img > /dev/null
    mount /tmp/qubes-modules-$kver.img /tmp/qubes-modules-$kver -o loop
    cp -a -t /tmp/qubes-modules-$kver /lib/modules/$kver
    umount /tmp/qubes-modules-$kver
    rmdir /tmp/qubes-modules-$kver
    mv /tmp/qubes-modules-$kver.img $output_file
}

function build_initramfs() {
    kver=$1
    output_file=$2

    /sbin/dracut --nomdadmconf --nolvmconf --force \
        --modules "kernel-modules qubes-vm-simple" \
        --conf /dev/null --confdir /var/empty \
        -d "xenblk xen-blkfront cdrom ext4 jbd2 crc16 dm_snapshot" \
        $output_file $kver
    chmod 644 "$output_file"
}

function build_initcpio() {
    kver=$1
    output_file=$2
    echo $output_file
    config_file=/etc/mkinitcpio-qubes.conf

    echo "--> Building initcpio configuration file"
    sed 's/^HOOKS="base/HOOKS="lvm2 qubes base/' "/etc/mkinitcpio.conf" > "$config_file"

    mkinitcpio --config "$config_file" -k "$kver" -g "$output_file"
    
    chmod 644 "$output_file"

    echo "--> Copy built initramfs to /boot"
    cp "$output_file" /boot/
}

do_prepare_xen_kernel() {

	kernel_version="$1"
	kernel_base="$2"
	kernel_code="$3"
	output_dir="$basedir/$kernel_version"
	echo "--> Building files for $kernel_version in $output_dir"

	mkdir -p "$output_dir"
	cp "/boot/vmlinuz-linux-$kernel_code" "$output_dir/vmlinuz-linux-$kernel_code"
	echo "---> Generating modules.img"
	build_modules_img "$kernel_version" "$output_dir/modules.img"
	echo "---> Generating initramfs"
	build_initcpio "$kernel_version" "$output_dir/initramfs-$kernel_code.img"

	echo "--> Done."

}

# display what to run and run it quietly
run() {
	echo "==> $*"
	"$@" > /dev/null
}

# check kernel is valid for action
# it means kernel and its headers are installed
# $1: kernel version
check_kernel() {
	local kver="$1"; shift
	echo "Install tree: $install_tree/$kver/kernel"
	if [[ ! -d "$install_tree/$kver/kernel" ]]; then
		echo "==> No kernel $kver modules. You must install them to use DKMS!"
		return 1
	elif [[ ! -d "$install_tree/$kver/build/include" ]]; then
		echo "==> No kernel $kver headers. You must install them to use DKMS!"
		return 1
	fi
	return 0
}

# handle actions on kernel addition/upgrade/removal
# $1: kernel version
# $*: dkms args
do_kernel() {
	local kver="$1"; shift
	check_kernel "$kver" || return
	# do $@ once for each dkms module in $source_tree
	local path
	for path in "$install_tree"/"$kver"/extra/u2mfn.ko; do
		echo "Preparing kernel for $path"
		if [[ "$path" =~ ^$install_tree/([^/]+)-([^/]+)/extra/u2mfn\.ko$ ]]; then
			do_prepare_xen_kernel "$kver" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
		fi
	done
}

# emulated program entry point
main() {

	# prevent to have all each dkms call to fail
	if (( EUID )); then
		echo 'You must be root to use this hook' >&2
		exit 1
	fi

	# dkms path from framework config
	# note: the alpm hooks which trigger this script use static path
	source_tree='/usr/src'
	install_tree='/usr/lib/modules'

	# check source_tree and install_tree exists
	local path
	for path in "$source_tree" "$install_tree"; do
		if [[ ! -d "$path" ]]; then
			echo "==> Missing mandatory directory: $path. Exiting!"
			return 1
		fi
	done

	if [ -n "$1" ] ; then
			echo $install_tree
			if [[ "$1" =~ ^$install_tree/([^/]+)/ ]]; then
				do_kernel "${BASH_REMATCH[1]}"
			fi
	else
		# parse stdin paths to guess what do do
		while read -r path; do
			if [[ "/$path" =~ ^$install_tree/([^/]+)/ ]]; then
				do_kernel "${BASH_REMATCH[1]}"
			fi
		done
	fi

	return 0
}

main "$@"
