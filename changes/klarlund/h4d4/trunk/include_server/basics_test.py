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
#

__author__ = "Nils Klarlund"

import os
import os.path
import unittest

import basics

class BasicsTest(unittest.TestCase):

  def setUp(self):

    basics.opt_debug_pattern = 1

  def tearDown(self):
    pass

  def test_InitializeClientTmp(self):
    os.environ['DISTCC_CLIENT_TMP'] = 'to/be'
    self.assertRaises(SystemExit, basics.InitializeClientTmp)
    os.environ['DISTCC_CLIENT_TMP'] = '/to/be/or'
    self.assertRaises(SystemExit, basics.InitializeClientTmp)
    try:
      os_mkdir = os.mkdir

      def Mock_os_mkdir(f, *args):
        if not f.startswith('/to/'):
          raise Exception, f
      os.mkdir = Mock_os_mkdir
      
      os.environ['DISTCC_CLIENT_TMP'] = '/to/be'
      basics.InitializeClientTmp()
      basics.InitializeClientRoot(1)
      self.assertEqual(os.path.dirname(basics.client_root), "/to/be")
      os.environ['DISTCC_CLIENT_TMP'] = '/to'
      basics.InitializeClientTmp()
      basics.InitializeClientRoot(2)
      self.assertEqual(os.path.dirname(
          os.path.dirname(basics.client_root)), "/to")
      self.assertEqual(os.path.basename(basics.client_root), "padding")
      self.assertEqual(len(
        [ None for ch in basics.client_root if ch == '/' ]), 3)
    finally:
      os.mkdir = os_mkdir
    
unittest.main()    
