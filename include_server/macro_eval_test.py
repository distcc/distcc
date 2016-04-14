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

# See also tests in include_server_test.py.

import os
import basics
import parse_file
import cache_basics
import macro_eval
import shutil
import tempfile
import unittest

NotCoveredError = basics.NotCoveredError

class MacroEvalTest(unittest.TestCase):

  def setUp(self):

    basics.opt_debug_pattern = 1

    self.tmp = tempfile.mkdtemp()
    caches = cache_basics.SetUpCaches(self.tmp)

    self.includepath_map = caches.includepath_map
    self.canonical_path = caches.canonical_path
    self.directory_map = caches.directory_map
    self.realpath_map = caches.realpath_map


  def tearDown(self):
    shutil.rmtree(self.tmp)


  def test__SubstituteSymbolInString(self):
    self.assertEqual(
      macro_eval._SubstituteSymbolInString("X", "f(y)", "X+(X, X##Y)"),
      "f(y)+(f(y), f(y)##Y)")
    self.assertEqual(
      macro_eval._SubstituteSymbolInString("a", "b", "c(a, aa)"),
      "c(b, aa)")

  def test_MassageAccordingToPoundSigns(self):
    self.assertEqual(macro_eval._MassageAccordingToPoundSigns('#aa##bb'),
                     '"aabb"')
    self.assertEqual(macro_eval._MassageAccordingToPoundSigns('# a(.)'),
                     '"a(.)"')

  def test__ParseArgs(self):

    self.assertEqual(macro_eval._ParseArgs("(a,m(c, n(d)), c)", 0),
                     (["a", "m(c, n(d))", " c"], 17))

    self.assertEqual(macro_eval._ParseArgs("""(a","m(c, n(d)), c)""", 0),
                     (["""a","m(c, n(d))""", " c"], 19))


  def test__PrependToSet(self):
    self.assertEqual(
      macro_eval._PrependToSet("x", set(["y", "z"])),
      set(["xy", "xz"]))


  def test__BigUnion(self):
    self.assertEqual(macro_eval._BigUnion([set([]), set([1,2]), set([3])]),
		     set([1,2,3]))


  def test_EvalExprDirs(self):

    self.assertEqual(
      macro_eval.EvalExpression("A", { 'A': ['b'] }),
      set(['A', 'b']))

    self.assertEqual(
      macro_eval.EvalExpression("A", { 'A': ['B'], 'B': ['A'] }),
      set(['A', 'B']))

    self.assertEqual(
      macro_eval.EvalExpression("A", { 'A': ['B'], 'B': ['c'] }),
      set(['A', 'B', 'c']))

    self.assertEqual(
      macro_eval.EvalExpression("max(2, 4)",
		      { 'max': [ ( ['x', 'y'], "x < y? y: x") ] }),
      set(['max(2, 4)', '2 <  4?  4: 2']))

    self.assertEqual(
      macro_eval.EvalExpression("F(2, 4)",
		      { 'F': ['max'],
			'max': [ ( ['x', 'y'], "x < y? y: x") ] }),
      set(['max(2, 4)', 'F(2, 4)', '2 <  4?  4: 2']))

    self.assertEqual(
      macro_eval.EvalExpression("max(max(1,2), 3)",
		      { 'max': [ ( ['x', 'y'], "(x < y? y: x)") ] }),
       set(['((1 < 2? 2: 1) <  3?  3: (1 < 2? 2: 1))',
            'max(max(1,2), 3)',
            '(max(1,2) <  3?  3: max(1,2))',
            'max((1 < 2? 2: 1), 3)']))

    self.assertEqual(
      macro_eval.EvalExpression("A", { 'A': ['"a.c"'] }),
      set(['A', '"a.c"']))

    # The ## operator only works in rhs of function-like macros. Check
    # that it doesn't work stand-alone.
    self.assertEqual(
      macro_eval.EvalExpression("A##A", { 'A': ['a.c'] }),
      set(['A##A', 'a.c##A', 'A##a.c', 'a.c##a.c']))

    self.assertEqual(
      macro_eval.EvalExpression("A(y)A(z)", { 'A': [(['x'], 'x##a.c')] }),
      set(['A(y)A(z)', 'A(y)za.c', 'ya.cza.c', 'ya.cA(z)']))

    self.assertEqual(
      macro_eval.EvalExpression("m(abc)", { 'm': [( ['a'], "<a##_post.c>" )] }),
      set(['m(abc)', '<abc_post.c>']))

    self.assertEqual(
      macro_eval.EvalExpression("myfile(hello)",
                                { 'myfile': [(['x'], "myquote(myplace/x)")],
                                  'myquote': [(['y'], """#y""")] }),
      set(['myfile(hello)',
           '"myplace/hello"',
           'myquote(myplace/hello)']))


  def test_FromCPPInternals(self):
    # This little example works.
    #
    #   #define foo(x) bar x
    #
    #   foo(foo) (2) == bar foo (2)
    #
    # Let us check that.
    self.assertEqual(
      macro_eval.EvalExpression("foo(foo) (2)",
                                {'foo':[(['x'], "bar x")]}),
      set(['bar foo (2)', 'foo(foo) (2)']))

    # The next one does not work, because we are not inserting spaces.
    #
    # From :
    #   http://gcc.gnu.org/onlinedocs/cppinternals/Token-Spacing.html#Token-Spacing
    #
    #   #define PLUS +
    #   #define EMPTY
    #   #define f(x) =x=
    #
    #   +PLUS -EMPTY- PLUS+ f(=)
    #     ==> + + - - + + = = =
    #
    # We do not insert spaces as CPP does. But we generate a lot of
    # combinations!
    self.assertEqual(
      macro_eval.EvalExpression("+PLUS -EMPTY- PLUS+ f(=)",
                                { 'PLUS':['+'],
                                  'EMPTY':[""],
                                  'f':[(['x'], '=x=')] }),
      set(['++ -EMPTY- ++ ===',
           '++ -EMPTY- PLUS+ ===',
           '+PLUS -- ++ f(=)',
           '+PLUS -EMPTY- ++ ===',
           '++ -EMPTY- PLUS+ f(=)',
           '+PLUS -EMPTY- PLUS+ f(=)',
           '+PLUS -- ++ ===',
           '++ -EMPTY- ++ f(=)',
           '+PLUS -- PLUS+ ===',
           '+PLUS -- PLUS+ f(=)',
           '++ -- PLUS+ ===',
           '++ -- ++ ===',
           '+PLUS -EMPTY- PLUS+ ===',
           '++ -- PLUS+ f(=)',
           '+PLUS -EMPTY- ++ f(=)',
           '++ -- ++ f(=)']))


  def test_ResolveExpr(self):
    # Erect the edifice of caches.
    caches = cache_basics.SetUpCaches(self.tmp)
    parse_file_obj = parse_file.ParseFile(caches.includepath_map)

    symbol_table = {}
    # Set up symbol_table by parsing test_data/more_macros.c.
    self.assertEqual(parse_file_obj.Parse(
      "test_data/more_macros.c", symbol_table),
      ([], [], ['TEMPLATE_VARNAME(foo)'], []))

    # Check what we got in symbol_table.
    self.assertEqual(
      macro_eval.EvalExpression("TEMPLATE_VARNAME(foo)", symbol_table),
      set(['TEMPLATE_VARNAME(foo)',
           '"maps/foo.tpl.varnames.h"',
           'AS_STRING(maps/foo.tpl.varnames.h)',
           'AS_STRING_INTERNAL(maps/foo.tpl.varnames.h)']))

    # Verify that resolving this expression yields one actual file (which we
    # have placed in test_data/map).
    [((d, ip), rp)], symbols = macro_eval.ResolveExpr(
      caches.includepath_map.Index,
      caches.build_stat_cache.Resolve,
      'TEMPLATE_VARNAME(foo)',
      caches.directory_map.Index(os.getcwd()), # current dir
      caches.directory_map.Index(""), # file directory
      [caches.directory_map.Index("test_data")], # search directory
      [],
      symbol_table)
    self.assertEqual(caches.directory_map.string[d], "test_data/")
    self.assertEqual(caches.includepath_map.string[ip],
                     "maps/foo.tpl.varnames.h")
    self.assertEqual(symbols,
                     set(['TEMPLATE_VARNAME', 'maps',
                          'AS_STRING', 'AS_STRING_INTERNAL',
                          'tpl', 'varnames', 'h', 'foo']))


unittest.main()
