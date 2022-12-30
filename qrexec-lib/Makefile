CC=gcc
CFLAGS := $(CFLAGS) -I. -g -O2 -Wall -Wextra -Werror -pie -fPIC $(shell pkg-config --cflags icu-uc)
SO_VER=2
LDFLAGS+=-shared -Wl,--no-undefined,--as-needed
pure_libs := $(shell pkg-config --libs icu-uc)
.PHONY: all clean install
objs := ioall.o copy-file.o crc32.o unpack.o pack.o

pure_lib := libqubes-pure.so
pure_sover := 0
pure_objs := unicode.o

all: libqubes-rpc-filecopy.so.$(SO_VER) $(pure_lib).$(pure_sover)
libqubes-rpc-filecopy.so.$(SO_VER): $(objs) ./$(pure_lib).$(pure_sover)
	$(CC) $(LDFLAGS) -Wl,-soname,$@ -o $@ $^ $(LDLIBS)
$(pure_objs): CFLAGS += -fvisibility=hidden -DQUBES_PURE_IMPLEMENTATION

$(pure_lib).$(pure_sover): $(pure_objs)
	$(CC) $(LDFLAGS) -Wl,-Bsymbolic,-soname,$@ -o $@ $^ $(pure_libs)

%.o: %.c Makefile
	$(CC) $(CFLAGS) -MD -MP -MF $@.dep -c -o $@ $<

unicode.o: unicode-class-table.c
unicode-class-table.c: gentbl.py Makefile
	python3 -- $< > $@

%.a: $(objs)
	$(AR) rcs $@ $^
clean:
	rm -f ./*.o ./*~ ./*.a ./*.so.* ./*.dep unicode-class-table.c

install:
	mkdir -p $(DESTDIR)$(LIBDIR)
	install libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)
	ln -sf libqubes-rpc-filecopy.so.$(SO_VER) $(DESTDIR)$(LIBDIR)/libqubes-rpc-filecopy.so
	install $(pure_lib).$(pure_sover) $(DESTDIR)$(LIBDIR)
	ln -sf $(pure_lib).$(pure_sover) $(DESTDIR)$(LIBDIR)/$(pure_lib)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)/qubes
	cp libqubes-rpc-filecopy.h $(DESTDIR)$(INCLUDEDIR)
	cp pure.h $(DESTDIR)$(INCLUDEDIR)/qubes
-include ./*.o.dep
