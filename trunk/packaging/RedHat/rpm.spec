%define	RELEASE	1
%define rel     %{?CUSTOM_RELEASE} %{!?CUSTOM_RELEASE:%RELEASE}
%define	_prefix	/usr
%define _bindir %{_prefix}/bin
%define _datadir %{_prefix}/share
#%define _docdir %{_datadir}/doc/%{name}-%{version}
%define _docdir %{_datadir}/doc/%{name}
%define _libdir %{_prefix}/lib
%define _mandir %{_datadir}/man
%define _sysconfdir /etc

Name: %NAME
Summary: Client side program for distributed C/C++ compilations.
Version: %VERSION
Release: %rel
Group: Development/Languages
Url: https://code.google.com/p/distcc
License: GPL
Packager: Google Inc. <opensource@google.com>
Source: http://%{NAME}.googlecode.com/files/%{NAME}-%{VERSION}.tar.gz
Distribution: Redhat 7 and above.
BuildRoot: %{_tmppath}/%{name}-buildroot
Prefix: %_prefix
Provides: distcc
Obsoletes: crosstool-distcc distcc-include-server

%description
distcc is a program to distribute compilation of C or C++ code across several
machines on a network. distcc should always generate the same results as a
local compile, is simple to install and use, and is often two or more times
faster than a local compile.

%prep
%setup

%build
# Work around broken sendfile in 32 bit apps on some x86_64 systems
ac_cv_func_sendfile=no ac_cv_header_sys_sendfile_h=no ./configure \
  --prefix=%{_prefix} \
  --bindir=%{_bindir} \
  --sysconfdir=%{_sysconfdir} \
  --datadir=%{_datadir} \
  --with-docdir=%{_docdir} \
  --mandir=%{_mandir} \
  --enable-rfc2553 --with-included-popt
# Get the list of files installed by the python install process
# by asking make to tell setup.py to put it in python_install_record
make RPM_OPT_FLAGS="$RPM_OPT_FLAGS" \
     PYTHON_INSTALL_RECORD=python_install_record

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=${RPM_BUILD_ROOT} PYTHON_INSTALL_RECORD=python_install_record install
# The remaining configuration files are installed here rather than by
# 'make install' because their nature and their locations are too
# system-specific.
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
install -m 644 packaging/RedHat/logrotate.d/distcc $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/distcc
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d
install -m 644 packaging/RedHat/xinetd.d/distcc $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d/distcc
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d
install -m 755 packaging/RedHat/init.d/distcc $RPM_BUILD_ROOT%{_sysconfdir}/init.d/distcc
# TODO(fergus): move the next five lines to 'make install'?
mkdir -p $RPM_BUILD_ROOT/%{_libdir}/distcc
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/cc
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/c++
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/gcc
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/g++

%clean
rm -rf $RPM_BUILD_ROOT

%files -f python_install_record
%defattr(-, root, root, 0755)
%{_bindir}/distcc
%{_bindir}/distccmon-text
%{_bindir}/lsdistcc
%{_libdir}/distcc
%{_bindir}/pump
%dir %{_sysconfdir}/distcc
%config %{_sysconfdir}/distcc/hosts
%doc %{_mandir}/man1/distcc.1.gz
%doc %{_mandir}/man1/distccmon-text.1.gz
%doc %{_mandir}/man1/pump.1.gz
%doc %{_mandir}/man1/include_server.1.gz
%doc %{_mandir}/man1/lsdistcc.1.gz
%doc %{_docdir}


%package server
Summary: Server side program for distributed C/C++ compilations.
Group: Development/Languages
Provides: distccd
Obsoletes: crosstool-distcc-server

%description server
distcc is a program to distribute compilation of C or C++ code across several
machines on a network. distcc should always generate the same results as a
local compile, is simple to install and use, and is often two or more times
faster than a local compile.

%files server
%defattr(-, root, root, 0755)
%{_bindir}/distccd
%dir %{_sysconfdir}/logrotate.d
%config %{_sysconfdir}/logrotate.d/distcc
# Don't list init.d dir because on Red Hat it's a symlink owned by
# chkconfig, so it causes a conflict on install.
#%dir %{_sysconfdir}/init.d
%config %{_sysconfdir}/init.d/distcc
%dir %{_sysconfdir}/xinetd.d/
%config %{_sysconfdir}/xinetd.d/distcc
%dir %{_sysconfdir}/distcc
%config %{_sysconfdir}/distcc/clients.allow
%config %{_sysconfdir}/distcc/commands.allow.sh
%dir %{_sysconfdir}/default
%config %{_sysconfdir}/default/distcc
%doc %{_mandir}/man1/distccd.1.gz

%pre server

%post server
DISTCC_USER=distcc
if [ -s /etc/redhat-release ]; then
  # sadly, can't useradd -s /sbin/nologin on rh71, since
  # then starting the service as user distcc fails,
  # since it uses su - without overriding the shell :-(
  # See https://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=26894
  /sbin/service distcc stop &>/dev/null || :
  if fgrep 'nice initlog $INITLOG_ARGS -c "su - $user' /etc/init.d/functions | fgrep -v '.-s ' > /dev/null 2>&1 ; then
    # Kludge: for Red Hat 6.2, don't use -s /sbin/nologin
    /usr/sbin/useradd -d /var/run/distcc -m -r $DISTCC_USER &>/dev/null || :
  else
    # but do for everyone else
    /usr/sbin/useradd -d /var/run/distcc -m -r -s /sbin/nologin $DISTCC_USER &>/dev/null || :
  fi
else
  echo Creating $DISTCC_USER user...
  if ! id $DISTCC_USER > /dev/null 2>&1 ; then
    if ! id -g $DISTCC_USER > /dev/null 2>&1 ; then
      addgroup --system --gid 11 $DISTCC_USER
    fi
    adduser --quiet --system --gid 11 \
      --home / --no-create-home --uid 15 $DISTCC_USER
  fi
