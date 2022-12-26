CC=gcc
CFLAGS+=-I. -g -O2 -Wall -Wextra -Werror -pie -fPIC $(shell pkg-config --cflags icu-uc)
SO_VER=2
LDFLAGS+=-shared
LDLIBS += $(shell pkg-config --libs icu-uc)
.PHONY: all clean install
objs := ioall.o copy-file.o crc32.o unpack.o pack.o

all: libqubes-rpc-filecopy.so.$(SO_VER)
libqubes-rpc-filecopy.so.$(SO_VER): $(objs)
	$(CC) $(LDFLAGS) -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -MD -MP -MF $@.dep -c -o $@ $<
unpack.o: unpack-table.c
unpack-table.c: gentbl.py
	python3 gentbl.py > unpack-table.c

%.a: $(objs)
	$(AR) rcs $@ $^
clean:
	rm -f *.o *~ *.a *.so.*

install:
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)
	ln -s libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)/libqubes-rpc-filecopy.so
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp libqubes-rpc-filecopy.h $(DESTDIR)$(INCLUDEDIR)
-include ./*.o.dep
