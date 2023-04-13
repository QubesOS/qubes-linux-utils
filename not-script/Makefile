CC=gcc
CFLAGS := $(CFLAGS) -g3 -O2 -Wall -Wextra -Werror -fPIE -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE -Wmaybe-uninitialized
.PHONY: clean install all
all: not-script

%: %.c Makefile
	 s=$$(pkg-config --cflags --libs xenstore) && $(CC) $(CFLAGS) -MD -MP -MF $@.dep -o $@ $< $$s

clean:
	rm -f ./*.o ./*~ ./*.a ./*.so.* ./*.dep not-script

install:
	install -d $(DESTDIR)/etc/xen/scripts
	install not-script $(DESTDIR)/etc/xen/scripts/qubes-block

-include ./*.o.dep
