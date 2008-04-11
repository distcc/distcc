#! /usr/bin/python2.4

# Copyright 2007 Google Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

"""Build the include server module"""

__author__ = "Manos Renieris"

import distutils
import os
from distutils.core import setup
from distutils.extension import Extension

ext = Extension(
    name="include_server.distcc_pump_c_extensions",
    sources=[
             '../distcc/src/clirpc.c',
             '../distcc/src/clinet.c',
             '../distcc/src/state.c',
             '../distcc/src/srvrpc.c',
             '../distcc/src/pump.c',
             '../distcc/src/rpc.c',
             '../distcc/src/io.c',
             '../distcc/src/include_server_if.c',
             '../distcc/src/trace.c',
             '../distcc/src/util.c',
             '../distcc/src/tempfile.c',
             '../distcc/src/filename.c',
             '../distcc/src/bulk.c',
             '../distcc/src/sendfile.c',
             '../distcc/src/compress.c',
             '../distcc/src/argutil.c',
             '../distcc/src/cleanup.c',
             '../distcc/src/emaillog.c',
             '../distcc/src/timeval.c',
             '../distcc/src/netutil.c',
             '../distcc/lzo/minilzo.c',
             'c_extensions/distcc_pump_c_extensions_module.c',
             ],
    include_dirs = ["../distcc/src", 
                    "../distcc/lzo",
                    os.path.join(os.getenv("BUILDDIR") or "", 
                                 "../distcc/src"),
                    os.path.join(os.getenv("BUILDDIR") or "", 
                                 "../../distcc/src"),
                   ],
    define_macros = [('_GNU_SOURCE', 1)],
    library_dirs = [],
    libraries = [],
    runtime_library_dirs = [],
    extra_objects = [],
    extra_compile_args = ['-Wall', '-Wextra', '-Werror'],
    extra_link_args = ['-Wall', '-Wextra', '-Werror'],
)

# TODO(csilvers): get the version number from configure.ac
args = {
  'name': "include_server",
  'package_dir': {'include_server':'.'},
  'version': '1.00',
  'description': "Include server for distcc-pump",
  'author': "Nils Klarlund",
  'author_email': "opensource@google.com",
  'url': 'http://code.google.com/p/distcc-pump',
  'long_description': """The include server is part of distcc-pump.""",
  'packages': ["include_server"],
  'ext_modules': [ext],
}

setup(**args)
