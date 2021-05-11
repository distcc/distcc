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

__author__ = "opensource@google.com (Craig Silverstein, Nils Klarlund)"

import re
import os
import sys
import glob

import basics
import cache_basics

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
NotCoveredError = basics.NotCoveredError

# TODO(klarlund): Make mechanism for handling -U, -undef options, along with
# default symbols.

class ParseState:
  """Everything we figure out during parsing.  This is accessed a lot and
  needs to be fast, so you should access and set the data members directly.
  Mutator functions are provided for the non-list elements, but solely
  because this way you can set these elements from within a lambda.
  """
  def __init__(self):
    self.nostdinc = False
    self.file_names = []
    self.quote_dirs = []
    self.include_files = []
    self.i_dirs = []
    self.before_system_dirs = []
    self.after_system_dirs = []

    self.language = 'none'    # equivalent to commandline of '-x none'
    self.isysroot = ""
    self.sysroot = ""
    self.output_file = None
    self.iprefix = ""
    self.Dopts = []

  def set_nostdinc(self): self.nostdinc = True
  def set_language(self, x): self.language = x
  def set_isysroot(self, x): self.isysroot = x
  def set_sysroot(self, x): self.sysroot = x
  def set_outputfile(self, x): self.output_file = x
  def set_iprefix(self, x): self.iprefix = x
  def include_sysroot(self):
    return self.isysroot if self.isysroot else self.sysroot

def _SplitMacroArg(arg):
  """Split an arg as found in -Darg

  Argument:
    arg: argument

  Returns: [arg] if there is no '=' in arg, otherwise [symb, val], where symb is
    what is to the left of '=' and val is what is to the right.
  """
  pos = arg.find("=")
  if pos > 0:
    return [arg[:pos], arg[pos + 1:]]
  else:
    return [arg]

def _RaiseNotImplemented(name, comment=''):
  raise NotCoveredError('%s is not implemented.  %s' % (name, comment))

# These are the cpp options that a) are more than one letter long,
# b) always take an argument, and c) may either have that argument
# as a separate word in argv, or may have the argument concatenated
# after the option-name (eg, either "-include foo" or "-includefoo").
# These are taken from
#    http://gcc.gnu.org/onlinedocs/cpp/Invocation.html#Invocation
# and, more completely, from the gnu gcc info pages.
# Each option takes as a value, the function to run on the opt's argument.
# Below, ps is a ParseState object.
# TODO(csilvers): check for arg[0] == '=' for iquote, isystem
CPP_OPTIONS_MAYBE_TWO_WORDS = {
  '-MF':            lambda ps, arg: None,
  '-MT':            lambda ps, arg: None,
  '-MQ':            lambda ps, arg: None,
  '-arch':          lambda ps, arg: None,
  '-target':        lambda ps, arg: None,
  '-include':       lambda ps, arg: ps.include_files.append(arg),
  '-imacros':       lambda ps, arg: ps.include_files.append(arg),
  '-idirafter':     lambda ps, arg: ps.after_system_dirs.append(arg),
  '-iprefix':       lambda ps, arg: ps.set_iprefix(arg),
  '-iwithprefix':   lambda ps, arg: ps.after_system_dirs.append(
                                      os.path.join(ps.iprefix, arg)),
  '-iwithprefixbefore':  lambda ps, arg: ps.i_dirs.append(
                                           os.path.join(ps.iprefix, arg)),
  '-isysroot':      lambda ps, arg: ps.set_isysroot(arg),
  '-imultilib':     lambda ps, arg: _RaiseNotImplemented('-imultilib'),
  '-isystem':       lambda ps, arg: ps.before_system_dirs.append(arg),
  '-iquote':        lambda ps, arg: ps.quote_dirs.append(arg),
}
CPP_OPTIONS_MAYBE_TWO_WORDS_FIRST_LETTERS = ('M', 'i', '-', 'a', 't')
# A "compile-time" check to make sure the first-letter list is up-to-date
for key in CPP_OPTIONS_MAYBE_TWO_WORDS.keys():
  assert key[1] in CPP_OPTIONS_MAYBE_TWO_WORDS_FIRST_LETTERS

PATH_EXPR='[/a-zA-Z_0-9.]+' # regular expression for a partial file path

