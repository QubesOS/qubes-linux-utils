SBINDIR ?= /usr/sbin
LIBDIR ?= /usr/lib64
SCRIPTSDIR ?= /usr/lib/qubes
SYSLIBDIR ?= /usr/lib
INCLUDEDIR ?= /usr/include
CFLAGS ?= -Wall -Wextra -Werror -O3 -g3 -Werror=format=2
CC ?= gcc

export LIBDIR SCRIPTSDIR SYSLIBDIR INCLUDEDIR
.PHONY: all selinux install install-selinux install-fedora-kernel-support install-debian-kernel-support clean

all:
	$(MAKE) -C qrexec-lib all
	$(MAKE) -C qmemman all
	$(MAKE) -C imgconverter all
	$(MAKE) -C not-script all
selinux:
	$(MAKE) -f /usr/share/selinux/devel/Makefile -C selinux qubes-meminfo-writer.pp

install:
	$(MAKE) -C udev install
	$(MAKE) -C qrexec-lib install
	$(MAKE) -C qmemman install
	$(MAKE) -C imgconverter install
	$(MAKE) -C not-script install

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

install-gptfix: gptfixer/gpt
	install -D gptfixer/gpt $(DESTDIR)$(SBINDIR)/gptfix
gptfixer/gpt_LDLIBS := -lz
gptfixer/gpt_CFLAGS := -D_GNU_SOURCE -fno-strict-aliasing -fno-delete-null-pointer-checks -fno-strict-overflow
%: %.c Makefile
	$(CC) $($(@)_CFLAGS) -o $@ $< $(CFLAGS) -MD -MP -MF $@.dep $($(@)_LDLIBS)
-include gptfixer/*.dep

clean:
	$(MAKE) -C qrexec-lib clean
	$(MAKE) -C qmemman clean
	$(MAKE) -C imgconverter clean
	$(MAKE) -C not-script clean
	rm -rf selinux/*.pp selinux/tmp/
	rm -rf debian/changelog.*
	rm -rf pkgs
