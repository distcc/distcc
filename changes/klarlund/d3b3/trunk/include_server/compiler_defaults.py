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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
# USA.

 
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
import shutil
import subprocess

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
DEBUG_DATA = basics.DEBUG_DATA
NotCoveredError = basics.NotCoveredError

  
def _RealPrefix(path):
  """Determine the longest real prefix and whether its extension is a link."

  Args:
    path: a string starting with '/'
  Returns:
    a pair consisting of:
    - a list of directory components of path, corresponding to a prefix that is a
      a file and directory that really exists (that is symlinks don't count), and
    - a Boolean that is true if this prefix is not path and its extension by 
      the next directory of path is a symlink.
  """
  prefix = "/"
  parts = path.split('/')
  while prefix != path:
    part = parts.pop(0)
    last_prefix = prefix
    prefix = os.path.join(prefix, part)
    if os.path.islink(prefix):
      return last_prefix, True
    if not os.path.isdir(prefix):
      return last_prefix, False
  return path, False


def _MakeLinkFromMirrorToRealLocation(system_dir, client_root, system_links):
  """Create a link under client root what will resolve to system dir on server.

  Args:
    system_dir: a path such as /usr/include or
                /usr/lib/gcc/i486-linux-gnu/4.0.3/include
    client_root: a path such as /dev/shm/tmpX.include_server-X-1
    system_links: a list of paths under client_root; each denotes a symlink

  The link is created only if necessary. So,
    /usr/lib/gcc/i486-linux-gnu/4.0.3/include
  is not created if
    /usr/include
  is already in place.  """
  if not system_dir.startswith('/'):
    raise ValueError("Expected absolute path, but got '%s'." % system_dir)
  if not os.path.realpath(system_dir) == system_dir:
    raise NotCoveredError(
        "Default compiler search path '%s' must be a realpath." %s)
  rooted_system_dir = client_root + system_dir
  # Typical values for rooted_system_dir:
  #
  #  /dev/shm/tmpX.include_server-X-1/usr/include
  make_link = False
  real_prefix, is_link = _RealPrefix(rooted_system_dir)
  parent = os.path.dirname(rooted_system_dir)
  if real_prefix == rooted_system_dir:
    # rooted_system_dir already exists as a real path. Make rooted_system_dir a
    # link.
    shutil.rmtree(rooted_system_dir)
    make_link = True
  elif real_prefix == parent:
    # The really constructed path does not extend beoynd the parent directory,
    # so we're all set to create the link if it's not already there.
    if os.path.exists(rooted_system_dir):
      assert os.path.islink(rooted_system_dir)
    else:
      make_link = True
  elif not is_link:
    os.makedirs(parent)
    make_link = True
  else:
    # A link above real_prefix has already been created with this routine.
    pass
  if make_link:
    assert _RealPrefix(parent) == (parent, False), parent
    depth = len([c for c in system_dir if c == '/'])
    # The more directories on the path system_dir, the more '../' need to
    # appended. We add 
    os.symlink('../' * (basics.MAX_COMPONENTS_IN_SERVER_ROOT + depth + 3)
               + system_dir[1:],  # remove leading '/'
               rooted_system_dir)
    system_links.append(rooted_system_dir)


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
    # We clear the environment, because otherwise, directories
    # declared by CPATH, for example, will be incorporated into the
    # result. (See the CPP manual for the meaning of CPATH.)  The only
    # thing we keep is PATH, so we can be sure to find the compiler.
    # NOTE: having the full PATH can be tricky: what if there's a gcc
    # -> distcc symlink somewhere on the PATH, before the real gcc?
    # We think the right thing will happen here, but it's complicated.
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
  """Records and caches the default search dirs and creates symlink farm.

  The 'default' searchdirs are those on the search-path that are built in, that
  is known to the preprocessor, as opposed to being set on the commandline via
  -I et al.

  Because header files under the system default directories are assumed to exist
  on the server and because the server unconditionally rewrites include search
  paths on the command line to be relative to the server root, we must take
  corrective action when identifying default system dirs: references to files
  under these relocated system directories must be redirected to the absolute
  location where they're actually found.

  To do so, we create a symlink forest under client_root that after being
  sent to the server wil make

     /server/path/root/usr/include

  become a symlink to

     /usr/include

  Consequently, an include search directory such as -I /usr/include/foo will work on
  the server, even after it has been rewritten to:

    -I /server/path/root/usr/include/foo
 
  This function works only for gcc, and only some versions at that.
  """

  def __init__(self, canonical_lookup, client_root):
    """Constructor.

    Instance variables:
      system_dirs_real_paths: a dictionary such that
        system_dirs_real_paths[c][lang] is a list of directory paths
        (strings) for compiler c and language lang
      system_dirs_default: a list of all such strings, subjected to
        realpath-ification, for all c and lang
      system_links: locations under client_root representing system default dirs
    """
    self.canonical_lookup = canonical_lookup
    self.system_dirs_default_all = set([])
    self.system_dirs_default = {}
    self.system_links = []
    self.client_root = client_root

  def SetSystemDirsDefaults(self, compiler, language, timer=None):
    """Set instance variables according to compiler.

    Arguments:
      compiler: a filepath (the first argument on the distcc command line)
      language: 'c' or 'c++' or other item in basics.LANGUAGES
      timer: a basis.IncludeAnalyzerTimer or None

    The timer will be disabled during this routine because the select involved
    in Popen calls does not handle SIGALRM.
    
    See also the constructor documentation for this class.
    """
    assert isinstance(compiler, str)
    assert isinstance(language, str)
    Debug(DEBUG_TRACE, "SetSystemDirsDefaults with CC, LANG: %s, %s" %
                       (compiler, language))
    if compiler in self.system_dirs_default:
      if language in self.system_dirs_default[compiler]:
        return
    else:
      self.system_dirs_default[compiler] = {}
    try:
      if timer:
        # We have to disable the timer because the select system call that is
        # executed when calling the compiler through Popen gives up if presented
        # with a SIGALRM.
        timer.Stop()
      self.system_dirs_default[compiler][language] = (
        _SystemSearchdirsGCC(compiler, language, self.canonical_lookup))
      Debug(DEBUG_DATA,
            "system_dirs_default[%s][%s]: %s" %
            (compiler, language,
             self.system_dirs_default[compiler][language]))
      # Now summarize what we know and add to system_dirs_default_all.
      self.system_dirs_default_all |= (
          set(self.system_dirs_default[compiler][language]))
      #
      for system_dir in self.system_dirs_default[compiler][language]:
        _MakeLinkFromMirrorToRealLocation(system_dir, self.client_root,
                                          self.system_links)
    finally:
      if timer:
        timer.Start()
