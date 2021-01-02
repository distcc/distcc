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

"""Evaluation of macros acccording to an overapproximation semantics.

This module generally follows CPP semantics for the evaluation of macros. But we
treat every define as a possible one, because we don't know whether it is
actually executed when a file is really preprocessed. Our semantics is thus
multi-valued: an expression (that is, a string) is evaluated to the set of
poosible values it can have according to arbitrary choices of #defines in effect
among those encountered in the program text.

An example explains the general idea. If we have:

#define A x
#define A y
#define B 1
#define B 2

Then the expression

  A.B

evaluates to one of the following:

  x.1
  y.1
  x.2
  y.2

The set {"x.1", "y.1", "x.2", "y.2"} is the value of EvalExpr("A.B",
symbol_table), where symbol_table is the dictionary in which we have
stored the four #define's.

Currently, we will be satisfied with

  A A

evaluating to

  x x
  x y
  y x
  y y

although a sharper semantic modelling would yield only:

  x x
  y y



How to Read This Code
---------------------
An understanding of the C preprocessor is necessary. See "The GNU C Preprocessor
Internals" (http://gcc.gnu.org/onlinedocs/cppinternals). Especially the section
"Macro Expansion Algorithm" is informative.

Whitespace Insertion and Other Deficiencies
-------------------------------------------
CPP inserts whitespaces and sometimes doesn't according to very complicated
rules. We do not insert whitespaces.

Also, we retokenize each intermediate expansion.

For actual arguments of macros, we also we do not do the right thing for
parentheses inside single quotes, which is to ignore them.

There are probably several more deviations from CPP semantics.

These deviations should not matter for most common included computes.

What If the Include Processor is Wrong
--------------------------------------
Assume that we have

  #include very_complicated_call(anfhis,fifj)

If the include processor produces spurious expansions like:

 "whatda.c"
 2 + 2
 "5 * 3"

then file whada.c if found in search directory becomes part of the include
closure. So does the file "5 * 3". But 2 + 2 does not have the shape of an a
filepath in an include: the filepath must be in quotes or angle brackets.

These spurious files are not harmful to preprocessing on the server.

If the include server omits calculating the expansion

 "right_file.h"

then the compilation on the server will fail. The client, according to the logic
of dcc_build_somewhere, will then perform a local compilation.

Symbol Table
------------

The symbol table is a dictionary, whose entries are of the form

  symbol: definition_list

Each definition in definition_list is either
 - a string, denoting the expansion of an object-like macro, or
 - a pair ([param_1,...,param_n], rhs), which denotes a function-like macro,
   whose formal parameters are param_1,.., param_n and whose expansion is rhs,
   before the substitution of formal parameters for actual parameters.
"""

__author__ = "Nils Klarlund"

import re

import basics
import parse_file
import statistics

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
DEBUG_TRACE1 = basics.DEBUG_TRACE1
DEBUG_TRACE2 = basics.DEBUG_TRACE2
NotCoveredError = basics.NotCoveredError

# REGULAR EXPRESSIONS

SINGLE_POUND_RE = re.compile(r"\B#\s*(\S*)") # \B = here: not at end of word
DOUBLE_POUND_RE = re.compile(r"##")
SYMBOL_RE = re.compile(r"\b\w+\b") # \b = word boundary \w = word constituent


# HELPER FUNCTIONS

def _BigUnion(list_of_sets):
  """Return the set that is the union of the sets or lists in list_of_sets."""
  result = []
  for s in list_of_sets:
    result.extend(list(s))
  return set(result)


def _PrependToSet(expr, expr_set):
  """Return the set consisting of expr + element with element in expr_set."""
  return set([ expr + expr_ for expr_ in expr_set ])


def _SubstituteSymbolInString(x, y, str):
  """Return the string that results from substituting x for y in str."""
  Debug(DEBUG_TRACE2,
        """_SubstituteSymbolInString: x: "%s", y: "%s", str:"%s" """,
        x, y, str)
  result = re.sub(r"\b%s\b" % re.escape(x), y.replace('\\', '\\\\'), str)
  Debug(DEBUG_TRACE2, """_SubstituteSymbolInString (result): "%s" """, result)
  return result

