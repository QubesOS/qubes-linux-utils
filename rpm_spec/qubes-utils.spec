%define version %(cat version)
%if 0%{?qubes_builder}
%define _builddir %(pwd)
%endif

Name:		qubes-utils
Version:	%{version}
Release:	1%{?dist}
Summary:	Common Linux files for Qubes Dom0 and VM

Group:		Qubes
License:	GPL
URL:		http://www.qubes-os.org

Requires:	udev
BuildRequires:  qubes-libvchan-devel

%description
Common Linux files for Qubes Dom0 and VM

%package devel
Summary:	Development headers for qubes-utils
Release:	1%{?dist}

%description devel
Development header and files for qubes-utils

%prep


%build
make all

%install
make install DESTDIR=%{buildroot}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/etc/udev/rules.d/99-qubes-*.rules
/usr/libexec/qubes/udev-*


%files devel
%defattr(-,root,root,-)
/usr/include/libqrexec-utils.h
/usr/include/libqubes-rpc-filecopy.h
/usr/include/qrexec.h
%{_libdir}/libqrexec-utils.a
%{_libdir}/libqubes-rpc-filecopy.a

%changelog

