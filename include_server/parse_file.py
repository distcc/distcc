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

"""A very fast directives-only parser for C and C++ source code.

We parse only the following directives:
  #include       (the standard C/C++ inclusion mechanism)
  #include_next  (a GNU C/C++ extension)
  #import        (an Objective-C feature, similar to #include)
  #define        (because #defines can affect the results of '#include MACRO')
"""

__author__ = 'Nils Klarlund'

import re
import time

import basics
import cache_basics
import statistics

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
DEBUG_TRACE2 = basics.DEBUG_TRACE2
NotCoveredError = basics.NotCoveredError

# For coarse and fast scanning
RE_INCLUDE_DEFINE = re.compile("include|define|import")

# For fine-grained, but slow backtracking, parsing
POUND_SIGN_RE = re.compile(r"""
  ^                            # start of line
  [ \t]*                       # space(s)
  ([*][/])?                    # a possible ..*/ ending block comment
  [ \t]*                       # space(s)
  ([/][*] [^\n]* [*][/])*      # initial block comment(s) /*...*/
  [ \t]*                       # space(s)
  (?P<directive>               # group('directive') -- what we're after
   [#]                         # the pound sign
   [ \t]*                      # space(s)
   (define|include_next|include|import)\b # the directive
   ((?!\\\n).)*                 # the rest on this line: zero or more
                                # characters, each not a backslash that
                                # is followed by \n
   (\\\n((?!\\\n).)*)*          # (backslash + \n + rest of line)*
  )
  """, re.VERBOSE + re.MULTILINE)


NOT_COMMA_OR_PARENS = "([^(),])"

# For parsing macro expressions of the form:
#  symbol
#  symbol  (something, ..., something), where something is not ',', '(', or ')'
MACRO_EXPR = r"""
 (?P<symbol>\w+)                   # the symbol, named 'symbol'
  ( \s*
    [(] \s*                        # beginning parenthesis
    (?P<args>                      # a parenthesized expression (with no
                                   # containing expressions -- a limitation)
                                   # named 'args'
    %(NOT_COMMA_OR_PARENS)s*        # the first argument (if it exists)
     ([,]%(NOT_COMMA_OR_PARENS)s*)* # subsequent arguments
    )
  [)]                              # ending parenthesis
  )?""" % {'NOT_COMMA_OR_PARENS': NOT_COMMA_OR_PARENS}

MACRO_EXPR_RE = re.compile(MACRO_EXPR, re.VERBOSE)

# Nice little parser of certain directive lines (after backslash-ended
# line continuations and comments are removed)
DIRECTIVE_RE = re.compile(r"""
 ^[ \t]*
  [#]
  [ \t]*
  (
   ((?P<include> include_next | include | import)
    \s*
    ( "(?P<quote> (\w|[_/.,+-])*)"            |  # "bar/foo.h"
      <(?P<angle> (\w|[_/.,+-])*)>            |  # <stdio.h>
      (?P<expr>   .*?))                         # expr, match . minimally
    )
   |
   (?P<define> define \s+ (?P<lhs> %s)    # insert MACRO_EXPR here
                      \s* (?P<rhs> .*?))  # match . minimally before
                                          # trailing white space
  )
  \s*                                     # trailing whitespace
  ((/[*]|//).*)?                          # optional trailing comment start
  $
  """ % MACRO_EXPR,
  re.VERBOSE)

#
INCLUDE_STRING_RE = re.compile(r"""
  ^
  \s*
  ( "\s*(?P<quote> (\w|[\\_/.,+-])*)\s*"            |
    <\s*(?P<angle> (\w|[\\_/.,+-])*)\s*>
  )
  \s*
  $
""", re.VERBOSE)

# For ridding lines of backslash
BACKSLASH_RE = re.compile(r"\\\n", re.MULTILINE)

# For matching non-comment prefix of line.
COMMENT_RE = re.compile(r"((?!/[*]|//).)*")

# FOR SEARCHING AFTER /* .. */.
PAIRED_COMMENT_RE = re.compile(r"(/[*].*?[*]/)")


def InsertMacroDefInTable(lhs, rhs, symbol_table, callback_function):
  """Insert the definition of a pair (lhs, rhs) into symbol table.

  Arguments:
    lhs: a string, of the form "symbol" or "symbol(param1, ..., paramN)"
    rhs: a string
    symbol_table: where the definition will be inserted
    callback_function: a function called with value "symbol"
  """
  m_expr = MACRO_EXPR_RE.match(lhs)
  if m_expr.end(0) != len(lhs):
    raise NotCoveredError(
      "Unexpected macro definition with LHS: '%s'." % lhs)
  # Calculate the definition df, either
  # - a pair ([arg_1, .., arg_n], rhs) where arg_i is the
  #   i'th formal parameter (function-like macro definition), or
  # - just a symbol (object-like macro definition)
  if m_expr.group('args') != None:  # perhaps ''
    # A function-like macro definition.
    # Construct pair (list of formal parameters, rhs).
    args = m_expr.group('args').split(',')
    df = args, rhs
    # lhs is adjusted to be just the 'function' name
    lhs = m_expr.group('symbol')
  else: # m_expr.group('args')
    # An object-like macro definition
    assert m_expr.group('symbol') == lhs
    df = rhs
  if lhs not in symbol_table:
    symbol_table[lhs] = [df]
  else:
    symbol_table[lhs].append(df)
  callback_function(lhs)


