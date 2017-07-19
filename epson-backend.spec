# epson-backend.spec.in -- an rpm spec file templete
# Copyright (C) Seiko Epson Corporation 2015.
#  This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

%define pkg epson-backend
%define ver 1.0.1
%define rel 1

# used in RPM macro set for the LSB Driver Development Kit
%define drivername      epson-backend
%define driverstr       epson-backend
%define distribution    LSB
%define manufacturer    EPSON
%define supplier        %{drivername}
%define lsbver          3.2

%define extraversion    -%{rel}lsb%{lsbver}
%define supplierstr     @VENDOR_NAME@

AutoReqProv: no


Name: %{pkg}
Version: %{ver}
Release: %{rel}lsb%{lsbver}
Source0: %{name}_%{version}-%{release}.tar.gz
License: GPL
Vendor: Seiko Epson Corporation
URL: http://download.ebz.epson.net/dsc/search/01/search/?OSC=LX
Packager: Seiko Epson Corporation <linux-printer@epson.jp>
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
Group: Hardware/Printing
Requires: lsb >= %{lsbver}
Summary: Epson Custom Backend

# in x86_64 environment, `cups-config --serverbin` and %{_libdir}/cups are
# not necessarily the same.
%define cups_serverbin %(type cups-config >/dev/null 2>&1 && cups-config --serverbin || echo "%{_libdir}/cups")

%description
This software is a USB backend program used with Common UNIX Printing System
(CUPS) from the Linux. 

%prep
%setup -q

%build
%configure
make

%install
rm -rf ${RPM_BUILD_ROOT}
make install DESTDIR=${RPM_BUILD_ROOT}
mkdir -p ${RPM_BUILD_ROOT}/%{_localstatedir}/cache/%{name}

%post
/sbin/ldconfig
%{_libdir}/%{name}/scripts/inst-cups-post.sh install

%preun
if [ "$1" = 0 ] ; then
        %{_libdir}/%{name}/scripts/inst-cups-post.sh deinstall
        rm -f %{_libdir}/%{name}/setup
fi

%postun
/sbin/ldconfig

%clean
make clean
rm -rf ${RPM_BUILD_ROOT}

%files
%defattr(-,root,root)
%{_libdir}/%{name}/scripts/inst-cups-post.sh
%doc README COPYING AUTHORS NEWS

%{cups_serverbin}/backend/ecblp


%dir %{_libdir}/%{name}
%{_libdir}/%{name}/ecbd

%dir %{_libdir}/%{name}/rc.d
%{_libdir}/%{name}/rc.d/inst-rc_d.sh
%{_libdir}/%{name}/rc.d/ecbd
%{_libdir}/%{name}/rc.d/init-functions

%dir %{_localstatedir}/cache/%{name}
