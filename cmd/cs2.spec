Summary: Meiko CS/2 Support Programs
Name: cs2
Version: 0.1
Release: 5
Copyright: none
Group: System Environment/Base
Source: cs2-0.1.tgz
BuildRoot: /var/tmp/%{name}-buildroot
Prereq: kernel > 2.2.19-6.2.7cs2.6


%description
The CS/2 package includes userland support for Meiko devices,
including the Meiko CAN (Control Area Network), bargraph LED display,
and support for mapping the elan nanosecond clock into userspace.

%prep
%setup

%build
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc
mkdir -p $RPM_BUILD_ROOT/usr/bin
mkdir -p $RPM_BUILD_ROOT/usr/man/man8
install -s -o root -m 755 cancon $RPM_BUILD_ROOT/usr/bin
install -s -o root -m 755 cansnoop $RPM_BUILD_ROOT/usr/bin
install -s -o root -m 755 canctrl $RPM_BUILD_ROOT/usr/bin
install -s -o root -m 755 canhb $RPM_BUILD_ROOT/usr/bin
install -s -o root -m 755 canping $RPM_BUILD_ROOT/usr/bin
install -o root -m 755 canobj $RPM_BUILD_ROOT/etc
install -o root -m 755 canhosts.template $RPM_BUILD_ROOT/etc
gzip cancon.8 canctrl.8 canping.8 cansnoop.8
install -m 644 cancon.8.gz $RPM_BUILD_ROOT/usr/man/man8
install -m 644 canctrl.8.gz $RPM_BUILD_ROOT/usr/man/man8
install -m 644 canping.8.gz $RPM_BUILD_ROOT/usr/man/man8
install -m 644 cansnoop.8.gz $RPM_BUILD_ROOT/usr/man/man8

%files
%doc README ChangeLog DISCLAIMER

/etc/canobj
/etc/canhosts.template
/usr/bin/cancon
/usr/bin/cansnoop
/usr/bin/canctrl
/usr/bin/canhb
/usr/bin/canping
/usr/man/man8/cancon.8.gz
/usr/man/man8/canctrl.8.gz
/usr/man/man8/canping.8.gz
/usr/man/man8/cansnoop.8.gz

%changelog