# These are the cpp options that require regular expressions, m is Match.
CPP_OPTIONS_REGULAR_EXPRESSIONS = {
  '-Wa,(%s\.s)' % PATH_EXPR:     lambda ps, m: ps.include_files.append(m.group(1)),
  '-Wa,\[(%s\.s)\]' % PATH_EXPR: lambda ps, m: ps.include_files.append(m.group(1)),
}

CPP_OPTIONS_REGULAR_EXPRESSIONS_STARTS_WITH = '-Wa,'
for key in CPP_OPTIONS_REGULAR_EXPRESSIONS.keys():
  assert key.startswith(CPP_OPTIONS_REGULAR_EXPRESSIONS_STARTS_WITH)

CPP_OPTIONS_REGULAR_EXPRESSIONS_COMPILED = {}
for key in CPP_OPTIONS_REGULAR_EXPRESSIONS.keys():
  CPP_OPTIONS_REGULAR_EXPRESSIONS_COMPILED[key] = re.compile(key)

# These are the cpp options that a) are more than one letter long,
# b) always take an argument, and c) must have that argument as a
# separate word in argv.
CPP_OPTIONS_ALWAYS_TWO_WORDS = {
  '-Xpreprocessor': lambda ps, arg: _RaiseNotImplemented('-Xpreprocessor'),

  # In order to parse correctly, this data structure needs to include
  # *all* two-word arguments that gcc accepts (we don't want to see
  # "gcc -aux-info foo" and think that foo is an output filename...)
  # This list is taken from the complete list from the gcc info page:
  # "Option Summary".  These aren't preprocessor-related, so are noops.
  '-aux-info':      lambda ps, arg: None,
  '--param':        lambda ps, arg: None,
  '-Xassembler':    lambda ps, arg: None,
  '-Xlinker':       lambda ps, arg: None,
  '-Xclang':        lambda ps, arg: None,
}

# For efficiency, it's helpful to be able to combine the two above
CPP_OPTIONS_TWO_WORDS = {}
CPP_OPTIONS_TWO_WORDS.update(CPP_OPTIONS_MAYBE_TWO_WORDS)
CPP_OPTIONS_TWO_WORDS.update(CPP_OPTIONS_ALWAYS_TWO_WORDS)

# These are the cpp options that a) are more than one letter long,
# b) always take an argument, and c) have that argument separated from
# the option by '='.
CPP_OPTIONS_APPEARING_AS_ASSIGNMENTS = {
  '--sysroot':     lambda ps, arg: ps.set_sysroot(arg)
}

# These are the cpp options that do not take an argument.
# (Note, most cpp options do not take an argument, but do not pertain to
# preprocessing, so we can ignore them.  Those are dealt in the default
# case in our processing loop.  This is only for no-argument options
# that we actually care about for preprocessing.)
CPP_OPTIONS_ONE_WORD = {
#  '-undef':         lambda ps, arg: _RaiseNotImplemented('-undef')
  '-undef':         lambda ps, arg: None,
  '-nostdinc':      lambda ps: ps.set_nostdinc(),
  # TODO(csilvers): deal with -nostdinc++ as well?
}

# These are the cpp options that are one letter long, and take an
# argument.  In all such cases, the argument may either be the next
# word, or may be appended right after the letter.
CPP_OPTIONS_ONE_LETTER = {
  'D': lambda ps, arg: ps.Dopts.append(arg.split('=')),
  'I': lambda ps, arg: ps.i_dirs.append(arg),
#  'U': lambda ps, arg: _RaiseNotImplemented('-U') # affects computed includes
  'U': lambda ps, arg: None,
  'o': lambda ps, arg: ps.set_outputfile(arg),
  'x': lambda ps, arg: ps.set_language(arg),

  # In order to parse correctly, this data structure needs to include
  # *all* two-word arguments that gcc accepts (we don't want to see
  # "gcc -L foo" and think that foo is an output filename...)  Since
  # most one-letter args can go as either '-Lfoo' or '-L foo', we need
  # to include (almost) all one-letter args in our list, even when we
  # don't care about them.  This list is taken from the complete list
  # from the gcc info page: "Option Summary".  Since these aren't
  # preprocessor-related, they are all noops.
  'A': lambda ps, arg: None,
  'l': lambda ps, arg: None,
  'F': lambda ps, arg: ps.i_dirs.extend(glob.glob(os.path.join(arg,'*', 'Headers'))),
  'u': lambda ps, arg: None,
  'L': lambda ps, arg: None,
  'B': lambda ps, arg: None,
  'V': lambda ps, arg: None,
  'b': lambda ps, arg: None,
}


