all:

install:
	mkdir -p $(DESTDIR)$(SYSLIBDIR)/udev/rules.d
	cp udev-qubes-block.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/99-qubes-block.rules
	cp udev-qubes-usb.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/99-qubes-usb.rules
	cp udev-qubes-misc.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/99-qubes-misc.rules
	cp udev-qubes-dmroot.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/90-qubes-dmroot.rules

	mkdir -p $(DESTDIR)$(SCRIPTSDIR)
	cp udev-block-add-change $(DESTDIR)$(SCRIPTSDIR)
	cp udev-block-remove $(DESTDIR)$(SCRIPTSDIR)
	cp udev-usb-add-change $(DESTDIR)$(SCRIPTSDIR)
	cp udev-usb-remove $(DESTDIR)$(SCRIPTSDIR)
