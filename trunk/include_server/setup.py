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

"""Build the include server module.

Note: the version number should be passed to this script through
the environment variable DISTCC_VERSION.
"""

__author__ = "Manos Renieris"

import distutils
import os
from distutils.core import setup
from distutils.extension import Extension

ext = Extension(
    name="include_server.distcc_pump_c_extensions",
    sources=[
             '../src/clirpc.c',
             '../src/clinet.c',
             '../src/state.c',
             '../src/srvrpc.c',
             '../src/pump.c',
             '../src/rpc.c',
             '../src/io.c',
             '../src/include_server_if.c',
             '../src/trace.c',
             '../src/util.c',
             '../src/tempfile.c',
             '../src/filename.c',
             '../src/bulk.c',
             '../src/sendfile.c',
             '../src/compress.c',
             '../src/argutil.c',
             '../src/cleanup.c',
             '../src/emaillog.c',
             '../src/timeval.c',
             '../src/netutil.c',
             '../lzo/minilzo.c',
             'c_extensions/distcc_pump_c_extensions_module.c',
             ],
    include_dirs = ["../src", 
                    "../lzo",
                    os.path.join(os.getenv("BUILDDIR") or "", 
                                 "src"),
                    os.path.join(os.getenv("BUILDDIR") or "", 
                                 "../src"),
                    os.path.join(os.getenv("BUILDDIR") or "", 
                                 "../../src"),
                   ],
    define_macros = [('_GNU_SOURCE', 1)],
    library_dirs = [],
    libraries = [],
    runtime_library_dirs = [],
    extra_objects = [],
    extra_compile_args = ['-Wall', '-Wextra', '-Werror'],
    extra_link_args = ['-Wall', '-Wextra', '-Werror'],
)

args = {
  'name': "include_server",
  'package_dir': {'include_server':'.'},
  'version': os.getenv("DISTCC_VERSION") or 'unknown',
  'description': "Include server for distcc-pump",
  'author': "Nils Klarlund",
  'author_email': "opensource@google.com",
  'url': 'http://code.google.com/p/distcc-pump',
  'long_description': """The include server is part of distcc-pump.""",
  'packages': ["include_server"],
  'ext_modules': [ext],
}

setup(**args)
