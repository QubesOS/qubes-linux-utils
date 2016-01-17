#!/usr/bin/ash

run_earlyhook() {

	msg "Starting Qubes copy on write setup script"

	if ! grep -q 'root=[^ ]*dmroot' /proc/cmdline; then
	    warning "Qubes: dmroot not requested, probably not a Qubes VM"
	    exit 0
	fi

	if [ -e /dev/mapper/dmroot ] ; then
	    die "Qubes: FATAL error: /dev/mapper/dmroot already exists?!"
	fi

	modprobe xen-blkfront || warning "Qubes: Cannot load Xen Block Frontend..."

	msg "Qubes: Waiting for /dev/xvda* devices..."
	while ! [ -e /dev/xvda ]; do sleep 0.1; done
	msg "Qubes: /dev/xvda* found"

	SWAP_SIZE=$(( 1024 * 1024 * 2 )) # sectors, 1GB

	if [ `cat /sys/block/xvda/ro` = 1 ] ; then
	    msg "Qubes: Doing COW setup for AppVM..."

	    while ! [ -e /dev/xvdc ]; do sleep 0.1; done
	    VOLATILE_SIZE=$(cat /sys/block/xvdc/size) # sectors
	    ROOT_SIZE=$(cat /sys/block/xvda/size) # sectors
	    if [ $VOLATILE_SIZE -lt $SWAP_SIZE ]; then
		die "Qubes: volatile.img smaller than 1GB, cannot continue"
	    fi
	sfdisk -q --unit S /dev/xvdc >/dev/null <<EOF
1,$SWAP_SIZE,S
,,L
EOF

	    if [ $? -ne 0 ]; then
		die "Qubes: failed to setup partitions on volatile device"
	    fi
	    while ! [ -e /dev/xvdc1 ]; do sleep 0.1; done
	    mkswap /dev/xvdc1
	    while ! [ -e /dev/xvdc2 ]; do sleep 0.1; done

	    echo "0 `cat /sys/block/xvda/size` snapshot /dev/xvda /dev/xvdc2 N 16" | \
		dmsetup --noudevsync create dmroot || die "Qubes: FATAL: cannot create dmroot!"
	fi

	dmsetup mknodes dmroot
	
}