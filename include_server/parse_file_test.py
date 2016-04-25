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


"""Tests for parse_file."""

__author__ = "opensource@google.com"

import unittest

import basics
import cache_basics
import parse_file
import include_server
import include_analyzer

class parse_file_Test(unittest.TestCase):

  def setUp(self):
    include_server.print_statistics = False
    client_root_keeper = basics.ClientRootKeeper()
    include_server.write_include_closure_file = True
    self.include_analyzer = include_analyzer.IncludeAnalyzer(client_root_keeper)

  def tearDown(self):
    pass

  def test_RegularExpressions(self):

    self.assertEqual(parse_file.POUND_SIGN_RE.match(
        """  #\tinclude blah. blah."""
        ).group(0), "  #\tinclude blah. blah.")

    self.assertEqual(parse_file.POUND_SIGN_RE.match(
        """  # gggg include blah. blah."""
        ), None)

    self.assertEqual(parse_file.POUND_SIGN_RE.match(
        """  */  /**/ /*  a */ #  	include blah. blah."""
        ).group(0), '  */  /**/ /*  a */ #  \tinclude blah. blah.')

    self.assertEqual(
      parse_file.MACRO_EXPR_RE.search("m(a, b) + c + n(d)").groupdict(),
      {'args': 'a, b', 'symbol': 'm'})

    # The expression we recognize do not include nested parenthesis
    self.assertEqual(
      parse_file.MACRO_EXPR_RE.search("m(a, (b)) + c + n(d)").groupdict(),
      {'args': None, 'symbol': 'm'})

    self.assertEqual(parse_file.MACRO_EXPR_RE.match("random()").group('symbol'),
                     "random")

    self.assertTrue(parse_file.DIRECTIVE_RE.match(
	"""  # include <a.c>""").group('angle') == 'a.c')
    self.assertTrue(parse_file.DIRECTIVE_RE.match(
	"""  # include mac(a.c, mic)""").group('expr') == 'mac(a.c, mic)')
    self.assertTrue(parse_file.DIRECTIVE_RE.match(
	"""  # include "a.c" """).group('quote') == 'a.c')
    self.assertTrue(parse_file.DIRECTIVE_RE.match(
	"""  #include "a.c" """).group('quote') == 'a.c')
    self.assertTrue(parse_file.DIRECTIVE_RE.match(
	"""  #include"a.c" """).group('quote') == 'a.c')

    self.assertEqual(parse_file.DIRECTIVE_RE.match(
        """ #define m(a) <a##_post.c> """).group('rhs'),
                     '<a##_post.c>')

    self.assertEqual(
      parse_file.DIRECTIVE_RE.match("#define xmlRealloc(ptr, size)"
                         + " xmlReallocLoc((ptr), (size),"
                         + " __FILE__, __LINE__)").group('lhs'),
      "xmlRealloc(ptr, size)")

    self.assertEqual(
      parse_file.DIRECTIVE_RE.match("#define random() rand()").group('lhs'),
      "random()")

    self.assertEqual(
      parse_file.DIRECTIVE_RE.match("#define ABBA ").group('lhs'),
      "ABBA")

    self.assertEqual(
      parse_file.DIRECTIVE_RE.match("#define ABBA").group('lhs'),
      "ABBA")

    self.assertEqual(parse_file.BACKSLASH_RE.sub("",
"""a\
b\
c\
d"""), "abcd")
    self.assertEqual(parse_file.BACKSLASH_RE.sub("", """a
b
"""),
				      """a
b
""")

    self.assertEqual(parse_file.PAIRED_COMMENT_RE.sub("", "ab/*./*..*/cd"), "abcd")
    self.assertEqual(parse_file.PAIRED_COMMENT_RE.sub("", "ab/*/cd"), "ab/*/cd")

    self.assertEqual(parse_file.COMMENT_RE.match("ab/*cd").group(), "ab")
    self.assertEqual(parse_file.COMMENT_RE.match("ab//cd").group(), "ab")
    self.assertEqual(parse_file.COMMENT_RE.match("ab/cd").group(), "ab/cd")

    self.assertEqual(parse_file.
      INCLUDE_STRING_RE.match(""" < ab.c>""").group('angle'),
      "ab.c")

  def test_ParseFile(self):

    includepath_map = cache_basics.MapToIndex()
    canonical_path = cache_basics.CanonicalPath()
    parse_file_obj = parse_file.ParseFile(includepath_map)

    symbol_table = {}
    self.assertEqual(parse_file_obj.Parse(
      "test_data/more_macros.c", symbol_table),
      ([], [], ['TEMPLATE_VARNAME(foo)'], []))
    symbol_table_keys = list(symbol_table.keys())
    symbol_table_keys.sort()
    self.assertEqual(symbol_table_keys,
                     ['AS_STRING', 'AS_STRING_INTERNAL',
                      'ILLFORMED', 'TEMPLATE_VARNAME'])
    [([arg], val)] = symbol_table['TEMPLATE_VARNAME']
    self.assertEqual(arg, '_filename_')
    self.assertEqual(val, 'AS_STRING(maps/_filename_.tpl.varnames.h)')

    self.assertEqual(parse_file_obj.Parse(
      "test_data/computed_includes.c", symbol_table),
      ([],
       [],
       [ 'A' , 'm(abc)' ],
       []))
    self.assertEqual(symbol_table['A'], ['"p1.h"'])
    [val] = symbol_table['ILLFORMED']
    self.assertEqual(val, "(_filename_,(x))   "
                + "AS_STRING(maps/_filename_.tpl.varnames.h, "
                + "NOTHANDLED(_filename_))")

unittest.main()
