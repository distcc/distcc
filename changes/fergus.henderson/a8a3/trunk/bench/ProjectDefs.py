# benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003, 2004 by Martin Pool

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA

__doc__ = """distcc benchmark project definitions"""

from Project import Project

# Would like to test glibc, but it needs a separate source and build
# directory, and this tool doesn't support that yet.  

# disable-sanity-checks is needed to stop it wanting linuxthreads --
# the resulting library is useless, but this is only a test.

#Project(url = 'http://ftp.gnu.org/pub/gnu/glibc/glibc-2.3.2.tar.bz2',
#        configure_cmd = './configure --disable-sanity-checks'
#        ).register()

#Project(url='http://mirror.aarnet.edu.au/pub/gnu/libc/glibc-2.3.tar.bz2',
#        configure_cmd='./configure --disable-sanity-checks',
#        md5='fd20b4a9feeb2b2f0f589b1a9ae8a5e2  glibc-2.3.tar.bz2').register()

Project(url='http://archive.apache.org/dist/httpd/httpd-2.0.43.tar.gz',
        md5='8051de5d160c43d4ed2cc47dc9be6fd3  httpd-2.0.43.tar.gz').register()

Project(url='ftp://ftp.gtk.org/pub/gtk/v2.0/glib-2.0.7.tar.bz2',
        md5='5882b1e729f57cb18af653a2f504197b  glib-2.0.7.tar.bz2').register()

Project(url='http://us1.samba.org/samba/ftp/old-versions/samba-2.2.7.tar.gz',
        build_subdir='source',
        md5='824cd4e305f9b744f3eec702a7b96f7f  samba-2.2.7.tar.gz',
        ).register()

Project(url='http://ftp.gnu.org/gnu/make/make-3.80.tar.bz2',
        md5='0bbd1df101bc0294d440471e50feca71 *make-3.80.tar.bz2'
        ).register()

# failed: "make: *** No rule to make target `defconfig'.  Stop."
#Project(url='http://public.ftp.planetmirror.com/pub/linux/kernel/v2.4/linux-2.4.20.tar.bz2',
#        configure_cmd='make defconfig',
#        build_cmd='make bzImage',
#        ).register()

Project(url='http://www.kernel.org/pub/linux/kernel/v2.5/linux-2.5.51.tar.bz2',
        md5='2300b7b7d2ce4c017fe6dae49717fd9a *linux-2.5.51.tar.bz2',
        configure_cmd='make defconfig',
        build_cmd='make bzImage'
        ).register()

Project(url='http://sources-redhat.oc1.mirrors.redwire.net/gdb/old-releases/gdb-5.3.tar.gz',
        ).register()

## gimp 1.2.3 has makefile bugs that break -j
## Project(url='ftp://212.8.35.65/pub/FreeBSD/distfiles/gimp-1.2.3.tar.bz2',
##        md5='b19235f19f524f772a4aef597a69b1da *gimp-1.2.3.tar.bz2',
##        configure_cmd='./configure --disable-perl',
##        ).register()

Project(url='ftp://ftp.gimp.org/pub/gimp/v2.2/gimp-2.2.10.tar.bz2',
        md5='aa29506ed2272af02941a7a601a7a097  gimp-2.2.10.tar.bz2',
        configure_cmd='./configure --disable-perl --disable-print',
        ).register()

## Project(url='http://ibiblio.org/pub/linux/system/emulators/wine/wine-0.9.3.tar.bz2',
##         ).register()

Project(url='http://ibiblio.org/pub/linux/system/emulators/wine/wine-0.9.4.tar.bz2',
        md5='73205d83a5612a43441a8532683c0434  wine-0.9.4.tar.bz2',
        ).register()

Project(url='http://ftp.gnu.org/gnu/hello/hello-2.1.1.tar.gz',
        md5='70c9ccf9fac07f762c24f2df2290784d *hello-2.1.1.tar.gz',
        ).register()


# XXX: Does not build on Debian at the moment, problem with libIDL-config

# Project(url='http://mirror.aarnet.edu.au/pub/mozilla/releases/mozilla1.4/src/mozilla-source-1.4.tar.bz2',
#         name='mozilla-1.4',
#         configure_cmd="LIBIDL_CONFIG=libIDL-config-2 ./configure",
#         unpacked_subdir='mozilla',
#         ).register()


Project(url='http://ftp.mozilla.org/pub/firebird/releases/0.6/MozillaFirebird-0.6-source.tar.bz2',
        name='MozillaFirebird',
        unpacked_subdir='mozilla',
        ).register()

Project(url='http://us1.samba.org/samba/ftp/old-versions/samba-3.0.20.tar.gz',
        name='samba-3.0.20',
        build_subdir='source',
        # newer versions of popt can be incompatible
        configure_cmd='./configure --with-included-popt',
        pre_build_cmd = 'make proto', 
        ).register()
