#! /usr/bin/python2.4

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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 
"""Divination of built-in system directories used by compiler installation.

It is undesirable for the distcc-pump to send header files that reside
under the built-in search path.  In a correct compiler installation,
these files must already be present on the server. This module lets
the distcc-pump run the compiler in a special mode that allows the
built-in system directories to be revealed.

The current code is tested only for gcc 4.1.1.

TODO(klarlund) Find out what other versions this code works for.
TODO(klarlund) The include server halts if the built-in system
directories cannot be determined. Should this be improved upon?
"""

__author__ = "Nils Klarlund"


import os
import re
import sys
import basics
import subprocess

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
DEBUG_DATA = basics.DEBUG_DATA
NotCoveredError = basics.NotCoveredError


def _SystemSearchdirsGCC(compiler, language, canonical_lookup):
  """Run gcc on empty file; parse output to figure out default paths.

  Arguments:
    compiler: a filepath (the first argument on the distcc command line)
    language: 'c' or 'c++' or other item in basics.LANGUAGES
    canonical_lookup: a function that maps strings to their realpaths
  Returns:
    list of system search dirs for this compiler and language

  """

  # We are trying to wring the following kind of text out of the
  # compiler:
  #--------------------
  # blah. blah.
  # ...
  # blah. blah.
  # #include "..." search starts here:
  # #include <...> search starts here:
  #  /usr/local/include
  #  /usr/lib/gcc/i486-linux-gnu/4.0.3/include
  #  /usr/include
  # End of search list.
  # blah. blah.
  #------------

  command = [compiler, "-x", language, "-v", "-c", "/dev/null", "-o",
             "/dev/null"]

  try:
    # We clear the environment, because otherwise, directories declared by
    # CPATH, for example, will be incorporated into the result. (See the CPP
    # manual for the meaning of CPATH.)  The only thing we keep is PATH,
    # so we can be sure to find the compiler.
    # TODO(csilvers): it's possible we could need to pass in some
    # other environment vars, like LD_LIBRARY_PATH.  Instead of adding
    # in more env-vars by hand, consider just removing from os.environ
    # all the env-vars that are meaningful to gcc, such as CPATH.  See
    # http://docs.freebsd.org/info/gcc/gcc.info.Environment_Variables.html,
    # or the "Environment Variables Affecting GCC" section of the gcc
    # info page.
    p = subprocess.Popen(command,
                         shell=False,
                         stdin=None,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT,
                         env={'PATH': os.environ['PATH']})
    out = p.communicate()[0]
  except (IOError, OSError), why:
    raise NotCoveredError (
             ( "Couldn't determine default system include directories\n"
             + "for compiler '%s', language '%s':\n"
             + "error executing '%s': %s.")
             % (compiler, language, command, why))

  if p.returncode != 0:
    raise NotCoveredError(
             ( "Couldn't determine default system include directories\n"
             + "for compiler '%s', language '%s':\n"
             + "command '%s' exited with status '%d'.\n Command output:\n%s") %
             (compiler, language, command, p.returncode, out))

  match_obj = re.search(
    r"%s\n(.*?)\n%s"  # don't ask
    % ("#include <...> search starts here:", "End of search list"),
    out,
    re.MULTILINE + re.DOTALL)
  if match_obj == None:
    raise NotCoveredError(
             ( "Couldn't determine default system include directories\n"
             + "for compiler '%s', language '%s':\n"
             + "couldn't parse output of '%s'.\nReceived:\n%s") %
             (compiler, language, command, out))
  return [ canonical_lookup(directory)
           for directory in match_obj.group(1).split() ]


class CompilerDefaults(object):
  """Records and caches the default searchdirs, aka include directories
  or search-path.  The 'default' searchdirs are those built in to the
  preprocessor, as opposed to being set on the commandline via -I et al.

  This scheme works only for gcc, and only some versions at that.
  """

  def __init__(self, canonical_lookup):
    """Constructor.

    Instance variables:
      system_dirs_real_paths: a dictionary such that
        system_dirs_real_paths[c][lang] is a list of directory paths
        (strings) for compiler c and language lang
      system_dirs_default: a list of all such strings, subjected to
        realpath-ification, for all c and lang
    """
    self.canonical_lookup = canonical_lookup
    self.system_dirs_default_all = set([])
    self.system_dirs_default = {}


  def SetSystemDirsDefaults(self, compiler, timer=None):
    """Set instance variables according to compiler.

    Arguments:
      compiler: a string "c", "c++",...
      timer: a basis.IncludeAnalyzerTimer or None

    The timer will be disabled during this routine because the select involved
    in Popen calls does not handle SIGALRM.
    
    See also the constructor documentation for this class.
    """
    assert isinstance(compiler, str)
    Debug(DEBUG_TRACE, "SetSystemDirsDefauls with CC: %s" % compiler)
    if compiler in self.system_dirs_default: return
    try:
      if timer:
        # We have to disable the timer because the select system call that is
        # executed when calling the compiler through Popen gives up if presented
        # with a SIGALRM.
        timer.Stop()
      self.system_dirs_default[compiler] = {}
      # Try 'c', 'c++', ...
      for language in basics.LANGUAGES:
        self.system_dirs_default[compiler][language] = (
          _SystemSearchdirsGCC(compiler, language, self.canonical_lookup))
        Debug(DEBUG_DATA,
              "system_dirs_default[%s][%s]: %s" %
              (compiler, language,
               self.system_dirs_default[compiler][language]))
      # Now summarize what we know and add to system_dirs_default_all.
      self.system_dirs_default_all |= set(
        [ _default
          for _compiler in self.system_dirs_default
          for _language in self.system_dirs_default[_compiler]
          for _default in self.system_dirs_default[_compiler][_language] ])
    finally:
      if timer:
        timer.Start()
