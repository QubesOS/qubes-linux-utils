all:

install:
	mkdir -p $(DESTDIR)$(SYSLIBDIR)/udev/rules.d
	cp udev-qubes-block.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/99-qubes-block.rules
	cp udev-qubes-usb.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/99-qubes-usb.rules
	cp udev-qubes-misc.rules $(DESTDIR)$(SYSLIBDIR)/udev/rules.d/99-qubes-misc.rules

	mkdir -p $(DESTDIR)$(LIBDIR)/qubes
	cp udev-block-add-change $(DESTDIR)$(LIBDIR)/qubes/
	cp udev-block-remove $(DESTDIR)$(LIBDIR)/qubes/
	cp udev-block-cleanup $(DESTDIR)$(LIBDIR)/qubes/
	cp udev-usb-add-change $(DESTDIR)$(LIBDIR)/qubes/
	cp udev-usb-remove $(DESTDIR)$(LIBDIR)/qubes/
