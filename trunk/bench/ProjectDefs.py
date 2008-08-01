# benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003, 2004 by Martin Pool
# Copyright 2008 Google Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
# USA.

__doc__ = """distcc benchmark project definitions"""

from Project import Project

# Consider the following when adding a new project:
# 1) Should be fairly large.  That's where distcc helps most.
# 2) Should have minimal dependencies.  That way, the build will
#    succeed without you needing to install lots of libraries
#    on your machine.
#
# See Project.py:Project.__init__.doc() for documentation of the
# named arguments that are allowed for Project.


Project(url='http://ftp.gnu.org/gnu/hello/hello-2.1.1.tar.gz',
        md5='70c9ccf9fac07f762c24f2df2290784d *hello-2.1.1.tar.gz',
        ).register()

Project(url='http://ftp.gnu.org/gnu/make/make-3.80.tar.bz2',
        md5='0bbd1df101bc0294d440471e50feca71 *make-3.80.tar.bz2'
        ).register()

Project(url='ftp://ftp.gtk.org/pub/gtk/v2.0/glib-2.0.7.tar.bz2',
        md5='5882b1e729f57cb18af653a2f504197b  glib-2.0.7.tar.bz2'
        ).register()

Project(url='http://us1.samba.org/samba/ftp/old-versions/samba-2.2.7.tar.gz',
        build_subdir='source',
        md5='824cd4e305f9b744f3eec702a7b96f7f  samba-2.2.7.tar.gz',
        ).register()

Project(url='http://us1.samba.org/samba/ftp/old-versions/samba-3.0.20.tar.gz',
        name='samba-3.0.20',
        build_subdir='source',
        # newer versions of popt can be incompatible
        configure_cmd='./configure --with-included-popt',
        pre_build_cmd = 'make proto', 
        ).register()

Project(url='http://archive.apache.org/dist/httpd/httpd-2.0.43.tar.gz',
        md5='8051de5d160c43d4ed2cc47dc9be6fd3  httpd-2.0.43.tar.gz'
        ).register()

Project(url='http://yate.null.ro/tarballs/yate2/yate-2.0.0-alpha2.tar.gz',
        name='yate',
        configure_cmd='./configure --without-libpq --without-mysql --without-wphwec --without-libgsm --without-libspeex --without-spandsp --without-pwlib --without-openh323 --without-libgtk2 --without-gtkmozilla --without-libqt4 --without-coredumper --without-doxygen --without-kdoc',
        md5='b9fd116bc5c8142de6e130931cd3bdf2  yate-2.0.0-alpha2.tar.gz'
        ).register()

Project(url='http://www.kernel.org/pub/linux/kernel/v2.6/linux-2.6.25.tar.bz2',
        md5='db95a49a656a3247d4995a797d333153 *linux-2.6.25.tar.bz2',
        configure_cmd="make V=1 HOSTCC='$(CC)' defconfig",
        build_cmd="make V=1 HOSTCC='$(CC)' bzImage",
        include_server_args='--stat_reset_triggers=include/linux/compile.h:include/asm/asm-offsets.h'
        ).register()

Project(url='http://www.python.org/ftp/python/2.5.2/Python-2.5.2.tgz',
        name='Python-2.5.2',
        ).register()

Project(url='http://ftp.gnu.org/gnu/binutils/binutils-2.18.tar.bz2',
        configure_cmd = './configure --disable-werror',
        ).register()

# disable-sanity-checks is needed to stop it wanting linuxthreads --
# the resulting library is useless, but this is only a test.
Project(url = 'http://ftp.gnu.org/pub/gnu/glibc/glibc-2.6.tar.bz2',
        build_subdir = '_build',
        configure_cmd = '../configure --disable-sanity-checks',
        ).register()

Project(url='http://mirror.trouble-free.net/mysql_mirror/Downloads/MySQL-5.0/mysql-5.0.51b.tar.gz',
        md5='e6715a878a7c102f7a4c323f9ef63e8f *mysql-5.0.51b.tar.gz',
        configure_cmd = './configure',
        ).register()

Project(url='http://sources-redhat.oc1.mirrors.redwire.net/gdb/old-releases/gdb-5.3.tar.gz',
        ).register()

#### Commented out: gimp 1.2.3 has makefile bugs that break -j
## Project(url='ftp://212.8.35.65/pub/FreeBSD/distfiles/gimp-1.2.3.tar.bz2',
##        md5='b19235f19f524f772a4aef597a69b1da *gimp-1.2.3.tar.bz2',
##        configure_cmd='./configure --disable-perl',
##        ).register()

Project(url='ftp://ftp.gimp.org/pub/gimp/v2.2/gimp-2.2.10.tar.bz2',
        md5='aa29506ed2272af02941a7a601a7a097  gimp-2.2.10.tar.bz2',
        configure_cmd='./configure --disable-perl --disable-print',
        ).register()

Project(url='http://ibiblio.org/pub/linux/system/emulators/wine/wine-0.9.4.tar.bz2',
        md5='73205d83a5612a43441a8532683c0434  wine-0.9.4.tar.bz2',
        ).register()

Project(url='ftp://ftp.slackware.com/pub/slackware/slackware-9.1/source/xap/mozilla/mozilla-source-1.4.tar.bz2',
        name='mozilla-1.4',
        configure_cmd="LIBIDL_CONFIG=libIDL-config-2 ./configure",
        unpacked_subdir='mozilla',
        ).register()

Project(url='http://ftp.mozilla.org/pub/firebird/releases/0.6/MozillaFirebird-0.6-source.tar.bz2',
        name='MozillaFirebird',
        unpacked_subdir='mozilla',
        ).register()

#### Commented out: configure script hasn't kept up with modern gcc's.
#### For instance, they check whether setrlimit takes an enum as the
#### first argument by grepping for 'setrlimit(enum' in the .h file,
#### but crosstool gcc's use a typedef, so it's 'setrlimit(newtype'.
## Project(url='http://download.dre.vanderbilt.edu/previous_versions/ACE+TAO+CIAO-5.6.5.tar.bz2',
##         name='ace-5.6.5',
##         unpacked_subdir='ACE_wrappers',
##         build_subdir = '_build',
##         configure_cmd='../configure',
##         md5='32157a0a4cc9bd8dc03d98b90b707759  ACE+TAO+CIAO-5.6.5.tar.bz2'
##         ).register()
