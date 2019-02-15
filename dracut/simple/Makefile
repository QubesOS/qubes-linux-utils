install:
	install -d $(DESTDIR)/usr/lib/dracut/modules.d/90qubes-vm-simple
	install module-setup.sh init.sh \
		$(DESTDIR)/usr/lib/dracut/modules.d/90qubes-vm-simple/
	# flag indicating the module will (re-)enable scrub-pages option
	touch $(DESTDIR)/usr/lib/dracut/modules.d/90qubes-vm-simple/xen-scrub-pages-supported
