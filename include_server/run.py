#! /usr/bin/env python3

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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
# USA.

"""Run with PYTHONPATH including appropriate place for extension module."""

__author__ = "opensource@google.com"

import os
import sys
import glob


USAGE="""Usage: run.py [--install] CMD [ARG...]

Option:

 --run_in_install:  find extension module under lib of installation directory

Locate a Python extension module in some directory D under the
c_extensions/build subdirectory of the directory that contains the present
command. With --run_in_install, assume that this script resides in an installed
version; this means that the directory structure is different and D is located
as ../../lib.

Then run CMD [ARG...] with environment variable PYTHONPATH augmented with D.

Normally, print out a message where the extension module is found. But, with
--run_in_install this message is suppressed.

Examples:

From anywhere:
  # Start include server.
  /home/distcc/include_server/run.py include_server.py

In the include_server directory:
  # Run include_server tests.
  ./run.py include_server_test.py
  # Pycheck include_server.
  ./run.py `which pychecker` include_server.py

In installed distcc-pump:
   # See 'pump' script.
   $include_server_location/run.py --run_in_install include_server.py ..
"""

def usage():
  print(USAGE)
  sys.exit(1)

DEFAULT_PATH = "c_extensions/build/lib.*/*"

cmd = sys.argv[0]
if len(sys.argv) < 2: usage()

dirname = os.path.dirname(cmd)
directory = os.path.abspath(os.path.join(os.getcwd(), dirname))

# Define lib_directory, the directory of the .so, one way or another.
if sys.argv[1]== '--run_in_install':
  del sys.argv[1]
  if len(sys.argv) < 2: usage()
  # We are in share/python.
  lib_directory = os.path.join(dirname, "../../lib")
else:
  # We're in the source directory, not in installation.
  place_to_look = directory + '/' + DEFAULT_PATH
  potential_libs = glob.glob(place_to_look)
  # Now potential_libs is supposed to contain the filepaths of dynamically
  # loaded libraries. We expect exactly one such filepath.
  if len(potential_libs) == 0:
    sys.exit("No extension modules of the form '%s' found." %
             place_to_look)
  if len(potential_libs) > 1:
    sys.exit("More than one extension module found. "
             + " Cannot determine which one to use.")
  lib_directory = os.path.dirname(potential_libs[0])
  print("__________Using Python extension in %s" % lib_directory)

# Now, the all important change to PYTHONPATH. Note that we obliterate any
# environmental setting setting as well. This improves performance in
# installations with unneeded Python resources on network disks.
os.environ['PYTHONPATH'] = lib_directory

try:
  os.execv(os.path.join(directory, sys.argv[1]), sys.argv[1:])
except OSError:
  print("Could not run: '%s' with arguments: %s" %
    (os.path.join(directory, sys.argv[1]),
     sys.argv[1:]), file=sys.stderr)
