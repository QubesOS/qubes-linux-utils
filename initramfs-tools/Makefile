install:
	install -D local-top/qubes_cow_setup.sh \
		$(DESTDIR)/usr/share/initramfs-tools/scripts/local-top/qubes_cow_setup
	install -D local-top/scrub_pages.sh \
		$(DESTDIR)/usr/share/initramfs-tools/scripts/local-top/scrub_pages
	install -D qubes_vm \
		$(DESTDIR)/usr/share/initramfs-tools/hooks/qubes_vm
	install -m 0644 -D qubes.conf \
		$(DESTDIR)/usr/share/initramfs-tools/conf.d/qubes.conf
	
