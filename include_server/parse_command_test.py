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

"""Parsing of C and C++ commands and extraction of search paths."""

__author__ = "Nils Klarlund"

import os
import time

import basics
import cache_basics
import parse_command
import shutil
import tempfile
import unittest

NotCoveredError = basics.NotCoveredError

class ParseCommandUnitTest(unittest.TestCase):

  def setUp(self):

    basics.opt_debug_pattern = 1

    self.tmp = tempfile.mkdtemp()
    caches = cache_basics.SetUpCaches(self.tmp)

    self.includepath_map = caches.includepath_map
    self.canonical_path = caches.canonical_path
    self.directory_map = caches.directory_map
    self.realpath_map = caches.realpath_map
    self.systemdir_prefix_cache = caches.systemdir_prefix_cache

    mock_compiler = '/usr/crosstool/v8/gcc-4.1.0-glibc-2.2.2/blah/gcc'
    self.mock_compiler = mock_compiler
    mock_sysroot = '/usr/local/fake/sysroot'
    self.mock_sysroot = mock_sysroot

    def Mock_SetSystemDirsDefaults(compiler, sysroot, language, timer=None):
      if compiler != mock_compiler:
        raise Exception("compiler: %s, mock_compiler: %s" % (
          compiler, mock_compiler))
      if sysroot != mock_sysroot:
        raise Exception("sysroot: %s, mock_sysroot: %s" % (
          sysroot, mock_sysroot))

    self.compiler_defaults = lambda x: x
    self.compiler_defaults.SetSystemDirsDefaults =  Mock_SetSystemDirsDefaults
    self.compiler_defaults.system_dirs_default_all = []
    self.compiler_defaults.system_dirs_default = {}
    system_dirs_default = self.compiler_defaults.system_dirs_default
    system_dirs_default[mock_compiler] = {}
    system_dirs_default[mock_compiler][mock_sysroot] = {}
    system_dirs_default[mock_compiler][mock_sysroot]['c'] = []
    system_dirs_default[mock_compiler][mock_sysroot]['c++'] = []

  def tearDown(self):
    shutil.rmtree(self.tmp)

  def test__SplitMacroArg(self):
    self.assertEqual(parse_command._SplitMacroArg("="), ["="])
    self.assertEqual(parse_command._SplitMacroArg("A="), ["A", ""])
    self.assertEqual(parse_command._SplitMacroArg("A=B=C"), ["A", "B=C"])


  def _RetrieveDirectoriesExceptSys(self, directory_idxs):
    return cache_basics.RetrieveDirectoriesExceptSys(
      self.directory_map,
      self.realpath_map,
      self.systemdir_prefix_cache,
      directory_idxs)

  def test_ParseCommandLine(self):

    self.assertEqual(parse_command.ParseCommandLine(
        """   "a"b"\\"c"  "a"\n"b" a  b\\"c"""),
                     ['ab"c', 'a', 'b', 'a', 'b"c'])


    self.assertEqual(parse_command.ParseCommandLine(
      """this is a test"""),
      ['this', 'is', 'a', 'test'])
    self.assertEqual(parse_command.ParseCommandLine(
      """   this is a test"""),
      ['this', 'is', 'a', 'test'])
    self.assertEqual(parse_command.ParseCommandLine(
      """this is a test   """),
       ['this', 'is', 'a', 'test'])

    self.assertEqual(parse_command.ParseCommandLine(
      'this " is" a"test" '),
      ['this', ' is', 'atest'])

    self.assertEqual(parse_command.ParseCommandLine(
      r'this " \"is" a"test" '),
      ['this', ' "is', 'atest'])

    self.assertEqual(parse_command.ParseCommandLine(
      'this " is" a"test"'),
      ['this', ' is', 'atest'])

    self.assertRaises(NotCoveredError,
                      parse_command.ParseCommandLine,
                      """this is" a"test" """)
    self.assertRaises(NotCoveredError,
                      parse_command.ParseCommandLine,
                      'this is" a"test"')

  def test_ParseCommandArgs(self):

    quote_dirs, angle_dirs, include_files, filepath, _incl_clos_f, _d_opts = (
      parse_command.ParseCommandArgs(
        parse_command.ParseCommandLine(
          self.mock_compiler
          + " --sysroot=" + self.mock_sysroot
          + " -isystem system -Imice -iquote/and -I/men a.c "
          + " -include included_A.h "
          + " -includeincluded_B.h "
          + " -Wa,macros_A.s "
          + " -Wa,[macros_B.s] "
          + " -Wa,arch/x86/kernel/macros.s -Wa,- "
          + " -Wa,other_directive "
          + "-Xlinker W,l -L /ignored_by_us -o a.o"),
          os.getcwd(),
          self.includepath_map,
          self.directory_map,
          self.compiler_defaults))

    self.assertEqual(
      (self._RetrieveDirectoriesExceptSys(quote_dirs),
       self._RetrieveDirectoriesExceptSys(angle_dirs),
       [self.includepath_map.String(i) for i in include_files],
       filepath),
      (('/and', 'mice', '/men', 'system'),
       ('mice', '/men', 'system'),
       ["included_A.h", "included_B.h",
        "macros_A.s", "macros_B.s", "arch/x86/kernel/macros.s"],
       'a.c'))


    self.assertRaises(NotCoveredError,
                      parse_command.ParseCommandArgs,
                      parse_command.ParseCommandLine(
                        self.mock_compiler
                        + " --sysroot=" + self.mock_sysroot
                        + " -I- -iquote a.c"),
                      os.getcwd(),
                      self.includepath_map,
                      self.directory_map,
                      self.compiler_defaults)

    quote_dirs, angle_dirs, include_files, filepath, _incl_cls_file, _d_opts = (
      parse_command.ParseCommandArgs(parse_command.ParseCommandLine(
        "/usr/crosstool/v8/gcc-4.1.0-glibc-2.2.2/blah/gcc"
        + " --sysroot=/usr/local/fake/sysroot"
        + " -fno-exceptions -funsigned-char -D__STDC_FORMAT_MACROS -g0"
        + " -D_REENTRANT -DCOMPILER_GCC3 -DCOMPILER_GCC4 -DARCH_PIII -DOS_LINUX"
        + " -fmessage-length=0 -fno-strict-aliasing -fno-tree-vrp -D_REENTRANT"
        + " -DHAS_vsnprintf"
        + " -Iobj/gcc-4.1.0-glibc-2.2.2-piii-linux-g0-dbg/genfiles/third_party/libxml/third_party/libxml"
        + " -Ithird_party/zlib -iquote . -fno-strict-aliasing -c -o"
        + " obj/gcc-4.1.0-glibc-2.2.2-piii-linux-g0-dbg/bin/third_party/libxml/threads.c.o"
        + " third_party/libxml/threads.c"),
                                     os.getcwd(),
                                     self.includepath_map,
                                     self.directory_map,
                                     self.compiler_defaults))
    self.assertEqual(
      (self._RetrieveDirectoriesExceptSys(quote_dirs),
       self._RetrieveDirectoriesExceptSys(angle_dirs),
       filepath),
      (('',
        'obj/gcc-4.1.0-glibc-2.2.2-piii-linux-g0-dbg/genfiles/third_party/libxml/third_party/libxml',
        'third_party/zlib'),
       ('obj/gcc-4.1.0-glibc-2.2.2-piii-linux-g0-dbg/genfiles/third_party/libxml/third_party/libxml',
        'third_party/zlib'),
       'third_party/libxml/threads.c'))

unittest.main()
