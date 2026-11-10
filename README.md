# Qubes Linux Utilities

This repository holds low-level utilities and libraries shared by the Linux
components of [Qubes OS](https://www.qubes-os.org), used in both dom0 and VMs.

It contains:

- **libqubes-rpc-filecopy** — C library implementing the `qfile` protocol for
  sending and receiving files between qubes.
- **libqubes-pure** — C library of Qubes-specific helpers, such as filename and
  string validation.
- **qubesimgconverter** — Python package for handling and converting images.
- **meminfo-writer** — reports a VM's memory usage, used for dynamic memory
  balancing.
- **VM kernel and initramfs support** — dracut and initramfs-tools modules that
  set up the Qubes VM root filesystem when a VM boots its own kernel, plus
  related GPT and grub helpers.
- **udev rules** and a Xen block-device backend helper for Qubes block and USB
  devices.

For how Qubes is built and how its components fit together, see the
[Qubes OS documentation](https://www.qubes-os.org/doc/).
