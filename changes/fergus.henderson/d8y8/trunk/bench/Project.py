# benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003 by Martin Pool

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

import re, os, sys, time
from buildutil import make_dir, run_cmd


# Trees of software to be built.
trees = { }


class Project:
    """Defines a project to be built and tested.

    The Python process remains in the top-level directory for the
    whole process.  Commands are kicked off in subdirectories if
    necessary.

    'subdir' variables give just a single component of a name; 'dir' variables
    give a full path."""
    
    def __init__(self, url, package_file=None,
                 md5=None,
                 name=None,
                 configure_cmd=None,
                 pre_build_cmd = None,
                 build_cmd=None,
                 unpacked_subdir=None,
                 build_subdir=None,
                 source_name=None):
        self.url = url
        if not package_file:
            package_file = url.split('/')[-1]
        self.package_file = package_file

        if not name:
            name = re.match(r"(.*)\.tar(\.gz|\.bz2|)$", package_file).group(1)
        self.name = name

        self.md5 = md5
        
        self.configure_cmd = configure_cmd or "./configure"
        self.build_cmd = build_cmd or "make"
        self.source_name = source_name or name
        self.pre_build_cmd = pre_build_cmd

        self.package_dir = "packages"
        self.download_dir = "download"

        # By default, we assume the package creates an unpacked
        # directory whose name is the same as the tarball.  For
        # example, Wine's tarball is "Wine-xxxxxxx", but it unpacks to
        # "wine-xxxxxxxx".
        self.unpacked_subdir = unpacked_subdir or self.source_name
        self.build_subdir = build_subdir


    def register(self):
        trees[self.name] = self


    def __repr__(self):
        return "Project(name=%s)" % `self.name`


    def download(self):
        """Download package from vendor site"""

        make_dir(self.package_dir)
        make_dir(self.download_dir)
            
        if not os.path.isfile(os.path.join(self.package_dir, self.package_file)):
            # XXX: snarf gets upset if the HTTP server returns "416
            # Requested Range Not Satisfiable" because the file is already
            # totally downloaded.  This is kind of a snarf bug.
            print "** Downloading"
            run_cmd("cd %s && wget --continue %s" %
                    (self.download_dir, self.url))
            run_cmd("mv %s %s" %
                    (os.path.join(self.download_dir, self.package_file),
                     self.package_dir))

    def md5check(self):
        if self.md5:
            print "** Checking source package integrity"
            run_cmd("cd %s && echo '%s' | md5sum -c /dev/stdin" %
                    (self.package_dir, self.md5))


    def pre_actions(self, actions):
        """Perform actions preparatory to building according to selection."""
        
        if 'download' in actions:
            self.download()
        if 'md5check' in actions:
            self.md5check()
            
