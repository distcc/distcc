Summary: Client side program for distributed C/C++ compilations.
Name: %{name}
Version: %{version}
Release: %{release}
License: GPL + Google Internal
Group: Development/Languages
BuildRoot: %{_tmppath}/%{name}-buildroot
Provides: distcc
Url: https://www.corp.google.com/eng/designdocs/google3/distcc-intra-build.html
Obsoletes: crosstool-distcc

%define _prefix /usr
%define _bindir %{_prefix}/bin
%define _datadir %{_prefix}/share
%define _docdir %{_datadir}/doc/%{name}
%define _libdir %{_prefix}/lib
# TODO grhat wants versioned doc dirs, but goobuntu apparently doesn't. Which
# should we use?
# %define _docdir %{_datadir}/doc/%{name}-%{version}
%define _mandir %{_datadir}/man
%define _sysconfdir /etc

%description
distcc is a program to distribute compilation of C or C++ code across several
machines on a network. distcc should always generate the same results as a
local compile, is simple to install and use, and is often two or more times
faster than a local compile.

%prep

%build
./run_all_autoconf.sh
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
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
install -m 644 distcc/packaging/RedHat/logrotate.d/distcc $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/distcc
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d
install -m 644 distcc/packaging/RedHat/xinetd.d/distcc $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d/distcc
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d
install -m 755 distcc/packaging/RedHat/init.d/distcc $RPM_BUILD_ROOT%{_sysconfdir}/init.d/distcc
# TODO(mtm) Does grhat really need these links for something?
mkdir -p $RPM_BUILD_ROOT/%{_libdir}/distcc
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/cc
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/c++
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/gcc
ln -s %{_bindir}/distcc $RPM_BUILD_ROOT/%{_libdir}/distcc/g++

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-, root, root, 0755)
%{_bindir}/distcc
%{_bindir}/distccmon-text
%{_bindir}/lsdistcc
# TODO(mtm) Do we need the libdir stuff for grhat (see %install TODO above)?
%{_libdir}/distcc
%doc %{_mandir}/man1/distcc.1.gz
%doc %{_mandir}/man1/distccmon-text.1.gz
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
    update-rc.d -f distcc remove
    update-rc.d distcc defaults 95 05 >/dev/null
    if [ -x /usr/sbin/invoke-rc.d ]; then
      invoke-rc.d distcc start || exit 0
    else
      /etc/init.d/distcc start || exit 0
    fi
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


%package include_server
Summary: Include server for distcc-pump
Group: Development/Languages

%description include_server
The include server is part of the distcc-pump project, as described in 
<https://www.corp.google.com/eng/designdocs/google3/distcc-intra-build.html>.

# The python_record_install file contains a list of the files installed 
# by the python install process.
%files include_server -f include_server/python_install_record
%{_bindir}/pump
%defattr(-,root,root)


%changelog
* Thu Jun 14 2007 Manos Renieris <manos@google.com> 2.18.3-17gg1-pump1
- Added all the distcc-pump related parts.
- Changed the way the package is built.
- Remove source package generation.
- Man pages are now unzipped.

* Mon May 29 2007 Dongmin Zhang <zhangdm@google.com> 2.18.3-17gg1
- Integrate changes and bug fix from Fergus's changes. Quote from his description:
    Add -r<PORT> option to lsdistcc to specify which port to connect to.
    ("-p", "-o", and "-t" were already taken.)
    Fix a bug where "lsdistcc distcc%d" was only returning the first host.
    Add some unit tests for lsdistcc.
    Also tidy up the usage message a little.

* Mon May 21 2007 Dongmin Zhang <zhangdm@google.com> 2.18.3-16gg1
- Change the package name to 2.18.3-16gg1 to make the goobuntu and grhat be
  able to pick up the newest version of distcc.

* Mon Feb 8 2007 Dongmin Zhang <zhangdm@google.com> 2.18.3-16gg
- Added hosts list option to lsdistcc, such that lsdistcc can check only the
  hosts listed on the given list. The host list is given in command line.

* Wed Jan 31 2007 Dongmin Zhang <zhangdm@google.com> 2.18.3-15gg
- Added _libdir definition.
- Changed Name, Version, and Release to the ones passed by --define.
- Updated the server init script to give different path of ACL files for grhat
  and goobuntu as suggested by Arthur Hyun <ahyun@google.com>.

