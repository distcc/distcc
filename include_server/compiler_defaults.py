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


def _RealPrefixWithinClientRoot(client_root, path):
  """Determine longest directory prefix of PATH and whether PATH contains a symlink.

  Given an absolute path CLIENT_ROOT and an absolute path PATH that is
  interpreted as relative to CLIENT_ROOT, figure out the longest prefix
  of PATH such that every component of the prefix is a directory -- not
  a file or symlink -- when interpreted relative to CLIENT_ROOT.

  Args:
    path: a string starting with '/'
  Returns:
    a pair consisting of
    - the prefix
    - a bool, which is True iff PATH contained a symlink.
  """
  prefix = "/"
  parts = path.split('/')
  while prefix != path:
    part = parts.pop(0)
    last_prefix = prefix
    prefix = os.path.join(prefix, part)
    if os.path.islink(client_root + prefix):
      return last_prefix, True
    if not os.path.isdir(client_root + prefix):
      return last_prefix, False
  return path, False


def _MakeLinkFromMirrorToRealLocation(system_dir, client_root, system_links):
  """Create a link under client root what will resolve to system dir on server.

  See comments for CompilerDefaults class for rationale.

  Args:
    system_dir: a path such as /usr/include or
                /usr/lib/gcc/i486-linux-gnu/4.0.3/include
    client_root: a path such as /dev/shm/tmpX.include_server-X-1
    system_links: a list of paths under client_root; each denotes a symlink

  The link is created only if necessary. So,
    /usr/include/gcc/i486-linux-gnu/4.0.3/include
  is not created if
    /usr/include
  is already in place, since it's a prefix of the longer path.

  If a link is created, the symlink name will be appended to system_links.

  For example, if system_dir is '/usr/include' and client_root is
  '/dev/shm/tmpX.include_server-X-1', then this function will create a
  symlink in /dev/shm/tmpX.include_server-X-1/usr/include which points
  to ../../../../../../../../../../../../usr/include, and it will append
  '/dev/shm/tmpX.include_server-X-1/usr/include' to system_links.
  """
  if not system_dir.startswith('/'):
    raise ValueError("Expected absolute path, but got '%s'." % system_dir)
  if os.path.realpath(system_dir) != system_dir:
    raise NotCoveredError(
        "Default compiler search path '%s' must be a realpath." %s)
  # Typical values for rooted_system_dir:
  #  /dev/shm/tmpX.include_server-X-1/usr/include
  real_prefix, is_link = _RealPrefixWithinClientRoot(client_root, system_dir)
  parent = os.path.dirname(system_dir)
  rooted_system_dir = client_root + system_dir
  rooted_parent = client_root + parent
  if real_prefix == system_dir:
    # rooted_system_dir already exists as a real (non-symlink) path.
    # Make rooted_system_dir a link.
    #
    # For example, this could happen if /usr/include/c++/4.0 and
    # /usr/include are both default system directories.
    # First we'd call this function with /usr/include/c++/4.0,
    # and it would call os.mkdirdirs() to create
    # /dev/shm/tmpX.include_server-X-1/usr/include/c++,
    # and then it would create a symlink named 4.0 within that.
    # Then we'd call this function again with /usr/include.
    # In this case, we can replace the whole subtree with a single symlink
    # at /dev/shm/tmpX.include_server-X-1/usr/include.
    shutil.rmtree(rooted_system_dir)
    system_links[:] = filter(lambda path :
                             not path.startswith(rooted_system_dir),
                             system_links)
  elif real_prefix == parent:
    # The really constructed path does not extend beyond the parent directory,
    # so we're all set to create the link if it's not already there.
    if os.path.exists(rooted_system_dir):
      assert os.path.islink(rooted_system_dir)
      return
  elif not is_link:
    os.makedirs(rooted_parent)
  else:
    # A link above real_prefix has already been created with this routine.
    return
  assert _RealPrefixWithinClientRoot(client_root, parent) == (parent, False), (client_root, parent)
  depth = len([c for c in system_dir if c == '/'])
  # The more directories on the path system_dir, the more '../' need to
  # appended. We add enough '../' to get to the root directory. It's OK
  # if we have too many, since '..' in the root directory points back to
  # the root directory.
  # TODO(klarlund,fergus): do this in a more principled way.
  # This probably requires changing the protocol.
  os.symlink('../' * (basics.MAX_COMPONENTS_IN_SERVER_ROOT + depth)
             + system_dir[1:],  # remove leading '/'
             rooted_system_dir)
  system_links.append(rooted_system_dir)