fi

DISTCC_LOGFILE=/var/log/distccd.log
if [ ! -s $DISTCC_LOGFILE ]; then
  touch $DISTCC_LOGFILE
  chown ${DISTCC_USER}:adm $DISTCC_LOGFILE
  chmod 640 $DISTCC_LOGFILE
fi

if ! grep -q "3632/tcp" /etc/services; then
  echo -e "distcc\t\t3632/tcp\t\t\t# Distcc Distributed Compiler" >> /etc/services
fi

if ! grep -q "^distcc:" /etc/hosts.allow; then
  echo -e "distcc:\t127.0.0.1" >> /etc/hosts.allow
fi

# Update runlevel settings and start daemon.
if [ -s /etc/redhat-release ]; then
  /sbin/chkconfig --add distcc
  /etc/init.d/distcc start || exit 0
else
  if [ -x "/etc/init.d/distcc" ]; then
    update-rc.d -f distcc remove >/dev/null
    update-rc.d distcc defaults 95 05 >/dev/null
    if [ -x /usr/sbin/invoke-rc.d ]; then
      start_command="invoke-rc.d distcc start"
    else
      start_command="/etc/init.d/distcc start"
    fi
    $start_command || {
        echo "To enable distcc's TCP mode, you should edit these files"
        echo "        %{_sysconfdir}/distcc/clients.allow"
        echo "        %{_sysconfdir}/distcc/commands.allow.sh"
        echo "and then run (as root)"
        echo "        $start_command"
        echo "For more info, including alternatives to TCP mode, see"
        echo "%{_docdir}/INSTALL and %{_docdir}/examples/README."
    }
  fi
fi

%preun server
# Remove hosts.allow entry.
if grep -q "^distcc:" /etc/hosts.allow; then
  sed -e "/^distcc/d" /etc/hosts.allow > /etc/hosts.allow.new
  mv /etc/hosts.allow.new /etc/hosts.allow
fi

# Stop daemon and clear runlevel settings.
if [ -s /etc/redhat-release ]; then
  if [ $1 -eq 0 ]; then
    /sbin/service distcc stop &>/dev/null || :
  fi
  # chkconfig --del must run before deleting init script.
  /sbin/chkconfig --del distcc
else
  if [ -x "/etc/init.d/distcc" ]; then
    if [ -x /usr/sbin/invoke-rc.d ] ; then
      invoke-rc.d distcc stop || exit 0
    else
      /etc/init.d/distcc stop || exit 0
    fi
  fi
fi

%postun server
# TODO(mtm) Should Red Hat also remove user/group?
if [ -s /etc/debian_version ]; then
  case "$1" in
    purge)
      deluser --quiet --system distcc
      delgroup --quiet --system distcc
      ;;
    remove)
      ;;
    upgrade|failed-upgrade|abort-install|abort-upgrade|disappear)
      ;;
    *)
      echo "postrm called with unknown argument \`$1'" >&2
      exit 1
    ;;
  esac

  if [ "$1" = "purge" ] ; then
    # update-rc.d must run after deleting init script.
    update-rc.d distcc remove >/dev/null || exit 0
  fi
fi


%changelog
* Sat Mar 12 2008 Craig Silverstein <opensource@google.com> 3.0-1
- Updated to 3.0
- Added include-server files
- useradd is run in post- rather than pre-install
- distcc server is automatically started
- Remove source package generation
- Man pages are now unzipped
- Deb packages now also built, using alien

* Sat May 31 2003 Terry Griffin <terryg@axian.com> 2.5-2
- Updated to 2.5

* Sat May 24 2003 Terry Griffin <terryg@axian.com> 2.4.2-2
- Updated to 2.4.2

* Sat May 17 2003 Terry Griffin <terryg@axian.com> 2.3-2
- Updated to 2.3

* Sun May 04 2003 Terry Griffin <terryg@axian.com> 2.1-2
- Updated to 2.1
- Added symbolic links for masquerade mode

* Fri Mar 28 2003 Terry Griffin <terryg@axian.com> 2.0.1-2
- Updated to 2.0.1
- Removed info file from document list.

* Tue Feb 25 2003 Terry Griffin <terryg@axian.com> 1.2.1-2
- Updated to 1.2.1

* Mon Jan 27 2003 Terry Griffin <terryg@axian.com> 1.1-2
- Updated to 1.1
- Minor improvements to the RPM spec file

* Mon Dec 16 2002 Terry Griffin <terryg@axian.com> 0.15-2
- Changed server user back to 'nobody'

* Fri Dec 13 2002 Terry Griffin <terryg@axian.com> 0.15-2
- Updated to 0.15
- Changed port number in server configs to 3632

* Sat Nov 23 2002 Terry Griffin <terryg@axian.com> 0.14-2
- Updated to 0.14
- Major rework of the RPM spec file
- Added Red Hat server config files for both xinetd and SysV init.
- Change server user to daemon.

* Sat Nov 09 2002 Terry Griffin <terryg@axian.com> 0.12-1
- Updated to 0.12

* Thu Oct 10 2002 Terry Griffin <terryg@axian.com> 0.11-3
- First binary packages for Red Hat 8.x
- Fixed xinetd config file for location of distccd.

* Mon Sep 30 2002 Terry Griffin <terryg@axian.com> 0.11-2
- Moved distccd back to /usr/bin from /usr/sbin.

* Sat Sep 28 2002 Terry Griffin <terryg@axian.com> 0.11-1
- Initial build (Red Hat 7.x)
- Client and server in separate binary packages
- Added xinetd config file
- Moved distccd to /usr/sbin
- Added version number suffix to the documentation directory
