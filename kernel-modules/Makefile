MAKEFLAGS ::= -rR
LDLIBS += $(shell pkg-config --libs ext2fs com_err)
CFLAGS += $(shell pkg-config --cflags ext2fs com_err) \
    -fstack-protector-all -O2 -fwrapv \
    -fno-delete-null-pointer-checks -fno-strict-aliasing -Wall -Wextra \
    -Werror=format-security -pedantic-errors -Wformat=2 -Wmaybe-uninitialized \
    -Wshadow -g3

all: genfs

genfs: genfs.c
	gcc $(CFLAGS) genfs.c $(LDLIBS) -o $@

install: install-scripts install-genfs

install-scripts:
	install -d $(DESTDIR)/usr/sbin
	install qubes-prepare-vm-kernel $(DESTDIR)/usr/sbin

install-genfs:
	install -d $(DESTDIR)/usr/lib/qubes
	install genfs $(DESTDIR)/usr/lib/qubes/vm-modules-genfs

.PHONY: clean
clean:
	rm -f genfs
