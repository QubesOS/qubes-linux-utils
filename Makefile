LIBDIR ?= /usr/lib64
SCRIPTSDIR ?= /usr/lib/qubes
SYSLIBDIR ?= /usr/lib
INCLUDEDIR ?= /usr/include

export LIBDIR SCRIPTSDIR SYSLIBDIR INCLUDEDIR

all:
	$(MAKE) -C qrexec-lib all
	$(MAKE) -C qmemman all
	$(MAKE) -C imgconverter all

install:
	$(MAKE) -C udev install
	$(MAKE) -C qrexec-lib install
	$(MAKE) -C qmemman install
	$(MAKE) -C imgconverter install

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
	rm -rf debian/changelog.*
	rm -rf pkgs
