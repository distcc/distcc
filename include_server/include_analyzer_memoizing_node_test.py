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
import time

import basics
import parse_command
import cache_basics
import include_analyzer_memoizing_node
import unittest

NotCoveredError = basics.NotCoveredError

class IncludeAnalyzerMemoizingNodeUnitTest(unittest.TestCase):

  def _ToString(self, include_closure):
    """Translate the indices in an include closure to their denoted strings."""
    return (
      dict((self.realpath_map.string[rp_idx],
            [ (self.directory_map.string[dir_idx],
               self.includepath_map.string[ip_idx])
              for (dir_idx, ip_idx) in include_closure[rp_idx] ])
           for rp_idx in include_closure))

  def setUp(self):
    basics.opt_debug_pattern = 1
    client_root_keeper = basics.ClientRootKeeper()
    self.include_analyzer = (
      include_analyzer_memoizing_node.IncludeAnalyzerMemoizingNode(
        client_root_keeper))

    self.includepath_map = self.include_analyzer.includepath_map
    self.canonical_path = self.include_analyzer.canonical_path
    self.directory_map = self.include_analyzer.directory_map
    self.realpath_map = self.include_analyzer.realpath_map


  def test__CalculateIncludeClosureExceptSystem(self):
    """Construct a summary graph, then calculate closure and check."""
    includepath_map = self.includepath_map
    canonical_path = self.canonical_path
    directory_map = self.directory_map
    realpath_map = self.realpath_map

    # Create summary graph with structure A -> B -> C -> B and B -> D -> C' ->
    # E.  Note that B has two immediate successors.  Assume the current dir is
    # /curr (for all nodes). The include graph is made for a searchlist [src, /,
    # /dirlink].  Note that the first directory in the searchlist is relative.
    # For simplicity, we consider angled resolution only. Here is a description
    # of the nodes.
    #
    #  - Nodes A, B correspond to includepaths a.h and b.h, which are relatively
    #    resolved because they are found in src, which means absolute
    #    directory /curr/src.  These filepaths are thus resolved to src/a.h and
    #    src/b.h and their corresponding realpaths are /curr/src/a.h and
    #    /curr/src/b.h.  The realpaths are all what is recorded in the include
    #    closure, because the files are relatively resolved.
    #
    #  - Nodes C, C', and E are absolutely resolved includepaths. More
    #    specifically:
    #
    #     - Nodes C and C' correspond to includepath dir/c.h and c.h, which
    #       during resolution were found in / and /dirlink, respectively.
    #       However, /dirlink is a symbolic link to /dir, which is a real
    #       directory, so C and C' in fact correspond to the same realpath
    #       namely /dir/c.h.
    #
    #    - Node E corresponds to includepath dir/e.h and was found during
    #      resolution to be in /. So, the realpath of this node is /dir/e.h.
    #
    #  - Node D is a dummy and it is not recorded in the include closure. Still,
    #    its non-dummy descendants are recorded.

    src_idx = directory_map.Index("src")
    curr_idx = directory_map.Index("/curr")
    root_idx = directory_map.Index("/")
    dirlink_idx = directory_map.Index("/dirlink")
    curr_src_idx = directory_map.Index("/curr/src")
    dir_idx = directory_map.Index("/dir")

    A_children = []
    A = (realpath_map.Index("/curr/src/a.h"),
         (src_idx, includepath_map.Index("a.h")),
         A_children)

    B_children = []
    B = (realpath_map.Index("/curr/src/b.h"),
         (src_idx, includepath_map.Index("b.h")),
         B_children)

    C_children = []
    C = (realpath_map.Index("/dir/c.h"),
         (root_idx, includepath_map.Index("dir/c.h")),
         C_children)

    C__children = []
    C_ = (realpath_map.Index("/dir/c.h"),
          (dirlink_idx, includepath_map.Index("c.h")),
          C__children)

    D_children = []
    D = (None, None, D_children)

    E_children = []
    E = (realpath_map.Index("/dir/e.h"),
         (root_idx, includepath_map.Index("dir/e.h")),
         E_children)

    A_children.extend([B])
    B_children.extend([C, D])
    C_children.extend([B])
    C__children.extend([E])
    D_children.extend([C_])
    E_children.extend([])

    include_closure = {}
    self.include_analyzer._CalculateIncludeClosureExceptSystem(A, include_closure)
    stringified_include_closure = self._ToString(include_closure)

    # /curr/src/a.h is not known under absolute pathnames.
    self.assertEqual(stringified_include_closure['/curr/src/a.h'], [])
    # Neither is /curr/src/b.h.
    self.assertEqual(stringified_include_closure['/curr/src/b.h'], [])
    # But, /dir/c.h is known under two different absolute names.
    self.assertEqual(stringified_include_closure['/dir/c.h'],
                     [('/dirlink/', 'c.h'), ('/', 'dir/c.h')])
    # And, dir/e.h is known under exactly one absolute name.
    self.assertEqual(stringified_include_closure['/dir/e.h'], [('/', 'dir/e.h')])
    # That is all and nothing more.
    self.assertEqual(len(stringified_include_closure), 4)


  def _ConstructDistccCommandLine(self, src_stem):
    # A command line, which is more or less the one found in the
    # generated Makefile for distcc. We don't need the exact form of
    # the command."
    return ("gcc -DHAVE_CONFIG_H -D_GNU_SOURCE" +
            " -I./src" +
            ' -DSYSCONFDIR="/usr/local/etc"' +
            ' -DPKGDATADIR="/usr/local/share/distcc"' +
            " -Isrc" +
            " -I./lzo" +
            " -include include_me.h " +
            " -o src/%s.o" +
            " -c src/%s.c") % (src_stem, src_stem)

  def test__CalculateIncludeClosureExceptSystem_on_distcc(self):

    includepath_map = self.includepath_map
    canonical_path = self.canonical_path
    directory_map = self.directory_map
    realpath_map = self.realpath_map
    include_analyzer = self.include_analyzer

    current_dir = os.path.realpath("test_data/distcc")
    os.chdir(current_dir)

    src_stem = "distcc"
    cmd = self._ConstructDistccCommandLine(src_stem)

    parsed_command = (
        parse_command.ParseCommandArgs(
          parse_command.ParseCommandLine(cmd),
          current_dir,
          include_analyzer.includepath_map,
          include_analyzer.directory_map,
          include_analyzer.compiler_defaults))

    (include_analyzer.quote_dirs,
     include_analyzer.angle_dirs,
     include_analyzer.include_files,
     translation_unit,
     include_analyzer.result_file_prefix,
     _) = parsed_command

    self.assertEqual(translation_unit, "src/%s.c" % src_stem)

    expected_suffixes = [
      "src/include_me.h",
      "src/implicit.h",
      "src/distcc.c",
      "src/config.h",
      "src/distcc.h",
      "src/state.h",
      "src/compile.h",
      "src/trace.h",
      "src/exitcode.h",
      "src/util.h",
      "src/hosts.h",
      "src/bulk.h",
      "src/emaillog.h"]

    include_closure = (
     include_analyzer.ProcessCompilationCommand(current_dir,
                                                parsed_command))

    expected_prefix = os.getcwd() + '/'

    expected = set([ expected_prefix + expected_suffix
                     for expected_suffix in expected_suffixes ])
    self.assertEqual(set([realpath_map.string[key]
                          for key in include_closure.keys()]),
                     expected)

    for rp_idx in include_closure:
      self.assertEqual(len(include_closure[rp_idx]), 0)

    # TODO(klarlund): massage command so as to test that with a
    # different search path files are reported as absolute. That is,
    # provoke pairs (directory_idx, includepath_idx) to exist in
    # include_closure[rp_idx].

  def tearDown(self):
    pass


unittest.main()
