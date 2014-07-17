#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2015  Marek Marczykowski-Górecki
#                       <marmarek@invisiblethingslab.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#


%{!?version: %define version %(cat version)}

Name:		qubes-kernel-vm-support
Version:	%{version}
Release:	1%{?dist}
Summary:	Qubes VM kernel and initramfs modules

Group:		Qubes
Vendor:     Invisible Things Lab
License:	GPL v2 only
URL:		http://www.qubes-os.org

Requires:	dracut
Requires:	dkms

%define _builddir %(pwd)

%description
This package contains:
1. Dracut module required to setup Qubes VM root filesystem. This package is
needed in VM only when the VM uses its own kernel (via pvgrub or so). Otherwise
initrd is provided by dom0.

2. u2mfn kernel module sources (dkms) required by GUI agent and R2 version of
libvchan library.

%prep
# we operate on the current directory, so no need to unpack anything
# symlink is to generate useful debuginfo packages
rm -f %{name}-%{version}
ln -sf . %{name}-%{version}
%setup -T -D

%build

%install
make install-kernel-support DESTDIR=%{buildroot}

%files
/usr/lib/dracut/modules.d/90qubes-vm
/usr/src/u2mfn-%{version}/

%post
dkms add -m u2mfn -v %{version} --rpm_safe_upgrade

%preun
dkms remove -m u2mfn -v %{version} --all --rpm_safe_upgrade

%changelog

