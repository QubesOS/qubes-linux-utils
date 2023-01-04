LIBDIR ?= /usr/lib64
SCRIPTSDIR ?= /usr/lib/qubes
SYSLIBDIR ?= /usr/lib
INCLUDEDIR ?= /usr/include

export LIBDIR SCRIPTSDIR SYSLIBDIR INCLUDEDIR
.PHONY: all selinux install install-selinux install-fedora-kernel-support install-debian-kernel-support clean

all:
	$(MAKE) -C qrexec-lib all
	$(MAKE) -C qmemman all
	$(MAKE) -C imgconverter all
selinux:
	$(MAKE) -f /usr/share/selinux/devel/Makefile -C selinux qubes-meminfo-writer.pp

install:
	$(MAKE) -C udev install
	$(MAKE) -C qrexec-lib install
	$(MAKE) -C qmemman install
	$(MAKE) -C imgconverter install

install-selinux:
	install -m 0644 -D -t $(DESTDIR)/usr/share/selinux/packages selinux/qubes-meminfo-writer.pp
	install -m 0644 -D selinux/qubes-meminfo-writer.if $(DESTDIR)/usr/share/selinux/devel/include/contrib/ipp-qubes-meminfo-writer.if

install-fedora-kernel-support:
	$(MAKE) -C dracut install
	$(MAKE) -C kernel-modules install
	$(MAKE) -C grub install-fedora

install-debian-kernel-support:
	$(MAKE) -C initramfs-tools install
	$(MAKE) -C dracut install
	$(MAKE) -C grub install-debian

clean:
	$(MAKE) -C qrexec-lib clean
	$(MAKE) -C qmemman clean
	$(MAKE) -C imgconverter clean
	rm -rf selinux/*.pp selinux/tmp/
	rm -rf debian/changelog.*
	rm -rf pkgs