def _ParseArgs(string, pos):
  """Split stuff according to commas at outer level in parenthesized string.

  If string[pos:] does not start with '(', return (None, pos). If string[pos:]
  starts with '(' and there is a balanced parenthesis structure ending
  at pos_end, then return (args, pos_end), where args is
  string[pos:pos_end] hacked into segments between commas at outer
  level.  So "(a,m(c, n(d)), c)...." results in (["a", "m(c, n(d))", "
  c"], 17) being returned.
  """
  # TODO(klarlund): we ignore ignoring parentheses inside single quotes. Such
  # occurrences are deemed unlikely at this moment. Fix that so that parentheses
  # inside single quotes are ignored.
  open_parens = 0
  if not pos < len(string) or string[pos] != '(':
    return (None, pos)
  # Prepare a list of comma and extremal positions.  The '(' at the left is the
  # first extremal position.
  commas = [pos]
  pos_end = None
  inside_quotes = False
  for i in range(pos, len(string)):
    if inside_quotes:
      if string[i] == '"' and string[i-1] != r'\\':
        inside_quotes = False
      continue
    if string[i]==',' and open_parens==1:
      commas.append(i)
    elif string[i]=='(':
      open_parens += 1
    elif string[i]==')':
      open_parens -= 1
      if open_parens == 0:
        pos_end = i
        break
    elif string[i] == '"' and string[i-1] != r'\\':
      inside_quotes = True
  if not pos_end:
    return (None, pos)
  commas.append(pos_end)  # the other extremal position
  args_list = []
  for i in range(len(commas) - 1):
    args_list.append(string[commas[i] + 1 : commas[i + 1]])
  return (args_list, pos_end + 1)


def _MassageAccordingToPoundSigns(string):
  """Perform 'stringification (#) and concatenation (##)."""
  return SINGLE_POUND_RE.sub(r'"\1"', DOUBLE_POUND_RE.sub("", string))


# EVALUATION

def _EvalExprHelper(expr, symbol_table, disabled):
  if __debug__:
    Debug(DEBUG_TRACE2, "EvalExprHelper: expr: %s", expr)

  """Evaluate according to an overapproximation macro substitution semantics.

  Arguments:
    expr:          a string
    symbol_table:  { symbol: [rhs,
                              ...,
                              ([param_1,...,param_n], rhs),
                              ...],
                     ... }, where rhs and param_i are strings
    disabled:       set of disabled symbols (see "The GNU C Preprocessor
                    Internals")

  Returns: [ expr_1, expr_2, ...], which is a non-empy list of
    strings, namely the expansions of expr.
  """

  def _ReEvalRecursivelyForExpansion(expansion, after):
    """Reevaluate the expansion that is the result of finding a match for a
    macro.

    Arguments:
      symbol (outer scope): the name of the matched macro that resulted in
        expansion; it is the same as match.group()
      match (outer scope): the match data for the symbol
      expansion: the expansion we are substituting for the match
      after: the string after the expansion
    Modifies:
      value_set: the set of all possible expansions of expr

    The value set is updated according to recursive evaluations of the string
    that results from inserting expansion between expr[:match.start()] and
    expr[match.end():] (for symbol-like macro) or expr[args_end:] (for
    function-like macro).

    The idea is to form a set of strings from a cross product of two string sets
    describing all possibly expansions before and after the match.

    There are two recursions involved. First, we evaluate after to find all
    possible values of what follows the match. This recursion does not involve a
    larger disabled set. Each resulting string is named after_eval_expr. Second,
    we evaluate expansion concatenated with each after_eval_expr value.  In
    these evaluations, symbol is added to the disabled set.
    """
    if __debug__:
      Debug(DEBUG_TRACE2,
            ("_ReEvalRecursivelyForExpansion: expr: %s\n" +
             "    before:     %s\n    expansion:        %s\n    after: %s")
            % (expr, expr[:match.start()], expansion, after))
    value_set.update(
      _PrependToSet(expr[:match.start()],
                   _BigUnion([ _EvalExprHelper(expansion + after_expansion,
                                              symbol_table,
                                              disabled | set([symbol]))
                              for after_expansion in
                              _EvalExprHelper(after,
                                              symbol_table,
                                              disabled) ])))

  def _EvalMacro(definition, disabled):
    """Evaluate symbol according to definition.

    Here definition is either object-like or function-like.
    """
    # Consider that this symbol goes unevaluated.
    value_set.update(
      _PrependToSet(expr[:match.end()],
                   _EvalExprHelper(expr[match.end():],
                                   symbol_table,
                                   disabled)))
    if isinstance(definition, str):
      # The expansion is the definition.
      _ReEvalRecursivelyForExpansion(definition, expr[match.end():])
    elif isinstance(definition, tuple):
      # We have an invocation a function-like macro. Find the possible
      # values of the function symbol, according to object-like
      # expansions, before substituting.
      (lhs, rhs) = definition  # lhs = formal parameters, rhs =
                               # expansion before substitution
      # Verify that the number of formal parameters match the
      # number of actual parameters; otherwise skip.
      if not args_list or len(lhs) != len(args_list):
        return
      # Expand arguments recursively.
      args_expand = [ _EvalExprHelper(arg, symbol_table, disabled)
                        for arg in args_list ]
      # Do the substitutions. Again, we'll need to piece together
      # strings from a cross product. In this the fragments come from
      # the expansions of the arguments.
      expansions = [rhs]
      for i in range(len(args_expand)):
        expansions = [ _SubstituteSymbolInString(lhs[i], arg, expansion)
                       for expansion in expansions
                       for arg in args_expand[i] ]
      for expansion in expansions:
        real_expansion = _MassageAccordingToPoundSigns(expansion)
        _ReEvalRecursivelyForExpansion(real_expansion, expr[args_end:])
    else:
      assert False, "Definition '%s' is unexpected." % definition

  # Look for a symbol.
  match = SYMBOL_RE.search(expr)
  if not match:
    # No symbol found.
    return set([expr])
  else:
    # Let's break down the string into segments according to the
    # symbol found. This is non-standard: the real CPP only tokenizes
    # once.
    symbol = match.group()
    (args_list, args_end) = _ParseArgs(expr, match.end())
    Debug(DEBUG_TRACE2,
          "EvalExprHelper (inside): expr: %s\n" +
          "    symbol:     %s\n    args_list:       %s\n" +
          "    before: %s\n",
          expr, symbol, args_list, expr[:match.start()])
    if symbol not in symbol_table:
      # Process rest of string recursively.
      return _PrependToSet(expr[:match.end()],
                          _EvalExprHelper(expr[match.end():],
                                          symbol_table,
                                          disabled))
    else:
      # Now consider the set of meanings of this symbol.  But first
      # note that the string remaining unexpanded is always a
      # possibility, because we are doing a "forall" analysis.
      value_set = set([expr])
      # Now carry out substitution on expr[match.start():match.end()],
      # the whole stretch of expr that consists of symbol and possibly
      # args with parentheses.
      if symbol not in disabled:
        defs = symbol_table[symbol]
        for definition in defs:
          _EvalMacro(definition, disabled)
      return value_set


