CC=gcc
CFLAGS := $(CFLAGS) -I. -g3 -O2 -Wall -Wextra -Werror -pie -fPIC
SO_VER=2
LDFLAGS+=-Wl,--no-undefined,--as-needed,-Bsymbolic -L .
.PHONY: all clean install check
objs := ioall.o copy-file.o crc32.o unpack.o pack.o

pure_lib := libqubes-pure.so
pure_sover := 0
pure_objs := unicode.o qube-name.o

all: libqubes-rpc-filecopy.so.$(SO_VER) $(pure_lib).$(pure_sover)
libqubes-rpc-filecopy.so.$(SO_VER): $(objs) ./$(pure_lib).$(pure_sover)
	$(CC) -shared $(LDFLAGS) -Wl,-soname,$@ -o $@ $^
validator-test: validator-test.o ./$(pure_lib).$(pure_sover)
	libs=$$(pkg-config --libs icu-uc) && $(CC) '-Wl,-rpath,$$ORIGIN' $(LDFLAGS) -o $@ $^ $$libs
$(pure_objs): CFLAGS += -fvisibility=hidden -DQUBES_PURE_IMPLEMENTATION
validator-test: CFLAGS += -UNDEBUG
check: validator-test
	LD_LIBRARY_PATH=. ./validator-test

$(pure_lib).$(pure_sover): $(pure_objs)
	$(CC) -shared $(LDFLAGS) -Wl,-Bsymbolic,-soname,$@ -o $@ $^
$(pure_lib):
	ln -s $(pure_lib).$(pure_sover) $(pure_lib)
unicode-generator: unicode-generator.o
	libs=$$(pkg-config --libs icu-uc) && $(CC) $(LDFLAGS) -o $@ $^ $$libs
%.o: %.c Makefile
	$(CC) $(CFLAGS) -MD -MP -MF $@.dep -c -o $@ $<

unicode.o: unicode-allowlist-table.c
unicode.o: CFLAGS += $(shell pkg-config --cflags icu-uc)
ifeq ($(NO_REBUILD_TABLE),)
unicode-allowlist-table.c: unicode-generator
	./unicode-generator > $@.tmp
	mv -- $@.tmp $@
else ifneq ($(NO_REBUILD_TABLE),1)
$(error NO_REBUILD_TABLE should be empty or 1)
endif
check-table-up-to-date: unicode-generator unicode-allowlist-table.c
ifneq ($(NO_REBUILD_TABLE),1)
	$(error check-table-up-to-date without NO_REBUILD_TABLE=1 makes no sense)
endif
	/bin/bash -euc 'set -o pipefail; ./unicode-generator | diff -u ./unicode-allowlist-table.c -'
.PHONY: check-table-up-to-date

%.a: $(objs)
	$(AR) rcs $@ $^
clean:
	rm -f ./*.o ./*~ ./*.a ./*.so.* ./*.dep unicode-allowlist-table.c.tmp

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
