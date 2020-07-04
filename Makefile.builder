RPM_SPEC_FILES := rpm_spec/qubes-utils.spec rpm_spec/qubes-kernel-vm-support.spec
ARCH_BUILD_DIRS := archlinux
DEBIAN_BUILD_DIRS := debian
SOURCE_COPY_IN.bullseye := source-debian-copy-in
SOURCE_COPY_IN := $(SOURCE_COPY_IN.$(DIST))

source-debian-copy-in:
	patch -p1 -d "$(CHROOT_DIR)/$(DIST_SRC)" < $(ORIG_SRC)/debian/patches/debian-drop-python2.patch
