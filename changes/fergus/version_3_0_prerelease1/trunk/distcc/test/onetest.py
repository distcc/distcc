#!/usr/bin/python2.4
#
# Copyright 2007 Google Inc. All Rights Reserved.

"""Usage: onetest.py [--valgrind[=command]] [--lzo] [--pump] TESTNAME

This command runs a single test case.
TESTNAME should be the name of one of the test cases from testdistcc.py.
"""

__author__ = 'fergus@google.com (Fergus Henderson)'

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