def _SystemSearchdirsGCC(compiler, sysroot, language, canonical_lookup):
  """Run gcc on empty file; parse output to figure out default paths.

  This function works only for gcc, and only some versions at that.

  Arguments:
    compiler: a filepath (the first argument on the distcc command line)
    sysroot: the --sysroot passed to the compiler ("" to disable)
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

  command = [compiler]
  if sysroot:
    command += ["--sysroot=" + sysroot]
  command += ["-x", language, "-v", "-c", "/dev/null", "-o", "/dev/null"]
  Debug(DEBUG_DATA, "system search dirs command: %s" % command)

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
    if 'PATH' in os.environ:
      trimmed_env = {'PATH': os.environ['PATH']}
    else:
      trimmed_env = {}
    p = subprocess.Popen(command,
                         shell=False,
                         stdin=None,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT,
                         env=trimmed_env,universal_newlines=True)
    out = p.communicate()[0]
  except (IOError, OSError) as why:
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
    "%s\n(.*?)\n%s"  # don't ask
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
           for line in match_obj.group(1).split("\n")
           for directory in line.split()
           # Ignore Apple-modified MacOS gcc's "framework" directories.
           if not line.endswith(" (framework directory)")
           ]
           # TODO: Rather than just ignoring framework directories, we
           # should handle them properly, fully emulating the search
           # algorithm used by Apple's modified GCC.
           # The search algorithm used for framework directories is not very
           # well documented, as far as I can tell, but the source code is in
           # gcc/config/darwin-c.c in the Apple GCC sources.
           # From a quick glance, I think it looks like this:
           # - For each #include of the form Foo/bar.h,
           #        For each framework directory Baz,
           #            Look in Baz/Foo.framework/Headers/bar.h
           #            and in Baz/Foo.framework/PrivateHeaders/bar.h
           # - If the regular search fails, look for subframeworks.
           #     For each #include of the form Foo/bar.h
           #       from Baz/Quux.framework/Headers/whatever.h
           #            Look in Baz/Quux.framework/Frameworks/Foo/Headers/bar.h.

class CompilerDefaults(object):
  """Records and caches the default search dirs and creates symlink farm.

  This function works only for gcc, and only some versions at that,
  because we parse the output from gcc to determine the default search dirs.

  The 'default' searchdirs are those on the search-path that are built in, that
  is known to the preprocessor, as opposed to being set on the commandline via
  -I et al.

  When we pass an option such as -I/foo/bar to the server,
  the server will rewrite it to say -I/server/path/root/foo/bar,
  where /server/path/root is the temporary directory on the server
  that corresponds to root on the client (e.g. typically /dev/shm/distccd_nnn).
  This causes problems in this case of -I options such as -I/usr/include/foo,
  where the path contains a 'default' search directory (in this case
  /usr/include) as a prefix.
  Header files under the system default directories are assumed to exist
  on the server, and it would be expensive to send them to the server
  unnecessarily (we measured it, and it slowed down the build of Samba by 20%).
  So for -I options like -I/usr/include/foo, we want the server
  to use /usr/include/foo on the server, not /server/path/root/usr/include/foo.

  Because the server unconditionally rewrites include search
  paths on the command line to be relative to the server root, we must take
  corrective action when identifying default system dirs: references to files
  under these relocated system directories must be redirected to the absolute
  location where they're actually found.

  To do so, we create a symlink forest under client_root.
  This will contain symlinks of the form

    usr/include -> ../../../../../../../../../../../../usr/include

  After being sent to the server, the server will rewrite them as

    /server/path/root/usr/include ->
       /server/path/root/../../../../../../../../../../../../usr/include

  which will make

     /server/path/root/usr/include

  become a symlink to

     /usr/include

  Consequently, an include search directory such as -I /usr/include/foo will
  work on the server, even after it has been rewritten to:

    -I /server/path/root/usr/include/foo
  """

  def __init__(self, canonical_lookup, client_root):
    """Constructor.

    Instance variables:
      system_dirs_real_paths: a dictionary such that
        system_dirs_real_paths[c][lang] is a list of directory paths
        (strings) for compiler c and language lang
      system_dirs_default: a list of all such strings, subjected to
        realpath-ification, for all c and lang
      client_root: a path such as /dev/shm/tmpX.include_server-X-1
      system_links: locations under client_root representing system default dirs
    """
    self.canonical_lookup = canonical_lookup
    self.system_dirs_default_all = set([])
    self.system_dirs_default = {}
    self.system_links = []
    self.client_root = client_root

  def SetSystemDirsDefaults(self, compiler, sysroot, language, timer=None):
    """Set instance variables according to compiler, and make symlink farm.

    Arguments:
      compiler: a filepath (the first argument on the distcc command line)
      sysroot: the --sysroot passed to the compiler ("" to disable)
      language: 'c' or 'c++' or other item in basics.LANGUAGES
      timer: a basis.IncludeAnalyzerTimer or None

    The timer will be disabled during this routine because the select involved
    in Popen calls does not handle SIGALRM.

    See also the class documentation for this class.
    """
    assert isinstance(compiler, str)
    assert isinstance(language, str)
    Debug(DEBUG_TRACE,
          "SetSystemDirsDefaults with CC, SYSROOT, LANG: %s, %s, %s" %
          (compiler, sysroot, language))
    if compiler in self.system_dirs_default:
      if sysroot in self.system_dirs_default[compiler]:
        if language in self.system_dirs_default[compiler][sysroot]:
          return
      else:
        self.system_dirs_default[compiler][sysroot] = {}
    else:
      self.system_dirs_default[compiler] = {sysroot: {}}
    try:
      if timer:
        # We have to disable the timer because the select system call that is
        # executed when calling the compiler through Popen gives up if presented
        # with a SIGALRM.
        timer.Stop()
      self.system_dirs_default[compiler][sysroot][language] = (
        _SystemSearchdirsGCC(compiler,
                             sysroot, language, self.canonical_lookup))
      Debug(DEBUG_DATA,
            "system_dirs_default[%s][%s][%s]: %s" %
            (compiler, sysroot, language,
             self.system_dirs_default[compiler][sysroot][language]))
      # Now summarize what we know and add to system_dirs_default_all.
      self.system_dirs_default_all |= (
          set(self.system_dirs_default[compiler][sysroot][language]))
      # Construct the symlink farm for the compiler default dirs.
      for system_dir in self.system_dirs_default[compiler][sysroot][language]:
        _MakeLinkFromMirrorToRealLocation(system_dir, self.client_root,
                                          self.system_links)
    finally:
      if timer:
        timer.Start()
