CC=gcc
CFLAGS=-Wall -Wextra -Werror -g -O3
all:	meminfo-writer
meminfo-writer: meminfo-writer.o
	$(CC) -g -o meminfo-writer meminfo-writer.o -lxenstore
install:
	install -D meminfo-writer $(DESTDIR)/usr/sbin/meminfo-writer
	install -d $(DESTDIR)/usr/lib/systemd/system/
	install qubes-meminfo-writer*service $(DESTDIR)/usr/lib/systemd/system/
clean:
	rm -f meminfo-writer xenstore-watch *.o
