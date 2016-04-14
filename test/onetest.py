#!/usr/bin/env python3
#
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

"""Usage: onetest.py [--valgrind[=command]] [--lzo] [--pump] TESTNAME

This command runs a single test case.
TESTNAME should be the name of one of the test cases from testdistcc.py.
"""

__author__ = 'Fergus Henderson'

import testdistcc
import comfychair
import sys

if __name__ == '__main__':
  while len(sys.argv) > 1 and sys.argv[1].startswith("--"):
    if sys.argv[1] == "--valgrind":
      testdistcc._valgrind_command = "valgrind --quiet "
      del sys.argv[1]
    elif sys.argv[1].startswith("--valgrind="):
      testdistcc._valgrind_command = sys.argv[1][len("--valgrind="):] + " "
      del sys.argv[1]
    elif sys.argv[1] == "--lzo":
      testdistcc._server_options = ",lzo"
      del sys.argv[1]
    elif sys.argv[1] == "--pump":
      testdistcc._server_options = ",lzo,cpp"
      del sys.argv[1]

  if len(sys.argv) > 1:
    testname = sys.argv[1]
    del sys.argv[1]
    comfychair.main([eval('testdistcc.' + testname)])
  else:
    sys.exit(__doc__)