* Mon Jan 22 2007 Dongmin Zhang <zhangdm@google.com> 2.18.3-14gg
- Fixed a bug in timeout patch. Added sigaction to catch SIGCHLD such that the
  select() in dcc_collect_child() could break out when the file is finished to
  compile.

%changelog
* Fri Dec 1 2006 Ollie Wild <aaw@google.com> 2.18.3-13gg
- Removed the 01-distcc-gdb-20051210.patch patch.  The parser in this was too
  naive to deal with output generate with the -directives-only flag.  Also, it
  should no longer be needed with recent versions of gcc.
- Removed the 10-distcc-before-cpp_locking-sub-gdb.patch and
  12-distcc-after-cpp_locking-add-gdb.patch patches.  These were just modifying
  the former patch.

%changelog
* Fri Sep 8 2006 Michael Moss <mmoss@google.com> 2.18.3-12gg
- Update install and init scripts to work on Debian and Red Hat (allowing the
  .rpm to be converted to .deb with alien, and then installed on Debian).
  Some noteworthy changes:
  - Deb - Remove unused defaults file /etc/default/distcc.
  - Deb - No longer uses debconf.
  - RH  - useradd is run in post- rather than pre- install.
  - RH  - distcc server is automatically started.
- Added enable/disable commands to init scripts so the daemon can be
  "permanently" disabled on misbehaving hosts.
- Added patches to allow building LSB-compliant binaries.
- Reorganized some existing patches to better partition functionality.

* Tue Feb 28 2006 Dan Kegel <dank@kegel.com> 2.18.3-11
- removed cache again

* Mon Feb 20 2006 Dan Kegel <dank@kegel.com> 2.18.3-10
- added cache

* Mon Feb 6 2006 Dan Kegel <dank@kegel.com> 2.18.3-9
- use Josh's randomize patch instead of Michael's,
  since Josh's seems to perform better in our tests
- added disk space statistic on http interface
- lsdistcc now has -x option to output info even on down hosts (will be useful for server side caching)

* Tue Jan 2 2006 Dan Kegel <dank@kegel.com> 2.18.3-8
- removed load shedding patch (we have swap turned on, so overload isn't as bad)
- added stats for timeout

* Wed Dec 8 2005 Dan Kegel <dank@kegel.com> 2.18.3-7
- lsdistcc now has -l option, better -v output
- distccd now has nicer logging, --limit-load option, bugfixes in load shedding

* Tue Nov 22 2005 Dan Kegel <dank@kegel.com> 2.18.3-6
- rejects jobs if load too high
- serves up stats via http on port 3633
- lsdistcc now has new -p and -c0 options

* Wed Nov  2 2005 Dan Kegel <dank@kegel.com> 2.18.3-5
- updated lsdistcc to use the longer of the two of HOST and HOSTNAME
  to handle shells that set HOST to the nonqualified hostname,
  but HOSTNAME to the FQDN
- Changes copyright 2005 Google.  GPL.

* Thu Oct 13 2005 Dan Kegel <dank@kegel.com> 2.18.3-4
- updated lsdistcc patch
- removed gcc as a dependency, since we want to use it with
  a wide range of other compilers (and in our case, not the
  standard gcc), and it's impractical to list them all as dependencies
- Changes copyright 2005 Google.  GPL.

* Fri Sep 16 2005 Dan Kegel <dank@kegel.com> 2.18.3-3
- now reads /etc/distcc/hosts instead of /usr/etc/distcc/hosts
- replaced distcc-2.18.3-rhl.patch with distcc-2.18.3-stringmap.patch
  The stringmap patch updates the rhl init.d script to know about
  all installed crosstool toolchains, and enables fuzzy path matching
  This is useful if the toolchains are not installed at the same
  prefix on all systems
- removed distcc-domain.patch
- added distcc-2.18.3-lsdistcc.patch
- Changes copyright 2005 Google.  GPL.

* Sat Sep  3 2005 Dan Kegel <dank@kegel.com> 2.18.3-2
- now reads /etc/distcc/hosts instead of /usr/etc/distcc/hosts
- applied distcc-domain-2.patch
- Changes copyright 2005 Google.  GPL.

* Thu Jun 15 2005 Dan Kegel <dank@kegel.com> 2.18.3-1
- Updated to 2.18.3
- applied --randomize patch and cpp_locking patch
- redhat init.d script reads /etc/distccd.allow to construct --allow arguments
- added scriptlets from dag's package, but don't start service on install,
  and call it distcc rather than distccd (to match the current 
  packaging's old practice)
- Changes copyright 2005 Google.  GPL.

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
