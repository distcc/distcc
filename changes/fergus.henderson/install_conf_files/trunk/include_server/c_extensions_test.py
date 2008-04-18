#!/usr/bin/python2.4

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
# 

"""Tests for distcc_pump_c_extensions. Writes out doc strings and calls some
distcc rpc functions. Also, the program times the speed-up of using the libc
versions of os.path.realpath and os.path.exists provided by
distcc_pump_c_extensions."""

__author__ = "opensource@google.com"

import sys
import os.path
import time
import traceback
import struct
import random

import distcc_pump_c_extensions


def main():

  # Module tempfile doesn't work with distcc. Work-around follows.
  random_filename = "distcc-pump" + str(random.random() * time.time())
  if os.path.exists(random_filename):
    print sys.stderr >> (
      """"For unfathomably unlikely reasons, this test failed: '%s' exists."""
      % random_filename)
    sys.exit(1)
  def _MakeTempFile(mode):
    return open(random_filename, mode)

  # Exercise metainformation and documentation strings
  assert(distcc_pump_c_extensions.__file__)
  assert(distcc_pump_c_extensions.__doc__)
  assert(distcc_pump_c_extensions.__author__)
  assert(distcc_pump_c_extensions.RTokenString.__doc__)
  assert(distcc_pump_c_extensions.RArgv.__doc__)
  assert(distcc_pump_c_extensions.XArgv.__doc__)
  assert(distcc_pump_c_extensions.OsPathExists.__doc__)
  assert(distcc_pump_c_extensions.OsPathIsFile.__doc__)
  assert(distcc_pump_c_extensions.Realpath.__doc__)


  # RTokenString and RArgv
  
  # Pack something and try sending it
  pack = struct.pack
  fd = _MakeTempFile('wb')
  fd.write("ARGC       2")
  fd.write("ARGV       6")
  fd.write("tomato")
  fd.write("ARGV       7")
  fd.write("potatos")
  fd.close()

  # Now try to read it back with wrong expectations.
  fd = _MakeTempFile('rb')
  try:
    two_string = distcc_pump_c_extensions.RTokenString(fd.fileno(), "XXXX");
    sys.exit("internal error 1 - we should not get to here")
  except distcc_pump_c_extensions.Error:
    pass

  # Read it back with appropriate expectations.
  fd.seek(0);

  two_string = distcc_pump_c_extensions.RTokenString(fd.fileno(), "ARGC");
  if two_string != "AR":
    raise distcc_pump_c_extensions.error, "internal error 2"

  fd.seek(0);
  args = distcc_pump_c_extensions.RArgv(fd.fileno())
  if args != ["tomato", "potatos"]:
    raise distcc_pump_c_extensions.error, "internal error 3"
  fd.close()

  # XArgv and RArgv

  fd = _MakeTempFile('wb')
  darth_vader_barney = ["Darth Vader", "Barney"]
  args = distcc_pump_c_extensions.XArgv(fd.fileno(), darth_vader_barney)
  fd.close()

  fd = _MakeTempFile('r')
  args = distcc_pump_c_extensions.RArgv(fd.fileno())
  if args != darth_vader_barney:
    raise distcc_pump_c_extensions.error, "internal error 4"
  fd.close()

  # Libc functions --- also print out how fast they are compared to
  # Python built-ins.  
  t = time.time()
  f = "/"
  for i in range(10000):
    distcc_pump_c_extensions.OsPathExists(f);
  print 'Stat', time.time() - t
  t = time.time()
  for i in range(10000):
    os.path.exists(f);
  print 'os.path.exists', time.time() - t
  for i in range(10000):
    distcc_pump_c_extensions.Realpath(f);
  print 'c_realpath', time.time() - t
  t = time.time()
  for i in range(10000):
    os.path.realpath(f);
  print 'os.path.realpath', time.time() - t

  print "Test passed"

try:    
  main()
except:
  traceback.print_exc()
  sys.exit("c_extensions_test.py failed.")
