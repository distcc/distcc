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

The version number should be passed to this script through the environment
variable DISTCC_VERSION.  Also, the CPPFLAGS of the Makefile must be passed
through the enviroment.  This is how we figure out the locations of include
directories. SRCDIR must be passed as well; it explains where to find the C
sources and the include_server directory of Python source files and C
extensions.  Because SRCDIR is appended to build location and we don't want to
end up outside this location through a relative SRCDIR path with '..'s, SRCDIR
should be absolute.
"""

__author__ = "Manos Renieris and Nils Klarlund"

import distutils
import os
import re
import sys
from distutils.core import setup
from distutils.extension import Extension

# For removing the quotes at the beginning and end of a string.
PURGE_QUOTES_RE = re.compile(r'^"(.*)"$')

# CPPFLAGS checking.
cpp_flags_env = os.getenv("CPPFLAGS", "")
if not cpp_flags_env:
  # Don't quit; perhaps the user is asking for help using '--help'.
  print >> sys.stderr, "setup.py: CPPFLAGS must be defined."
cpp_flags = cpp_flags_env.split()
# Fish out the directories of '-I' options.
cpp_flags_includes = [PURGE_QUOTES_RE.sub(r'\1', flag[2:])
                      for flag in cpp_flags
                      if flag[:2]=='-I']

# SRCDIR checking.
if not os.getenv('SRCDIR'):
  # Don't quit; perhaps the user is asking for help using '--help'.
  print >> sys.stderr, ("setup.py: SRCDIR must be defined.")
  srcdir = 'UNDEFINED'
  srcdir_include_server = 'UNDEFINED'
else:
  srcdir = os.getenv('SRCDIR') 
  srcdir_include_server = srcdir + '/include_server'

# Specify extension.
ext = Extension(
    name="include_server.distcc_pump_c_extensions",
    sources=[srcdir + '/' + source
             for source in
             ['src/clirpc.c',
              'src/clinet.c',
              'src/state.c',
              'src/srvrpc.c',
              'src/pump.c',
              'src/rpc.c',
              'src/io.c',
              'src/include_server_if.c',
              'src/trace.c',
              'src/util.c',
              'src/tempfile.c',
              'src/filename.c',
              'src/bulk.c',
              'src/sendfile.c',
              'src/compress.c',
              'src/argutil.c',
              'src/cleanup.c',
              'src/emaillog.c',
              'src/timeval.c',
              'src/netutil.c',
              'lzo/minilzo.c',
              'include_server/c_extensions/distcc_pump_c_extensions_module.c',
              ]],
    include_dirs=cpp_flags_includes,
    define_macros=[('_GNU_SOURCE', 1)],
    library_dirs=[],
    libraries=[],
    runtime_library_dirs=[],
    extra_objects=[],
    # This is the same list as is in configure.ac, except we leave out
    # -Wmissing-prototypes and -Wmissing-declarations, which don't apply
    # to python extensions (it exports global fns via a pointer),
    # and -Wwrite-strings, which just had too many false positives.
    extra_compile_args=("-W -Wall -Wimplicit -Wuninitialized "
                        "-Wshadow -Wpointer-arith -Wcast-align "
                        "-Waggregate-return -Wstrict-prototypes "
                        "-Wnested-externs -Werror").split())

args = {
  'name': "include_server",
   # The 'include_server' package is in the srcdir_include_server.
  'package_dir': {'include_server': srcdir_include_server},
  'version': os.getenv("DISTCC_VERSION") or 'unknown',
  'description': "Include server for distcc's pump-mode",
  'author': "Nils Klarlund",
  'author_email': "opensource@google.com",
  'url': 'http://code.google.com/p/distcc',
  'long_description': """The include server is part of distcc.""",
  'packages': ["include_server"],
  'ext_modules': [ext],
}

setup(**args)
