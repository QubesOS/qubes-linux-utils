CC=gcc
CFLAGS+=-I. -g -Wall -pie -fPIC -Wall
COMMONIOALL=ioall.o

all: libqrexec-utils.a libqubes-rpc-filecopy.a
libqrexec-utils.a: unix-server.o ioall.o buffer.o write-stdin.o exec.o txrx-vchan.o
libqubes-rpc-filecopy.a: ioall.o copy-file.o crc32.o unpack.o

%.a:
	$(AR) rcs $@ $^
clean:
	rm -f *.o *~ *.a

install:
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp libqrexec-utils.a $(DESTDIR)$(LIBDIR)
	cp libqubes-rpc-filecopy.a $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp libqrexec-utils.h $(DESTDIR)$(INCLUDEDIR)
	cp libqubes-rpc-filecopy.h $(DESTDIR)$(INCLUDEDIR)
	cp qrexec.h $(DESTDIR)$(INCLUDEDIR)

