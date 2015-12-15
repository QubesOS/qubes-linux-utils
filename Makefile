ifeq ($(shell uname -m),x86_64)
LIBDIR ?= /usr/lib64
else
LIBDIR ?= /usr/lib
endif
SYSLIBDIR ?= /lib
INCLUDEDIR ?= /usr/include

export LIBDIR INCLUDEDIR

help:
	echo "Use rpmbuild to compile this pacakge"
	exit 0


rpms:
	rpmbuild --define "_rpmdir rpm/" --define "_builddir ." -bb rpm_spec/qubes-utils.spec
all:
	$(MAKE) -C qrexec-lib all
	$(MAKE) -C qmemman all
	$(MAKE) -C core all

install:
	$(MAKE) -C udev install
	$(MAKE) -C qrexec-lib install
	$(MAKE) -C qmemman install
	$(MAKE) -C core install

install-fedora-kernel-support:
	$(MAKE) -C dracut install
	$(MAKE) -C kernel-modules install

install-debian-kernel-support:
	$(MAKE) -C initramfs-tools install
	$(MAKE) -C kernel-modules install
	# expand module version
	rm -f debian/qubes-kernel-vm-support.dkms
	echo debian/tmp/usr/src/u2mfn-*/dkms.conf > debian/qubes-kernel-vm-support.dkms

clean:
	$(MAKE) -C qrexec-lib clean
	$(MAKE) -C qmemman clean
	$(MAKE) -C core clean
