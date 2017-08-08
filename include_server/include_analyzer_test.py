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

"""Test include analysis, including computed includes, as carried out
by ProcessCompilationCommandLine. Also, test stat_reset_triggers."""

__author__ = "Nils Klarlund"

import os
import re
import glob
import shutil
import tempfile
import unittest

import basics
import cache_basics
import parse_command
import statistics
import include_analyzer_memoizing_node

class IncludeAnalyzerTest(unittest.TestCase):

  def setUp(self):

    statistics.StartTiming()
    self.global_dirs = []
    basics.opt_print_statistics = False
    basics.opt_debug_pattern = 1
    client_root_keeper = basics.ClientRootKeeper()
    if algorithm == basics.MEMOIZING:
      self.include_analyzer = (
        include_analyzer_memoizing_node.IncludeAnalyzerMemoizingNode(
            client_root_keeper))
    else:
      self.fail("Algorithm not known.")

    statistics.StartTiming()

    self.directory_map = self.include_analyzer.directory_map
    self.compiler_defaults = self.include_analyzer.compiler_defaults
    self.canonical_path = self.include_analyzer.canonical_path

  def tearDown(self):
    if basics.opt_print_statistics:
      statistics.EndTiming()
      statistics.PrintStatistics(self.include_analyzer)

  def ProcessCompilationCommandLine(self, cmd, cwd):
    return (
      self.include_analyzer.ProcessCompilationCommand(
        cwd,
        parse_command.ParseCommandArgs(
          parse_command.ParseCommandLine(cmd),
          cwd,
          self.include_analyzer.includepath_map,
          self.include_analyzer.directory_map,
          self.include_analyzer.compiler_defaults)))

  def CanonicalPathsForTestData(self, dirs, test_data_dir='test_data'):
    """Absolutize names relative to test_data_dir of current directory."""
    return set([ self.canonical_path.Canonicalize(test_data_dir + '/' + f)
                 for f in dirs ])


  def RetrieveCanonicalPaths(self, files):
    return set([ self.include_analyzer.realpath_map.string[f] for f in files ])


  def test_AdvancedComputedIncludes(self):

    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        "gcc test_data/test_computed_includes/src.c",
        os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(['test_computed_includes/src.c',
                                      'test_computed_includes/helper.c',
                                      'test_computed_includes/incl.h']))

    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        "gcc test_data/test_computed_includes/srcA.c",
        os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(['test_computed_includes/srcA.c',
                                      'test_computed_includes/helper.c',
                                      'test_computed_includes/incl.h',
                                      'test_computed_includes/inclA.h']))

    # Test: FindNode is called once only if previous query is repeated. That is,
    # include graph is not calculated for this query.
    old_FindNode = self.include_analyzer.FindNode
    class mock_FindNode(object):
      def __init__(self):
        self.count = 0
      def FindNode(self, *_):
        self.count += 1
        if self.count == 2:
          raise Exception("Did not expect 2 calls of FindNode.")
        return old_FindNode(*_)

    self.include_analyzer.FindNode = mock_FindNode().FindNode
    try:
      includes = self.RetrieveCanonicalPaths(
        self.ProcessCompilationCommandLine(
        "gcc test_data/test_computed_includes/srcA.c",
        os.getcwd()))
    finally:
      self.include_analyzer.FindNode = old_FindNode

    # Test: if a -D option affecting the value of the computed include is
    # presented then include graph is recalculated --- and correctly so.
    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        r"""gcc -DINCL=\"../dfoo/foo2.h\" """ +
        " test_data/test_computed_includes/src.c",
        os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(['test_computed_includes/src.c',
                                      'test_computed_includes/helper.c',
                                      'test_computed_includes/incl.h',
                                      'test_computed_includes/inclA.h',
                                      'dfoo/foo2.h']))


    # Test: functional macros can be passed on the command line.
    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        """gcc  -D"STR(X)=# X" """
        + """-D"FINCLUDE(P)=STR(../MY_TEST_DATA/dfoo/P)" """
        + """-DMY_TEST_DATA=test_data """
        + "test_data/func_macro.c",
        os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['func_macro.c',
         'dfoo/foo.h',
         'dfoo/foo2.h',
         'dbar/dbar1/bar.h'
         ]))


  def test_AbsoluteIncludes(self):

    try:
      opt_unsafe_absolute_includes = basics.opt_unsafe_absolute_includes
      basics.opt_unsafe_absolute_includes = True

      tmp_dir = tempfile.mkdtemp()

      def WriteProgram(name, text):
        base = os.path.dirname(os.path.join(tmp_dir, name))
        if not os.path.isdir(base):
          os.makedirs(base)
        fd = open(os.path.join(tmp_dir, name), 'w')
        fd.write(text)
        fd.close()

      WriteProgram('foo.c',
                   """#include "%s/bar/baz.h"
                      blah blah.
                   """ % tmp_dir)

      # We don't want to get entangled in absolute includes.
      # The baz.h program is not supposed to be found (although
      # real preprocessing will).

      WriteProgram('baz.h',
                   """#include "../foobar.h"
                      blah blah.
                   """)

      includes = self.RetrieveCanonicalPaths(
          self.ProcessCompilationCommandLine(
          "gcc %s" % os.path.join(tmp_dir, 'foo.c'),
          os.getcwd()))

      self.assertEqual(
          includes,
          set([os.path.realpath('%s/foo.c' % tmp_dir)]))

    finally:
      basics.opt_unsafe_absolute_includes = opt_unsafe_absolute_includes
      shutil.rmtree(tmp_dir)

  def test_StatResetTriggers(self):

    """Check that the include analysis of a file is done from scratch after a
    trigger path went from non-existing to existing.
    """

    def CheckGeneration(lst, expected):
      for f_name in lst:
        self.assertTrue(
            re.match(r"%s/.+[.]include_server[-][0-9]+[-]%s"
                     % (self.include_analyzer.client_root_keeper.client_tmp,
                        expected),
                     f_name),
            f_name)

    def GetFileNamesFromAbsLzoName(lst):
      """Transform lists with elements like:
        '/dev/shm/tmpsn6NQT.include_server-12272-X/.../test_data/foo.c.lzo'
      to lists with elements like:
        'test_data/foo.c'"""
      return [  f_name.split('/')[-2]
                + '/'
                + f_name.split('/')[-1][:-4]
                for f_name in lst if f_name.endswith('.lzo') ]

    self.include_analyzer.stat_reset_triggers = {"seven*": {},
                                                 "ate": {"ate": (1,111,2)},
                                                 "nine": {} }

    try:

      real_glob_glob = glob.glob
      def Mock_GlobGlob(f):
        if f in ["seven*", "nine"]: return []
        if f == 'ate':  return ["ate"]
        return real_glob_glob(f)
      glob.glob = Mock_GlobGlob

      real_os_stat = os.stat
      def Mock_OsStat(f, dir_fd=None, follow_symlinks=True):
        # Return the same as initial value in two cases below.
        if f in ["seven", "nine"]: raise OSError
        if f == 'ate':
          obj = lambda: None
          obj.st_mtime = 1
          obj.st_ino = 111
          obj.st_dev = 2
          return obj
        return real_os_stat(f)
      os.stat = Mock_OsStat

      real_cache_basic_OsPathIsFile = cache_basics._OsPathIsFile
      def Mock_OsPathIsFile(f):
        # We postulate that the test_data/stat_reset_triggers.h file does not
        # yet exists. Moreover, we pretend that a version ind test_data/dfoo is
        # in existence.
        return f in [ "test_data/stat_triggers.c",
                      "test_data/dfoo/stat_triggers.h"]
      cache_basics._OsPathIsFile = Mock_OsPathIsFile

      files_and_links = self.include_analyzer.DoCompilationCommand(
        "gcc -Itest_data/dfoo test_data/stat_triggers.c".split(),
        os.getcwd(),
        self.include_analyzer.client_root_keeper)

      # Check that we picked up the dfoo version of the .h file!
      self.assertEqual(GetFileNamesFromAbsLzoName(files_and_links),
                       ['test_data/stat_triggers.c',
                        'dfoo/stat_triggers.h'])

      # The generation should still be the original, namely 1.
      self.assertEqual(self.include_analyzer.generation, 1)
      CheckGeneration(files_and_links, 1)

      def New_Mock_OsStat(f, dir_fd=None, follow_symlinks=True):
        if f in ["seven", "nine"]: raise OSError
        if f == 'ate':
          obj = lambda: None
          obj.st_mtime = 1
          obj.st_ino = 111
          obj.st_dev = 3  # so, this component changed from previous value
          return obj
        return real_os_stat(f)

      os.stat = New_Mock_OsStat

      def New_Mock_OsPathIsFile(f):
        return f in [ "test_data/stat_triggers.c",
                      "test_data/stat_triggers.h",
                      "test_data/dfoo/stat_triggers.h"]
      cache_basics._OsPathIsFile = New_Mock_OsPathIsFile

      files_and_links = self.include_analyzer.DoCompilationCommand(
        "gcc -Itest_data/dfoo test_data/stat_triggers.c".split(),
        os.getcwd(),
        self.include_analyzer.client_root_keeper)

      self.assertEqual(self.include_analyzer.generation, 2)
      CheckGeneration(files_and_links, 2)

      # Now, check that we picked up the test_data version of the .h file, not
      # the dfoo one!
      self.assertEqual(GetFileNamesFromAbsLzoName(files_and_links),
                       ['test_data/stat_triggers.c',
                        'test_data/stat_triggers.h'])

      # Third time.
      def New_Mock_GlobGlob(f):
        if f in ["seven*"]: return ["seventy"]
        if f in ["nine"]: return []
        if f == 'ate':  return ["ate"]
        return real_glob_glob(f)
      glob.glob = New_Mock_GlobGlob

      def New_New_Mock_OsStat(f, dir_fd=None, follow_symlinks=True):
        if f in ["seven", "nine"]: raise OSError
        if f == 'ate':
          obj = lambda: None
          obj.st_mtime = 1
          obj.st_ino = 111
          obj.st_dev = 3
          return obj
        if f == 'seventy':
          obj = lambda: None
          obj.st_mtime = 2
          obj.st_ino = 222
          obj.st_dev = 3
          return obj
        return real_os_stat(f)
      os.stat = New_New_Mock_OsStat

      # Revert _OsPathIsFile
      cache_basics._OsPathIsFile = Mock_OsPathIsFile

      files_and_links = self.include_analyzer.DoCompilationCommand(
        "gcc -Itest_data/dfoo test_data/stat_triggers.c".split(),
        os.getcwd(),
        self.include_analyzer.client_root_keeper)

      # Now, check that we again picked up the dfoo version of the .h file.
      self.assertEqual(GetFileNamesFromAbsLzoName(files_and_links),
                       ['test_data/stat_triggers.c',
                        'dfoo/stat_triggers.h'])

      self.assertEqual(self.include_analyzer.generation, 3)
      CheckGeneration(files_and_links, 3)

    finally:
      glob.glob = real_glob_glob
      os.stat = real_os_stat
      cache_basics._OsPathIsFile = real_cache_basic_OsPathIsFile


  def test_DotdotInInclude(self):
    """Set up tricky situation involving an "#include "../foo" occurring in a
    file accessed through a symbolic link.  This include is to be resolved
    relative to the file directory, namely where the link is, not where the file
    is.  Also, the include processor tries to identify files by their
    relatives names. This may lead to an infinite recursion according to an
    increasing sequence of filepath names involving "../" So if the included
    file includes the original file, an infinite loop may occur."""

    # In test_data, we find
    #
    #   symlink_farm/sub_farm/link_to_dd_dd_dfoo_include_dotdot_foo
    #
    # which is a link to ../../dfoo/include_dotdot_foo, that is, to
    #
    #   dfoo/include_dotdot_foo
    #
    # which is a file that contains:
    #
    #   #include "../foo"
    #
    # Here ../foo must refer to the file in location symlink_farm/sub_farm/foo,
    # which we have made a real file (not a symlink).
    #
    # We check that this "foo" is included. Moreover, we have put:
    #
    #   #include "sub_farm/link_to_dd_dd_dfoo_include_dotdot_foo"
    #
    #  into symlink_farm/sub_farm/foo, so that a nice infinite inclusion chain
    #  is modeled. That should not faze the include server. In particular, we
    #  do not want to see an infinitude of paths of the form:
    #
    #      symlink_farm/sub_farm/../sub_farm/../
    #      ...
    #      /sub_farm/../sub_farm/foo

    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        "gcc -xc test_data/symlink_farm/sub_farm/"
        + "link_to_dd_dd_dfoo_include_dotdot_foo",
        os.getcwd()))
    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['dfoo/include_dotdot_foo', 'symlink_farm/foo'],
        "test_data"))


  def helper_test_IncludeAnalyzer(self, test_data_dir):
    """Test basic functionality assuming test data is in test_data_dir."""

    # Simple stuff: quoted and angled directories, recursion
    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        "gcc -Itest_data/dfoo test_data/parse.c",
        os.getcwd()))
    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['parse.c',
         'dfoo/foo2.h',
         'dfoo/foo.h',
         'dfoo/../dbar/dbar1/bar.h'],
        test_data_dir))

    # Computed includesgg

    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        "gcc -Itest_data/dfoo test_data/computed_includes.c",
        os.getcwd()))

    # The path to the computed angle includes was not provided, so
    # they were not found.
    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['computed_includes.c', 'p1.h'],
        test_data_dir))

    # Check that symbol_table has been updated.
    self.assertEqual(self.include_analyzer.symbol_table,
                     {'dfoo_foo2_h': [None],
                      'A': ['"p1.h"'],
                      'dbar_dbar1_bar_h': [None],
                      'm': [(['a'], '<a##_pre.c>'), (['a'], '<a##_post.c>')]})


    # The include path to the angle includes, -Itest_data, is
    # provided at our next try. This should include abc_pre.c and
    # abc_post.c.  This is especially challenging for incremental
    # analysis because the previous cached result of include analyzing
    # computed_includes.c cannot in fact be reused here. That is an
    # unusual case.
    includes = self.RetrieveCanonicalPaths(
      self.ProcessCompilationCommandLine(
        "gcc -Itest_data/dfoo -Itest_data"
        + " test_data/computed_includes.c",
        os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['computed_includes.c', 'p1.h', 'abc_post.c', 'abc_pre.c'],
        test_data_dir))

   # The inclusion chain is baz/start_x -> baz/x.h -> foo/x.h ->
   # bar/x.h -> baz.h.  Only the final #include_next "x.h" in baz/x.h
   # does not Resolve because we just used -Ibaz for resolution, the
   # last in the list. This should not raise an exception, because the
   # failure may in reality not happen: the bad include_next could be
   # guarded by a conditional for example.
    self.assertEqual(
      self.RetrieveCanonicalPaths(self.ProcessCompilationCommandLine(
          "gcc -E  -Itest_data/test_include_next/foo"
          + " -Itest_data/test_include_next/bar"
          + " -Itest_data/test_include_next/baz"
          + " test_data/test_include_next/baz/start_x.c",
          os.getcwd())),
      self.CanonicalPathsForTestData(
        ['test_include_next/baz/start_x.c',
         'test_include_next/baz/x.h',
         'test_include_next/foo/x.h',
         'test_include_next/bar/x.h',
         'test_include_next/baz/x.h'],
        test_data_dir))

    # In contrast to previous example, the #final include_next in baz
    # resolves this time, namely to biz/x.h.
    self.assertEqual(
      self.RetrieveCanonicalPaths(self.ProcessCompilationCommandLine(
          "gcc -E  -Itest_data/test_include_next/foo"
          + " -Itest_data/test_include_next/bar"
          + " -Itest_data/test_include_next/baz"
          + " -Itest_data/test_include_next/biz"
          + " test_data/test_include_next/baz/start_x.c",
          os.getcwd())),
      self.CanonicalPathsForTestData(
        ['test_include_next/baz/start_x.c',
         'test_include_next/baz/x.h',
         'test_include_next/foo/x.h',
         'test_include_next/bar/x.h',
         'test_include_next/biz/x.h'],
        test_data_dir))


    includes = self.RetrieveCanonicalPaths(self.ProcessCompilationCommandLine(
          "gcc -E  -Itest_data/test_include_next/foo"
          + " -Itest_data/test_include_next/bar"
          + " -Itest_data/test_include_next/baz"
          + " test_data/test_include_next/baz/start_y.c",
          os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['test_include_next/baz/start_y.c',
         'test_include_next/baz/../foo/y.h',
         'test_include_next/bar/y.h',
         'test_include_next/foo/y.h',
         'test_include_next/baz/y.h'],
        test_data_dir))

    # Test that a directory that has a name matching an include is not picked.
    # Here the directory is test_data/i_am_perhaps_a_directory.h, which is in
    # the file directory of the translation unit. Instead,
    # test_data/dfoo/i_am_perhaps_a_directory.h should be picked: it is a
    # regular file.
    includes = self.RetrieveCanonicalPaths(self.ProcessCompilationCommandLine(
          "gcc -E  -Itest_data/dfoo test_data/test_directory_probing.c",
          os.getcwd()))

    self.assertEqual(
      includes,
      self.CanonicalPathsForTestData(
        ['test_directory_probing.c', 'dfoo/i_am_perhaps_a_directory.h'],
        test_data_dir))

  def test_IncludeAnalyzer(self):
    """Run helper_test_IncludeAnalyzer 'directly' without complications
    of symbolic links."""
    self.helper_test_IncludeAnalyzer('test_data')


  def test_IncludeAnalyzer_from_symlink_farm(self):
    """Run helper_test_IncludeAnalyzer through a link farm to exercise include
    processors ability to handle links."""
    self.helper_test_IncludeAnalyzer('test_data/symlink_farm')


for algorithm in [ basics.MEMOIZING ]:
  try:
    print("TESTING ALGORITHM %s" % algorithm)
    unittest.main()
  except:
    raise
