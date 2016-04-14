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
        self.assertTrue((pat, dir)
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
          raise Exception(f)

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
      self.assertEqual(os.path.dirname(
          os.path.dirname(client_root_keeper.client_root)), "/to")
      self.assertEqual(os.path.basename(client_root_keeper.client_root),
                       "padding")
      self.assertEqual(len(
        [ None for ch in client_root_keeper.client_root if ch == '/' ]), 3)
    finally:
      tempfile.mkdtemp = tempfile_mkdtemp
      os.makedirs = os_makedirs


  def test_ClientRootKeeper_Deletions(self):
    """Test whether directories emerge and go away appropriately."""

    # Test with a one-level value of DISTCC_CLIENT_TMP.
    os.environ['DISTCC_CLIENT_TMP'] = '/tmp'
    client_root_keeper = basics.ClientRootKeeper()
    client_root_keeper.ClientRootMakedir(117)
    self.assertTrue(os.path.isdir(client_root_keeper._client_root_before_padding))
    self.assertTrue(os.path.isdir(client_root_keeper.client_root))
    self.assertTrue(client_root_keeper.client_root.endswith('/padding'))
    client_root_keeper.ClientRootMakedir(118)
    client_root_keeper.CleanOutClientRoots()
    # Directories must be gone now!
    self.assertTrue(not os.path.isdir(
        client_root_keeper._client_root_before_padding))
    # Test with a two-level value of DISTCC_CLIENT_TMP.
    try:
      os.environ['DISTCC_CLIENT_TMP'] = tempfile.mkdtemp('basics_test',
                                                         dir='/tmp')
      client_root_keeper = basics.ClientRootKeeper()
      client_root_keeper.ClientRootMakedir(117)
      self.assertTrue(os.path.isdir(
          client_root_keeper._client_root_before_padding))
      self.assertTrue(os.path.isdir(client_root_keeper.client_root))
      client_root_keeper.ClientRootMakedir(118)
      client_root_keeper.CleanOutClientRoots()
      self.assertTrue(os.path.isdir,
                   client_root_keeper._client_root_before_padding)
    finally:
      os.rmdir(os.environ['DISTCC_CLIENT_TMP'])

unittest.main()
