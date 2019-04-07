CC=gcc
CFLAGS+=-I. -g -O2 -Wall -Wextra -Werror -pie -fPIC
SO_VER=2
LDFLAGS+=-shared

all: libqubes-rpc-filecopy.so.$(SO_VER)
libqubes-rpc-filecopy.so.$(SO_VER): ioall.o copy-file.o crc32.o unpack.o pack.o
	$(CC) $(LDFLAGS) -Wl,-soname,$@ -o $@ $^

%.a:
	$(AR) rcs $@ $^
clean:
	rm -f *.o *~ *.a *.so.*

install:
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)
	ln -s libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)/libqubes-rpc-filecopy.so
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp libqubes-rpc-filecopy.h $(DESTDIR)$(INCLUDEDIR)
