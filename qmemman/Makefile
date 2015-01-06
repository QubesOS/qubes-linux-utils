CC=gcc
CFLAGS=-Wall -Wextra -Werror -g -O3
SBINDIR ?= /usr/sbin

all:	meminfo-writer

_XENSTORE_H=$(shell ls /usr/include/xenstore.h)
ifneq "$(_XENSTORE_H)" ""
	CFLAGS+= -DUSE_XENSTORE_H
endif

meminfo-writer: meminfo-writer.o
	$(CC) -g -o meminfo-writer meminfo-writer.o -lxenstore
install:
	install -D meminfo-writer $(DESTDIR)$(SBINDIR)/meminfo-writer
ifeq (1,${DEBIANBUILD})
	install -d $(DESTDIR)/lib/systemd/system/
	install -m 0644 qubes-meminfo-writer.service $(DESTDIR)/lib/systemd/system/
else
	install -d $(DESTDIR)/usr/lib/systemd/system/
	install qubes-meminfo-writer*service $(DESTDIR)/usr/lib/systemd/system/
endif
clean:
	rm -f meminfo-writer xenstore-watch *.o
