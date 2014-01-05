ifeq ($(shell uname -m),x86_64)
LIBDIR = /usr/lib64
else
LIBDIR = /usr/lib
endif
INCLUDEDIR = /usr/include

export LIBDIR INCLUDEDIR

help:
	echo "Use rpmbuild to compile this pacakge"
	exit 0


rpms:
	rpmbuild --define "_rpmdir rpm/" --define "_builddir ." -bb rpm_spec/qubes-utils.spec
all: 
	$(MAKE) -C qrexec-lib all
	$(MAKE) -C qmemman all

install:
	$(MAKE) -C udev install
	$(MAKE) -C qrexec-lib install
	$(MAKE) -C qmemman install

clean:
	$(MAKE) -C qrexec-lib clean
