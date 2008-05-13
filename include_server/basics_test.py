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
import tempfile
import unittest

import basics

class BasicsTest(unittest.TestCase):

  def setUp(self):

    basics.opt_debug_pattern = 1

  def tearDown(self):
    pass

  def test_ClientRootKeeper(self):
    os.environ['DISTCC_CLIENT_TMP'] = 'to/be'
    self.assertRaises(SystemExit, basics.ClientRootKeeper)
    os.environ['DISTCC_CLIENT_TMP'] = '/to/be/or'
    self.assertRaises(SystemExit, basics.ClientRootKeeper)
    try:
      tempfile_mkdtemp = tempfile.mkdtemp
      os_makedirs = os.makedirs

      def Mock_tempfile_mkdtemp(pat, dir):
        self.assert_((pat, dir)
                     in
                     [('.%s-%s-%d' %
                       (basics.ClientRootKeeper.INCLUDE_SERVER_NAME,
                        os.getpid(), generation),
                       prefix)
                      for generation, prefix in
                      [(1,'/to/be'), (2, '/to')]])
        return (dir == '/to/be' and '/to/be/xxxxxx'
                or dir == '/to' and '/to/xxxxxxx')
      
      def Mock_os_makedirs(f, *unused_args):
        if not f.startswith('/to/'):
          raise Exception, f
        
      tempfile.mkdtemp = Mock_tempfile_mkdtemp
      os.makedirs = Mock_os_makedirs

      
      os.environ['DISTCC_CLIENT_TMP'] = '/to/be'
      client_root_keeper = basics.ClientRootKeeper()
      client_root_keeper.ClientRootMakedir(1)
      self.assertEqual(os.path.dirname(client_root_keeper.client_root),
                       "/to/be")
      os.environ['DISTCC_CLIENT_TMP'] = '/to'
      client_root_keeper = basics.ClientRootKeeper()
      client_root_keeper.ClientRootMakedir(2)
      print 'xxxxxxxxxxxx', client_root_keeper.client_root
      self.assertEqual(os.path.dirname(
          os.path.dirname(client_root_keeper.client_root)), "/to")
      self.assertEqual(os.path.basename(client_root_keeper.client_root),
                       "padding")
      self.assertEqual(len(
        [ None for ch in client_root_keeper.client_root if ch == '/' ]), 3)
    finally:
      tempfile.mkdtemp = tempfile_mkdtemp
      os.makedirs = os_makedirs
    
unittest.main()    
