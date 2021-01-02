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

__author__ = "Nils Klarlund"

import os
import os.path

import basics
import cache_basics
import mirror_path
import shutil
import tempfile
import unittest

NotCoveredError = basics.NotCoveredError


class MirrorPathTest(unittest.TestCase):

  """We construct a mock-up world of a file system in order to
  unittest the DoPath function of mirror_path."""


  def setUp(self):

    basics.debug_pattern = 3
    self.tmp = tempfile.mkdtemp()
    caches = cache_basics.SetUpCaches(self.tmp)

    self.canonical_path = caches.canonical_path
    self.simple_build_stat = caches.simple_build_stat
    self.mirror_path = mirror_path.MirrorPath(self.simple_build_stat,
                                              self.canonical_path,
                                              caches.realpath_map,
                                              caches.systemdir_prefix_cache)

    self.directories = ['/', '/a', '/link', '/a/link', '/a/b',
                        '/link/link', '/root']
    self.links = ['/a/link', '/link', '/link/link']
    self.exists = self.directories + self.links
    self.realpaths = {'/'         :'/',
                      '/a'        :'/a',
                      '/a/link'   :'/a/b',
                      '/link'     :'/a',
                      '/link/link':'/a/b'}


  def tearDown(self):
    shutil.rmtree(self.tmp)

  def test_MirrorPath(self):

    try:

      def isdir(path):
        return path in self.directories
      def exists(path):
        return path in self.exists
      def islink(path):
        return path in self.links
      def realpath(path):
        if path.startswith('/root'):
          self.fail("Not expected that '%s' started with '/root'." % path)
        return self.realpaths[path]
      def makedirs(path):
        if path == '/root/a':
          self.directories.extend(['/root/a'])
          self.exists.extend(['/root/a'])
        else:
          self.fail("makedirs %s" % path)
      def symlink(src, dest):
        if not (src, dest) in [ ('/a', '/root/link'),
                                ('/a/b', '/root/a/link') ]:
          self.fail("symlink %s %s" % (src, dest))
        self.links.append(dest)
        self.exists.append(dest)
      # Overwrite the canonicalization function that MirrorPath uses.
      self.mirror_path.canonical_path.Canonicalize = realpath
      # Overwrite various system functions that MirrorPath uses.
      isdir_ = os.path.isdir
      os.path.isdir = isdir
      exists_ = os.path.exists
      os.path.exists = exists
      islink_ = os.path.islink
      os.path.islink = islink
      makedirs_ = os.makedirs
      os.makedirs = makedirs
      symlink_ = os.symlink
      os.symlink = symlink

      # Mirror the link /a/link.
      self.mirror_path.DoPath('/a/link', 117, '/root')
      self.assertEqual(self.mirror_path.Links(),  ['/root/a/link'])
      self.assertTrue(self.simple_build_stat.Lookup('/root/a'))

      # Check that symlink function is not called again, by verifying
      # that mirror_path.Links() doesn't grow.
      self.mirror_path.DoPath('/a/link', 117, '/root')
      self.assertEqual(self.mirror_path.Links(), ['/root/a/link'])

      # Now mirror /link/link.
      self.mirror_path.DoPath('/link/link',  117, '/root')
      self.assertEqual(self.mirror_path.Links(), ['/root/a/link', '/root/link'])
      self.assertEqual(
        [ d for d in self.directories if d.startswith('/root') ],
        [ '/root', '/root/a' ])
      self.assertEqual(
        [ d for d in self.links if d.startswith('/root') ],
        [ '/root/a/link', '/root/link' ])

      # Now mirror /a/b. Since b is a file and /a already is mirrored,
      # there is no effect.
      self.mirror_path.DoPath('/a/b',  117, '/root')
      self.assertEqual(self.mirror_path.Links(), ['/root/a/link', '/root/link'])
      self.assertEqual(
        [ d for d in self.directories if d.startswith('/root') ],
        [ '/root', '/root/a' ])
      self.assertEqual(
        [ d for d in self.links if d.startswith('/root') ],
        [ '/root/a/link', '/root/link' ])

    finally:
      try:
        # Don't propagate another exception.
        os.path.isdir = isdir_
        os.path.exists = exists_
        os.path.islink = islink_
        os.makedirs = makedirs_
        os.symlink = symlink_
      except NameError:
        pass

unittest.main()
