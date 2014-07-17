ver		:= $(shell cat ../version)
srcdir	:= /usr/src/u2mfn-$(ver)

install:	install-u2mfn

install-u2mfn:
	install -d $(DESTDIR)$(srcdir)
	install -m 644 u2mfn/u2mfn.c u2mfn/Makefile $(DESTDIR)$(srcdir)
	sed -e 's#@@VERSION@@#$(ver)#' u2mfn/dkms.conf.in > $(DESTDIR)$(srcdir)/dkms.conf
