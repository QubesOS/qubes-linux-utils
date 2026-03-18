all: gpt

CFLAGS ?=
LDLIBS += -lz
CFLAGS += -D_GNU_SOURCE -fno-strict-aliasing -fno-delete-null-pointer-checks -fno-strict-overflow
%: %.c Makefile
	$(CC) -o $@ $< $(CFLAGS) -MD -MP -MF $@.dep $(LDLIBS)

install: gpt
	install -D gpt $(DESTDIR)$(SBINDIR)/gptfix

-include *.dep
