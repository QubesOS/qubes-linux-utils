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
Requires:	%{name}-libs
BuildRequires:  qubes-libvchan-devel

%description
Common Linux files for Qubes Dom0 and VM

%package devel
Summary:	Development headers for qubes-utils
Release:	1%{?dist}
Requires:	%{name}-libs

%description devel
Development header and files for qubes-utils

%package libs
Summary: Qubes utils libraries
Release:	1%{?dist}

%description libs
Libraries for qubes-utils

%prep


%build
make all

%install
make install DESTDIR=%{buildroot}

%post
if [ -r /etc/qubes-release ]; then
    # dom0
    /bin/systemctl enable qubes-meminfo-writer-dom0.service > /dev/null 2>&1
else
    # VM
    /bin/systemctl enable qubes-meminfo-writer.service > /dev/null 2>&1
fi

%postun
if [ $1 -eq 0 ]; then
    /bin/systemctl disable qubes-meminfo-writer.service > /dev/null 2>&1
    /bin/systemctl disable qubes-meminfo-writer.service > /dev/null 2>&1
fi

%post libs -p /sbin/ldconfig
%postun libs -p /sbin/ldconfig

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/etc/udev/rules.d/99-qubes-*.rules
/usr/libexec/qubes/udev-*
%{_sbindir}/meminfo-writer
%{_unitdir}/qubes-meminfo-writer.service
%{_unitdir}/qubes-meminfo-writer-dom0.service

%files libs
%{_libdir}/libqrexec-utils.so.1
%{_libdir}/libqubes-rpc-filecopy.so.1


%files devel
%defattr(-,root,root,-)
/usr/include/libqrexec-utils.h
/usr/include/libqubes-rpc-filecopy.h
/usr/include/qrexec.h
%{_libdir}/libqrexec-utils.so
%{_libdir}/libqubes-rpc-filecopy.so

%changelog