### DREADFUL PARSER +  OPTIMIZED PARSER

# This parser was written after a *much* simpler parser using regular
# expression turned out to be too slow, two orders of magnitude slower
# than str.split. The parser below is faster than the one based on
# regular expression and more complete, so that's the one we keep.

NONSPACE_RE = re.compile(r'\S') # don't use \S|$, which introduces backtracking
SPACE_RE = re.compile(r'\s')
NONESC_QUOTE_RE = re.compile(r'[^\\]"|^"')  # inefficient
QUOTE_RE = re.compile(r'(?<!\\)"') # backtracking, could also be improved
ESC_QUOTE_RE = re.compile(r'\\"')

def ParseCommandLineSlowly(line):
  """Parse line as if it were issued in a shell.

  Split the line into a list of string arguments indicated by spaces,
  except that doubly quoted substrings are treated atomically. Also,
  do allow backslash escaped quotes; they are turned into regular
  quotes.  This function is written for efficiency; only very simple
  regular expressions are used in main loop.

  The parser is not needed when the include server is driven by
  distcc, because the distcc client passes the argv vector. It is used
  as part of a faster parser.
  """

  if "'" in line:
    raise NotCoveredError("Single-quotes not accepted in command line.")
  args = []
  # Set position of first quote if it exists.
  m_unesc_q = NONESC_QUOTE_RE.search(line, 0)
  if m_unesc_q:
    unesc_q = m_unesc_q.end() - 1
  else:
    unesc_q = sys.maxsize
  m_nonspc = NONSPACE_RE.search(line, 0)
  if not m_nonspc:
    return args
  start = m_nonspc.start()
  end = start + 1
  while True:
    # Invariant: (1) start is at the beginning of the next argument
    # (perhaps at a quote, which will later be removed). (2) end is
    # such that line[start:end] is a prefix of the argument.
    assert start <= unesc_q
    assert start < end <= len(line), (start, end, len(line))
    assert not SPACE_RE.match(line, start)
    assert unesc_q == sys.maxsize or line[unesc_q] == '"'
    try:
      end = SPACE_RE.search(line, end).start()
    except AttributeError:
      end = len(line)
    if end < unesc_q:
      # We're good: no quotes found, we have an argument.
      args.append(ESC_QUOTE_RE.sub(
          '"',
          QUOTE_RE.sub(
            '',
            line[start:end])))
      # Search for beginning of next argument.
      try:
        start = NONSPACE_RE.search(line, end).start()
      except AttributeError:
        return args
      # We have one character so far.
      end = start + 1
      continue
    # We found a quote. Look for its counterpart.
    assert start <= unesc_q < end
    if unesc_q == len(line) - 1:
      raise NotCoveredError("""Unexpected '"' at end of line.""")
    m_unesc_q = NONESC_QUOTE_RE.search(line, unesc_q + 1)
    if not m_unesc_q:
      raise NotCoveredError("""Missing '"', could not parse command line.""")
    assert m_unesc_q.end() - 1 > unesc_q
    end = m_unesc_q.end()
    if end == len(line):
      args.append(ESC_QUOTE_RE.sub(
        '"',
        QUOTE_RE.sub(
        '',
        line[start:end])))
      return args
    # We found the counterpart before the end of the line. The argument may
    # still not be finished. But before continuing, look for the next quote.
    m_unesc_q = NONESC_QUOTE_RE.search(line, end)
    if m_unesc_q:
      unesc_q = m_unesc_q.end() - 1
    else:
      unesc_q = sys.maxsize


