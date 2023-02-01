CC=gcc
CFLAGS := $(CFLAGS) -I. -g3 -O2 -Wall -Wextra -Werror -pie -fPIC $(shell pkg-config --cflags icu-uc)
SO_VER=2
LDFLAGS+=-Wl,--no-undefined,--as-needed
pure_libs := $(shell pkg-config --libs icu-uc)
.PHONY: all clean install check
objs := ioall.o copy-file.o crc32.o unpack.o pack.o

pure_lib := libqubes-pure.so
pure_sover := 0
pure_objs := unicode.o

all: libqubes-rpc-filecopy.so.$(SO_VER) $(pure_lib).$(pure_sover)
libqubes-rpc-filecopy.so.$(SO_VER): $(objs) ./$(pure_lib).$(pure_sover)
	$(CC) -shared $(LDFLAGS) -Wl,-soname,$@ -o $@ $^
validator-test: validator-test.o ./$(pure_lib).$(pure_sover)
	$(CC) $(LDFLAGS) -o $@ $^ ./$(pure_lib).$(pure_sover)
$(pure_objs): CFLAGS += -fvisibility=hidden -DQUBES_PURE_IMPLEMENTATION
validator-test: CFLAGS += -UNDEBUG
check: validator-test
	LD_LIBRARY_PATH=. ./validator-test

$(pure_lib).$(pure_sover): $(pure_objs)
	$(CC) -shared $(LDFLAGS) -Wl,-Bsymbolic,-soname,$@ -o $@ $^ $(pure_libs)
$(pure_lib):
	ln -s $(pure_lib).$(pure_sover) $(pure_lib)

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
