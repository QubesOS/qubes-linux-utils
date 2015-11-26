all:

install:
	mkdir -p $(DESTDIR)/lib/udev/rules.d
	cp udev-qubes-block.rules $(DESTDIR)/lib/udev/rules.d/99-qubes-block.rules
	cp udev-qubes-usb.rules $(DESTDIR)/lib/udev/rules.d/99-qubes-usb.rules
	cp udev-qubes-misc.rules $(DESTDIR)/lib/udev/rules.d/99-qubes-misc.rules

	mkdir -p $(DESTDIR)/usr/lib/qubes
	cp udev-block-add-change $(DESTDIR)/usr/lib/qubes/
	cp udev-block-remove $(DESTDIR)/usr/lib/qubes/
	cp udev-block-cleanup $(DESTDIR)/usr/lib/qubes/
	cp udev-usb-add-change $(DESTDIR)/usr/lib/qubes/
	cp udev-usb-remove $(DESTDIR)/usr/lib/qubes/