def ParseCommandLine(line):
  """Parse line as it were issued in a shell (optimized).
  """
  # It turns out that str.split() for large string (size 500) is almost two
  # orders of magnitude faster than ParseCommandLineSlowly. Usually, when
  # there is a '"' this quote is near the beginning of the line (as in dX="some
  # thing"). We use this observation to apply split() to the suffix following
  # the last quote. In that way, only the prefix up to somewhere around the last
  # quote needs to be parsed by more sophisticated means.
  quote_pos = line.rfind('"')
  if quote_pos == -1:
    return line.split()
  else:
    # Walk forward to a space; the quote could be an escaped one in
    # the middle of non-space characters.
    good_pos = line.find(' ', quote_pos)
    if good_pos != -1:
      return (ParseCommandLineSlowly(line[0:good_pos])
              + line[good_pos:].split())
    else: # give up
      return ParseCommandLineSlowly(line)

# Make a regular expression that matches suffixes of strings ending in
# a period followed by a string in the domain of TRANSLATION_UNIT_MAP.
TRANSLATION_UNIT_FILEPATH_RE = (
  re.compile(r".*[.](?P<suffix>%s)$" %
             '|'.join([re.escape(ext)
                       for ext in basics.TRANSLATION_UNIT_MAP.keys()])))


