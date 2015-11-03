CC=gcc
CFLAGS+=-I. -g -O2 -Wall -Wextra -Werror -pie -fPIC `pkg-config --cflags vchan-$(BACKEND_VMM)`
COMMONIOALL=ioall.o
SO_VER=2
LDFLAGS=-shared
VCHANLIBS = `pkg-config --libs vchan-$(BACKEND_VMM)`

_XENSTORE_H=$(shell ls /usr/include/xenstore.h)
ifneq "$(_XENSTORE_H)" ""
	CFLAGS+= -DUSE_XENSTORE_H
endif


all: libqrexec-utils.so.$(SO_VER) libqubes-rpc-filecopy.so.$(SO_VER)
libqrexec-utils.so.$(SO_VER): unix-server.o ioall.o buffer.o exec.o txrx-vchan.o write-stdin.o
	$(CC) $(LDFLAGS) -Wl,-soname,$@ -o $@ $^ $(VCHANLIBS)
libqubes-rpc-filecopy.so.$(SO_VER): ioall.o copy-file.o crc32.o unpack.o pack.o
	$(CC) $(LDFLAGS) -Wl,-soname,$@ -o $@ $^

%.a:
	$(AR) rcs $@ $^
clean:
	rm -f *.o *~ *.a *.so.*

install:
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp libqrexec-utils.so.$(SO_VER) $(DESTDIR)$(LIBDIR)
	ln -s libqrexec-utils.so.$(SO_VER) $(DESTDIR)$(LIBDIR)/libqrexec-utils.so
	cp libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)
	ln -s libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)/libqubes-rpc-filecopy.so
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp libqrexec-utils.h $(DESTDIR)$(INCLUDEDIR)
	cp libqubes-rpc-filecopy.h $(DESTDIR)$(INCLUDEDIR)
	cp qrexec.h $(DESTDIR)$(INCLUDEDIR)