class ParseFile(object):
  """Parser class for syntax understood by CPP, the C and C++
  preprocessor. An instance of this class defines the Parse method."""

  def __init__(self, includepath_map):
    """Constructor. Make a parser.

    Arguments:
      includepath_map: string-to-index map for includepaths
    """
    assert isinstance(includepath_map, cache_basics.MapToIndex)
    self.includepath_map = includepath_map
    self.define_callback = lambda x: None

  def SetDefineCallback(self, callback_function):
    """Set a callback function, which is invoked for '#define's.

     The function is called as callback_function(symbol), whenever a '#define'
     of symbol is parsed.  The callback allows an include processor to adjust
     its notion of which expressions are still current. If we (the include
     processor) already met

             #define A B

     and later meet

             #define B

     whether this is the first definition of B or not, then the possible
     meanings of A have changed.  We set up a callback to identify such
     situations."""

    self.define_callback = callback_function

  def _ParseFine(self, poundsign_match, includepath_map_index, file_contents,
                 symbol_table, quote_includes, angle_includes, expr_includes,
                 next_includes):
    """Helper function for ParseFile."""
    Debug(DEBUG_TRACE2, "_ParseFine %s",
      file_contents[poundsign_match.start('directive'):
                    poundsign_match.end('directive')])
    m = DIRECTIVE_RE.match(  # parse the directive
          PAIRED_COMMENT_RE.sub( # remove possible paired comments
            "",
            BACKSLASH_RE.sub(   # get rid of lines ending in backslash
              "",
              file_contents[poundsign_match.start('directive'):
                            poundsign_match.end('directive')])))
    if m:
      try:
        groupdict = m.groupdict()
        if groupdict['include'] == 'include' or \
           groupdict['include'] == 'import':
          if groupdict['quote']:
            quote_includes.append(includepath_map_index(m.group('quote')))
          elif groupdict['angle']:
            angle_includes.append(includepath_map_index(m.group('angle')))
          elif groupdict['expr']:
            expr_includes.append(m.group('expr').rstrip())
          else:
            assert False
        elif groupdict['include'] == 'include_next':
          # We do not, in fact, distinguish between the two kinds of
          # include_next's, because we conservatively assume that they are of
          # the quote variety.
          if groupdict['quote']:
            next_includes.append(includepath_map_index(m.group('quote')))
          elif groupdict['angle']:
            next_includes.append(includepath_map_index(m.group('angle')))
          # The following restriction would not be too hard to remove.
          elif groupdict['expr']:
            NotCoveredError(
              "For include_next: cannot deal with computed include here.")
          else:
            assert False
            raise NotCoveredError("include_next not parsed")
        elif groupdict['define']:
          if not groupdict['lhs']:
            raise NotCoveredError("Unexpected macro definition with no LHS.")
          else:
            lhs = m.group('lhs')
            rhs = groupdict['rhs'] and groupdict['rhs'] or None
            InsertMacroDefInTable(lhs, rhs, symbol_table, self.define_callback)
      except NotCoveredError as inst:
        # Decorate this exception with the filename, by recreating it
        # appropriately.
        if not inst.source_file:
          raise NotCoveredError(inst.args[0],
                                self.filepath,
                                send_email = inst.send_email)
        else:
          raise

  def Parse(self, filepath, symbol_table):
    """Parse filepath for preprocessor directives and update symbol table.

    Arguments:
      filepath: a string
      symbol_table: a dictionary, see module macro_expr

    Returns:
      (quote_includes, angle_includes, expr_includes, next_includes), where
      all are lists of filepath indices, except for expr_includes, which is a
      list of expressions.
    """
    Debug(DEBUG_TRACE, "ParseFile %s", filepath)

    assert isinstance(filepath, str)
    self.filepath = filepath
    parse_file_start_time = time.perf_counter()
    statistics.parse_file_counter += 1

    includepath_map_index = self.includepath_map.Index

    try:
      fd = open(filepath, "r", encoding='latin-1')
    except IOError as msg:
      # This normally does not happen because the file should be known to
      # exists. Still there might be, say, a permissions issue that prevents it
      # from being read.
      raise NotCoveredError("Parse file: '%s': %s" % (filepath, msg),
                            send_email=False)

    file_contents = fd.read()
    fd.close()

    quote_includes, angle_includes, expr_includes, next_includes = (
      [], [], [], [])

    i = 0
    line_start_last = None

    while True:

      # Scan coarsely to find something of interest
      mfast = RE_INCLUDE_DEFINE.search(file_contents, i + 1)
      if not mfast: break
      i = mfast.end()
      # Identify the line of interest by scanning backwards to \n
      line_start = file_contents.rfind("\n", 0, i) + 1 # to beginning of line
      # Now, line_start is -1 if \n was not found.

      ### TODO(klarlund) continue going back if line continuation preceeding

      # Is this really a new line?
      if line_start == line_start_last: continue
      line_start_last = line_start

      # Here we should really skip back over lines to see whether a totally
      # pathological situation involving '\'-terminated lines like:
      #
      # #include <stdio.h>
      # # Start of pathological situation involving line continuations:
      # # \
      #    \
      #     \
      #      \
      #       include     "nidgaard.h"
      #
      # occurs, where the first # on each line is just Python syntax and should
      # not be considered as part of the C/C++ example. This code defines a
      # valid directive to include "nidgaard.h". We will not handle such
      # situations correctly -- the include will be missed.

      # Parse the line of interest according to fine-grained parser
      poundsign_match = POUND_SIGN_RE.match(file_contents, line_start)

      if not poundsign_match:
        continue

      self._ParseFine(poundsign_match, includepath_map_index, file_contents,
                      symbol_table, quote_includes, angle_includes,
                      expr_includes, next_includes)


    statistics.parse_file_total_time += time.perf_counter() - parse_file_start_time

    return (quote_includes, angle_includes, expr_includes, next_includes)