def ParseCommandArgs(args, current_dir, includepath_map, dir_map,
                     compiler_defaults, timer=None):
  """Parse arguments like -I to make include directory lists.

  Arguments:
    args: list of arguments (strings)
    current_dir: string
    includepath_map: a MapToIndex object
    dir_map: a DirectoryMapToIndex object
    compiler_defaults: a CompilerDefaults object
    timer: a basics.IncludeAnalyzerTimer object
  Returns:
    (quote_dirs, angle_dirs, files, source_file, source_file_prefix, dopts)
    where:
      quote_dirs: a list of dir_map-indexed directories
      angle_dirs: a list of dir_map-indexed directories
      files: a list of includepath_map-indexed files
      source_file_prefix: the source file name with extension stripped
      dopts: a list of items as returned by _SplitMacroArg
  Modifies:
    compiler_defaults
  """
  if __debug__: Debug(DEBUG_TRACE, "ParseCommand %s" % args)

  assert isinstance(dir_map, cache_basics.DirectoryMapToIndex)
  assert isinstance(includepath_map, cache_basics.MapToIndex)

  parse_state = ParseState()

  if len(args) < 2:
    raise NotCoveredError("Command line: too few arguments.")

  compiler = args[0]

  i = 1
  while i < len(args):
    # First, deal with everything that's not a flag-option
    if args[i][0] != '-' or args[i] == '-':     # - is the stdin file
      if args[i].startswith('"-'):
        pass     # TODO(csilvers): parse arg inside quotes?
      else:
        parse_state.file_names.append(args[i])  # if not a flag, it's a file
      i += 1
      continue

    # Deal with the one-letter options -- the kind most commonly seen.
    # We need to figure out whether the option-argument is glommed on to
    # the end of the option ("-Dfoo"), or is a separate word ("-D foo").
    action = CPP_OPTIONS_ONE_LETTER.get(args[i][1])   # letter after the -
    if action:
      arg = args[i][2:]
      if arg:                        # the glommed-onto-end case
        action(parse_state, arg)
        i += 1
      else:                          # the separate-word case
        try:
          action(parse_state, args[i+1])
          i += 2
        except IndexError:
          raise NotCoveredError("No argument found for option '%s'" % args[i])
      continue

    # Deal with the have-arg options with the arg as the 2nd word ("-MF foo").
    action = CPP_OPTIONS_TWO_WORDS.get(args[i])
    if action:
      try:
        action(parse_state, args[i+1])
        i += 2
      except IndexError:
        raise NotCoveredError("No argument found for option '%s'" % args[i])
      continue

    # Deal with the have-arg options that appear as if assignments
    # ("--sysroot=/mumble").
    if '=' in args[i]:
      arg, value = args[i].split('=', 1)
      action = CPP_OPTIONS_APPEARING_AS_ASSIGNMENTS.get(arg)
      if action:
        action(parse_state, value)
        i += 1
        continue

    # Deal with the options that take no arguments ("-nostdinc").
    action = CPP_OPTIONS_ONE_WORD.get(args[i])
    if action:
      action(parse_state)
      i += 1
      continue

    # Deal with the have-arg options with the arg concatenated to the word.
    # ("-MFfoo").  We do this last because it's slowest.
    if args[i][1] in CPP_OPTIONS_MAYBE_TWO_WORDS_FIRST_LETTERS:  # filter
      found_action = False
      for (option, action) in CPP_OPTIONS_MAYBE_TWO_WORDS.items():
        if action and args[i].startswith(option):
          action(parse_state, args[i][len(option):])
          i += 1
          found_action = True
          break
      if found_action:    # what we really need here is a goto!
        continue

    # Deal with the complex options requiring regular expressions last.
    if args[i].startswith(CPP_OPTIONS_REGULAR_EXPRESSIONS_STARTS_WITH):
      found_action = False
      for (option, action) in CPP_OPTIONS_REGULAR_EXPRESSIONS.items():
        r = CPP_OPTIONS_REGULAR_EXPRESSIONS_COMPILED[option]
        m = r.match(args[i])
        if action and m is not None:
          action(parse_state, m)
          i += 1
          found_action = True
          break
      if found_action:
        continue

    # Whatever is left must be a one-word option (that is, an option
    # without an arg) that it's safe to ignore.
    i += 1
    continue
  # Done parsing arguments!

  # Sanity-checking on arguments
  # -I- is a special form of the -I command.
  if "-" in parse_state.i_dirs:
    _RaiseNotImplemented('-I-', '(Use -iquote instead.)')

  if len(parse_state.file_names) != 1:
    raise NotCoveredError(
      "Could not locate name of translation unit: %s." % parse_state.file_names,
      send_email=False)

  source_file = parse_state.file_names[0]

  if parse_state.output_file:
    # Use output_file to create prefix
    source_file_prefix = re.sub("[.]o$", "", parse_state.output_file)
  else:
    # Remove suffix from source file
    source_file_prefix = re.sub("[.](%s)$" %
                                  "|".join(basics.TRANSLATION_UNIT_MAP.keys()),
                                  "",
                                  source_file)
  source_file_prefix = os.path.join(current_dir, source_file_prefix)
  if parse_state.language == 'none':    # no explicit -x flag, or -x none
    language_match = TRANSLATION_UNIT_FILEPATH_RE.match(source_file)
    if not language_match:
      raise NotCoveredError(
          "For source file '%s': unrecognized filename extension" % source_file)
    suffix = language_match.group('suffix')
    parse_state.language = basics.TRANSLATION_UNIT_MAP[suffix]
  assert parse_state.language in basics.LANGUAGES

  sysroot = parse_state.include_sysroot()
  compiler_defaults.SetSystemDirsDefaults(compiler, sysroot,
                                          parse_state.language, timer)

  def IndexDirs(dir_list):
    """Normalize directory names and index.

    Remove leading "./" and trailing "/"'s from directory paths in
    dir_list before indexing them according to dir_map.
    """
    S = basics.SafeNormPath
    I = dir_map.Index
    return [I(S(d)) for d in dir_list]

  # Now string the directory lists together according to CPP semantics.
  angle_dirs = IndexDirs(parse_state.i_dirs)
  angle_dirs.extend(IndexDirs(parse_state.before_system_dirs))
  if not parse_state.nostdinc:
    sysroot = parse_state.include_sysroot()
    angle_dirs.extend(
      IndexDirs(compiler_defaults.system_dirs_default
                [compiler][sysroot][parse_state.language]))
  angle_dirs.extend(IndexDirs(parse_state.after_system_dirs))

  quote_dirs = IndexDirs(parse_state.quote_dirs)
  quote_dirs.extend(angle_dirs)
  angle_dirs = tuple(angle_dirs)
  quote_dirs = tuple(quote_dirs)
  # Include files are meant to be sent to the server.  They do not pose the
  # danger of absolute includes, which includepath_map is designed to avoid.
  include_files = tuple(
      [includepath_map.Index(basics.SafeNormPath(f),
                             ignore_absolute_path_warning=True)
       for f in parse_state.include_files])

  if __debug__: Debug(DEBUG_TRACE, ("ParseCommand result: %s %s %s %s %s %s" %
                                    (quote_dirs, angle_dirs, include_files,
                                     source_file, source_file_prefix,
                                     parse_state.Dopts)))
  return (quote_dirs, angle_dirs, include_files, source_file, source_file_prefix,
          parse_state.Dopts)
