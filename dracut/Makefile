install:
	$(MAKE) -C simple
	$(MAKE) -C full-dmroot
	$(MAKE) -C full-modules
ifeq ($(BACKEND_VMM),xen)
	$(MAKE) -C xen-balloon-scrub-pages
endif
