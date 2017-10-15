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
Requires:	ImageMagick
%if 0%{?rhel} >= 7
Requires:	python34-qubesimgconverter
%else
Requires:	python3-qubesimgconverter
%endif
BuildRequires:  qubes-libvchan-devel
BuildRequires:  python-setuptools
%if 0%{?rhel} >= 7
BuildRequires:  python34-setuptools
%else
BuildRequires:  python3-setuptools
%endif
BuildRequires:  python2-rpm-macros
BuildRequires:  python3-rpm-macros
# for meminfo-writer
BuildRequires:  xen-devel

%description
Common Linux files for Qubes Dom0 and VM

%package -n python2-qubesimgconverter
Summary:    Python package qubesimgconverter
Requires:   python
Requires:   pycairo

%description -n python2-qubesimgconverter
Python package qubesimgconverter

%if 0%{?rhel} >= 7
%package -n python34-qubesimgconverter
Summary:    Python package qubesimgconverter
Requires:   python34
Requires:   python34-cairo

%description -n python34-qubesimgconverter
Python package qubesimgconverter
%else
%package -n python3-qubesimgconverter
Summary:    Python package qubesimgconverter
Requires:   python3
Requires:   python3-cairo

%description -n python3-qubesimgconverter
Python package qubesimgconverter
%endif

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
# we operate on the current directory, so no need to unpack anything
# symlink is to generate useful debuginfo packages
rm -f %{name}-%{version}
ln -sf . %{name}-%{version}
%setup -T -D


%build
make all

%install
make install DESTDIR=%{buildroot} PYTHON=%{__python2}
rm -rf imgconverter/build
%make_install -C imgconverter PYTHON=%{__python3}

%post
# dom0
/bin/systemctl enable qubes-meminfo-writer-dom0.service > /dev/null 2>&1
# VM
/bin/systemctl enable qubes-meminfo-writer.service > /dev/null 2>&1

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
/lib/udev/rules.d/99-qubes-*.rules
/usr/lib/qubes/udev-*
%{_sbindir}/meminfo-writer
%{_unitdir}/qubes-meminfo-writer.service
%{_unitdir}/qubes-meminfo-writer-dom0.service

%files -n python2-qubesimgconverter
%{python_sitelib}/qubesimgconverter/__init__.py*
%{python_sitelib}/qubesimgconverter/imggen.py*
%{python_sitelib}/qubesimgconverter/test.py*
%{python_sitelib}/qubesimgconverter-%{version}-py?.?.egg-info/*

%if 0%{?rhel} >= 7
%files -n python34-qubesimgconverter
%else
%files -n python3-qubesimgconverter
%endif
%{python3_sitelib}/qubesimgconverter/__init__.py
%{python3_sitelib}/qubesimgconverter/imggen.py
%{python3_sitelib}/qubesimgconverter/test.py
%{python3_sitelib}/qubesimgconverter-%{version}-py?.?.egg-info/*
%{python3_sitelib}/qubesimgconverter/__pycache__

%files libs
%{_libdir}/libqrexec-utils.so.2
%{_libdir}/libqubes-rpc-filecopy.so.2

%files devel
%defattr(-,root,root,-)
/usr/include/libqrexec-utils.h
/usr/include/libqubes-rpc-filecopy.h
/usr/include/qrexec.h
%{_libdir}/libqrexec-utils.so
%{_libdir}/libqubes-rpc-filecopy.so

%changelog