def EvalExpression(expr, symbol_table):
  """Calculate sets of possibly values of expr given symbol_table.

  Arguments:
    expr:          any string to be macro expanded
    symbol_table:  { symbol: {rhs, ..}, ,...,
                     symbol:{((param_1,...,param_n), rhs), ... }
  Returns:
    [ expr_1, expr_2, ...], a list of strings: the possible expansions of expr.
  """
  if __debug__:
    Debug(DEBUG_TRACE, "EvalExpression: expr: %s", expr)
  r = set(_EvalExprHelper(expr, symbol_table, set([])))
  if __debug__:
    Debug(DEBUG_TRACE, "EvalExpression: return: %s", r)
  return r


def ResolveExpr(includepath_map_index,
                resolve,
                expr,
                currdir_idx,
                searchdir_idx,
                quote_dirs,
                angle_dirs,
                symbol_table):
  """Evaluate and resolve possible values for expr using symbol table.

  Determine all possible values of expr. Those that are of the form "filepath"
  or <filepath> are resolved against (file_dir_idx, quote_dirs) or angle_dirs,
  respectively. The set of resolvants is returned along with a list of all
  symbols that occurs in possible evaluations of expr.

  Arguments:
    includepath_map_index: the Index function of an includepath map
    resolve: a Resolve method of a BuildStatCache object
    expr: any string to be macro expanded
    currdir_idx: a directory index
    searchdir_idx: a directory index (used for resolving quote-includes)
    quote_dirs: a directory index list
    angle_dirs: a directory index list
    symbol_table: as described in module macro_expr
  Returns:
    a pair(files, symbols), where files is a list of (filepath pair,
    realpath index), namely those files that are successful resolutions of
    possible double-quoted and angle-quoted values of expr, and symbols is
    the set of all identifiers occurring in expression or its possible
    expansions
  Raises:
    NotCoveredError
  """
  if __debug__:
        Debug(DEBUG_TRACE, "ResolveExpr: %s, %s, %s",
              expr, searchdir_idx, angle_dirs)
  resolved_files = []
  symbols = []
  statistics.resolve_expr_counter += 1
  for val in EvalExpression(expr, symbol_table):
    match_result = parse_file.INCLUDE_STRING_RE.match(val)
    if match_result:
      if match_result.group('quote'):
        resolved = resolve(includepath_map_index(match_result.group('quote')),currdir_idx, searchdir_idx, quote_dirs)
        resolved_files.append(resolved)
      elif match_result.group('angle'):
        resolved = resolve(includepath_map_index(match_result.group('angle')), currdir_idx, None, angle_dirs)
        resolved_files.append(resolved)
    else:
      symbols.extend(SYMBOL_RE.findall(val))
  if __debug__:
      Debug(DEBUG_TRACE, "ResolveExpr: return: %s", resolved_files)
  return (resolved_files, set(symbols))
