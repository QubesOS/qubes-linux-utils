all:

install:
	mkdir -p $(DESTDIR)/etc/udev/rules.d
	cp udev-qubes-block.rules $(DESTDIR)/etc/udev/rules.d/99-qubes-block.rules
	cp udev-qubes-usb.rules $(DESTDIR)/etc/udev/rules.d/99-qubes-usb.rules

	mkdir -p $(DESTDIR)/usr/libexec/qubes
	cp udev-block-add-change $(DESTDIR)/usr/libexec/qubes/
	cp udev-block-remove $(DESTDIR)/usr/libexec/qubes/
	cp udev-block-cleanup $(DESTDIR)/usr/libexec/qubes/
	cp udev-usb-add-change $(DESTDIR)/usr/libexec/qubes/
	cp udev-usb-remove $(DESTDIR)/usr/libexec/qubes/
