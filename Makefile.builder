SPEC_FILES := rpm_spec/qubes-utils.spec rpm_spec/qubes-kernel-vm-support.spec
RPM_SPEC_FILES.dom0 := $(if $(filter $(DIST_DOM0), $(DISTS_VM)),, $(SPEC_FILES))
RPM_SPEC_FILES.vm := $(SPEC_FILES)
RPM_SPEC_FILES := $(RPM_SPEC_FILES.$(PACKAGE_SET))

ARCH_BUILD_DIRS := archlinux
DEBIAN_BUILD_DIRS := debian
