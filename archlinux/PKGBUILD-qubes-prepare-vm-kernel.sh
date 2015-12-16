 #!/bin/sh
#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2015  Marek Marczykowski-Górecki
#                       <marmarekp@invisiblethingslab.com>
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

function recompile_u2mfn() {
    kver=$1
    u2mfn_ver=`dkms status u2mfn|tail -n 1|cut -f 2 -d ' '|tr -d ':,'`
    if ! modinfo -k "$kver" -n u2mfn 2>&1 > /dev/null; then
        echo "Module u2mfn not available. Checking available source to be built."
        u2mfn_ver=`cat /usr/src/u2mfn-*/dkms.conf | grep PACKAGE_VERSION | cut -d "=" -f 2 | tr -d '"' | sort -u | head -n 1`
        if [ -z "$u2mfn_ver" ] ; then
            echo "No source found for u2mfn. Is qubes-vm-kernel-support installed correctly?"
            return 1
        else
            echo "Found sources for u2mfn version $u2mfn_ver"
        fi

        dkms install u2mfn/$u2mfn_ver -k $kver --no-initrd
    fi
}

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

    mkinitcpio -k "$kver" -g "$output_file" -A qubes,lvm2
    
    chmod 644 "$output_file"
}

if [ -z "$1" ]; then
    echo "Usage: $0 <kernel-version> <kernel-name> [<display-kernel-version>]" >&2
    exit 1
fi

if [ ! -d /lib/modules/$1 ]; then
    echo "ERROR: Kernel version $1 not installed" >&2
    exit 1
fi

kernel_version=$1

if [ -n "$2" ]; then
    kernel_code="-linux-$2"
else
    kernel_code="-linux"
fi

if [ -n "$3" ]; then
    output_dir="$basedir/$3"
else
    output_dir="$basedir/$kernel_version"
fi

echo "--> Building files for $kernel_version in $output_dir"

echo "---> Recompiling kernel module (u2mfn)"
recompile_u2mfn "$kernel_version"
mkdir -p "$output_dir"
cp "/boot/vmlinuz$kernel_code" "$output_dir/vmlinuz$kernel_code"
echo "---> Generating modules.img"
build_modules_img "$kernel_version" "$output_dir/modules.img"
echo "---> Generating initramfs"
build_initcpio "$kernel_version" "$output_dir/initramfs$kernel_code.img"

echo "--> Done."
