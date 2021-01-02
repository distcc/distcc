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

"""The skeleton for an include analyzer.

This module defines the basic caches and helper functions for an
include analyzer.
"""

__author__ = "Nils Klarlund"

import os
import glob

import basics
import macro_eval
import parse_file
import parse_command
import statistics
import cache_basics
import mirror_path
import compress_files

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
NotCoveredError = basics.NotCoveredError

class IncludeAnalyzer(object):
  """The skeleton, including caches, of an include analyzer."""

  def _InitializeAllCaches(self):
    # Make cache for parsed files.
    self.file_cache = {}
    # Make table for symbols in #define's.
    self.symbol_table = {}
    # Erect the edifice of caches.
    caches = self.caches = (
        cache_basics.SetUpCaches(self.client_root_keeper.client_root))

    # Migrate the cache stuff to self namespace.
    self.includepath_map = caches.includepath_map
    self.directory_map = caches.directory_map
    self.realpath_map = caches.realpath_map

    self.canonical_path = caches.canonical_path
    self.dirname_cache = caches.dirname_cache
    self.compiler_defaults = caches.compiler_defaults
    self.systemdir_prefix_cache = caches.systemdir_prefix_cache

    self.simple_build_stat = caches.simple_build_stat
    self.build_stat_cache = caches.build_stat_cache

    self.IsIncludepathIndex = caches.IsIncludepathIndex
    self.IsSearchdirIndex = caches.IsSearchdirIndex
    self.IsCurrdirIndex = caches.IsCurrdirIndex
    self.IsRealpathIndex = caches.IsRealpathIndex
    self.IsFilepathPair = caches.IsFilepathPair

    # Make a cache for the symbolic links encountered; also for their
    # replication into root directory.
    self.mirror_path = mirror_path.MirrorPath(self.simple_build_stat,
                                              self.canonical_path,
                                              self.realpath_map,
                                              self.systemdir_prefix_cache)
    # Make a parser for C/C++.
    self.parse_file = parse_file.ParseFile(self.includepath_map)
    # Make a compressor for source files.
    self.compress_files = compress_files.CompressFiles(self.includepath_map,
                                                       self.directory_map,
                                                       self.realpath_map,
                                                       self.mirror_path)
    # A fast cache for avoiding calls into the mirror_path object.
    self.mirrored = set([])

    # For statistics only. We measure the different search lists
    # (search paths) by accumulating them all in sets.
    self.quote_dirs_set = set([]) # quote search lists
    self.angle_dirs_set = set([]) # angle searchlists
    self.include_dir_pairs = set([]) # the pairs (quote search list,
                                     # angle search lists)

  def __init__(self, client_root_keeper, stat_reset_triggers={}):
    self.generation = 1
    self.client_root_keeper = client_root_keeper
    self.client_root_keeper.ClientRootMakedir(self.generation)
    self.stat_reset_triggers = stat_reset_triggers
    self.translation_unit = "unknown translation unit"
    self.timer = None
    self.include_server_cwd = os.getcwd()
    self._InitializeAllCaches()

  def _ProcessFileFromCommandLine(self, fpath, currdir, kind, search_list):
    """Return closure of fpath whose kind is "translation unit" or "include".
       Such files come from the command line, either as the file to compile,
       or from a "-include" command line option.
    Arguments:
      fpath: a filepath (as a string)
      currdir: a string
      kind: a string used for an error message if fpath is not found
      search_list: a tuple of directory indices (for "include" kind files)
    Returns:
      an include closure calculated by RunAlgorithm
    """
    # We allow the filepath to be absolute. We do not tolerate absolute
    # includepaths, in general, and so must be careful here, because we use
    # build_stat_cache.Resolve. We prepare to use the searchdir parameter of
    # Resolve.
    if os.path.isabs(fpath):
      file_dirpath, file_filename = os.path.split(fpath)
    else:
      # Use empty string as directory name (offset from currdir)
      file_dirpath, file_filename = "", fpath
    fpath_resolved_pair, fpath_real = self.build_stat_cache.Resolve(
      self.includepath_map.Index(file_filename),
      self.currdir_idx,
      self.directory_map.Index(file_dirpath),
      search_list)
    if fpath_resolved_pair == None:
      raise NotCoveredError("Could not find %s '%s'." % (kind, fpath),
                            send_email=False)
    # We must inspect the path to replicate directories and symlinks.
    self.mirror_path.DoPath(
        os.path.join(currdir, fpath),
        self.currdir_idx,
        self.client_root_keeper.client_root)

    closure = self.RunAlgorithm(fpath_resolved_pair, fpath_real)
    return closure

  def ProcessCompilationCommand(self, currdir, parsed_command):
    """Do the include analysis for parsed_command.

    Precondition:
      currdir == os.getcwd()

    Arguments:
      currdir: a string denoting an absolute filepath when command is run
      parsed_command: the value returned by ParseCommandArgs

    Returns:
      an include closure as described in RunAlgorithm
    """

    Debug(DEBUG_TRACE, "ProcessCompilationCommand: %s, %s"
          % (currdir, parsed_command))

    assert isinstance(currdir, str)
    statistics.parse_file_counter_last = statistics.parse_file_counter
    (self.quote_dirs, self.angle_dirs,
     self.include_files, translation_unit,
     self.result_file_prefix, self.d_opts) = parsed_command

    statistics.translation_unit = translation_unit
    self.translation_unit = translation_unit

    self.currdir_idx = self.directory_map.Index(currdir)

    # Statistics only.
    self.include_dir_pairs |= set([(self.quote_dirs, self.angle_dirs)])
    self.quote_dirs_set.add(self.quote_dirs)
    self.angle_dirs_set.add(self.angle_dirs)
    statistics.quote_path_total += len(self.quote_dirs)
    statistics.angle_path_total += len(self.angle_dirs)

    total_closure = {}
    for include_file in self.include_files:
      total_closure.update(
        self._ProcessFileFromCommandLine(
          self.includepath_map.string[include_file],
          currdir,
          "include file",
          self.quote_dirs))
    total_closure.update(self._ProcessFileFromCommandLine(translation_unit,
                                                          currdir,
                                                          "translation unit",
                                                          ()))
    return total_closure

  def DoStatResetTriggers(self):
    """Reset stat caches if a glob evaluates differently from earlier.

    More precisely, if a path of a glob comes in or out of existence or has a
    new stamp, then reset stat caches."""

    trigger_map = self.stat_reset_triggers
    old_paths = [ path
                  for glob_expr in trigger_map
                  for path in trigger_map[glob_expr] ]
    for glob_expr in trigger_map:
      for path in glob.glob(glob_expr):
        try:
          old_paths.remove(path)
        except ValueError:
          pass
        new_stamp = basics.Stamp(path)
        if path in trigger_map[glob_expr]:
          if new_stamp != trigger_map[glob_expr][path]:
            Debug(basics.DEBUG_WARNING,
                  "Path '%s' changed. Clearing caches.",
                  path)
            trigger_map[glob_expr][path] = new_stamp
            self.ClearStatCaches()
            return
        else:
          Debug(basics.DEBUG_WARNING,
                "Path '%s' came into existence. Clearing caches.",
                path)
          trigger_map[glob_expr][path] = basics.Stamp(path)
          self.ClearStatCaches()
          return
    if old_paths:
      path = old_paths[0]
      Debug(basics.DEBUG_WARNING,
            "Path '%s' no longer exists. Clearing caches.",
            path)
      self.ClearStatCaches()

  def DoCompilationCommand(self, cmd, currdir, client_root_keeper):
    """Parse and and process the command; then gather files and links."""

    self.translation_unit = "unknown translation unit"  # don't know yet

    # Any relative paths in the globs in the --stat_reset_trigger argument
    # must be evaluated relative to the include server's original working
    # directory.
    os.chdir(self.include_server_cwd)
    self.DoStatResetTriggers()

    # Now change to the distcc client's working directory.
    # That'll let us use os.path.join etc without including currdir explicitly.
    os.chdir(currdir)

    parsed_command = (
        parse_command.ParseCommandArgs(cmd,
                                       currdir,
                                       self.includepath_map,
                                       self.directory_map,
                                       self.compiler_defaults,
                                       self.timer))
    (unused_quote_dirs, unused_angle_dirs, unused_include_files, source_file,
     result_file_prefix, unused_Dopts) = parsed_command

    # Do the real work.
    include_closure = (
      self.ProcessCompilationCommand(currdir, parsed_command))
    # Cancel timer before I/O in compress_files.
    if self.timer:  # timer may not always exist when testing
      self.timer.Cancel()
    # Get name of the initial source file
    translation_unit = self.translation_unit
    # Links are accumulated intra-build (across different compilations in a
    # build). We send all of 'em every time.  This will potentially lead to
    # performance degradation for large link farms. We expect at most a
    # handful. We add put the system links first, because there should be very
    # few of them.
    links = self.compiler_defaults.system_links + self.mirror_path.Links()
    files = self.compress_files.Compress(include_closure, client_root_keeper,
                                         self.currdir_idx)

    files_and_links = files + links

    # Note that the performance degradation comment above applies especially
    # to forced include directories, unless disabled with --no_force_dirs
    if basics.opt_no_force_dirs == False:
      files_and_links += self._ForceDirectoriesToExist()

    realpath_map = self.realpath_map

    if basics.opt_verify:
      # Invoke the real preprocessor.
      exact_no_system_header_dependency_set = (
        ExactDependencies(" ".join(cmd),
                          realpath_map,
                          self.systemdir_prefix_cache,
                          translation_unit))
      if basics.opt_write_include_closure:
        WriteDependencies(exact_no_system_header_dependency_set,
                          self.result_file_prefix + '.d_exact',
                          realpath_map)
      VerifyExactDependencies(include_closure,
                              exact_no_system_header_dependency_set,
                              realpath_map,
                              translation_unit)
    if basics.opt_write_include_closure:
      WriteDependencies(include_closure,
                        self.result_file_prefix + '.d_approx',
                        realpath_map)
    return files_and_links

  def _ForceDirectoriesToExist(self):
    """Force any needed directories to exist.

    In rare cases, the source files may contain #include "foo/../bar",
    but may not contain any other files from the "foo" directory.
    In such cases, we invent a dummy file in (the mirrored copy of)
    each such directory, just to force the distccd server to create the
    directory, so that the C compiler won't get an error when it tries
    to resolve that #include.

    Returns:
      A list of files to pass as dummy inputs.
    """

    must_exist_dirs = self.mirror_path.MustExistDirs()
    # Note: distcc's --scan-includes option needs to
    # know about this name; see ../src/compile.c.
    special_name = 'forcing_technique_271828'
    forcing_files = [d + '/' + special_name
                     for d in must_exist_dirs]
    for forcing_file in forcing_files:
      # If for extremely obscure reasons the file already exists and is useful,
      # then don't change it: that's why we open in "append" mode.
      open(forcing_file, "a").close()
    return forcing_files

  def RunAlgorithm(self, filepath_resolved_pair, filepath_real_idx):
    """Run FindNode on filepath; then compute include closure.
    Arguments:
      filepath_resolved_pair: (directory_idx, includepath_idx)
      filepath_real: the realpath_map index corresponding to
        filepath_resolved_pair
    Returns:
      include_closure: a dictionary.

    The include_closure consists of entries of the form

        realpath_idx: [(searchdir_idx_1, includepath_idx_1),
                       (searchdir_idx_2, includepath_idx_2), ...]

    where searchdir_i is an absolute path.  realpath_idx is a realpath
    index corresponding to a single #include (more exactly, it's the
    index of the path that the #include resolves to).

    This include closure calculation omits any system header files,
    that is, header files found in a systemdir (recall systemdirs are
    those searchdirs that are built into the preprocessor, such as
    "/usr/include").  It concentrates only on header files users might
    edit.

    The keys are the most important part of the include_closure; the
    values are used only to munge the preprocessor output to give more
    useful filenames via the #line directive.  The issue here is that
    source files in the distcc system are not in their "proper"
    locations: for instance, /usr/X11R6/include/X11.h might be in
    /tmp/distcc/usr/X11R6/include/X11.h rather than in
    /usr/X11R6/include.

    As the example above suggests, relative position of .h files is
    preserved in distcc-land, so if the #include ends up being a
    relative include, we do not need to do any munging, so we don't
    bother to store anything in the value-list corresponding to
    realpath_idx.  If, however, the #include ends up being an absolute
    include, we do store the "real" name (as an index-pair) in the
    list.  For debugging purposes, we may store more than one "real"
    name if there are several, which can happen when multiple symlinks
    point to the same place.
    TODO(csilvers): change the code to only store one.

    Here's a concrete example: suppose we're trying to resolve
    #include "bar.h", and the searchdir_list is ["reldir/foo",
    "/usr/foo"].  If "<cwd>/reldir/foo/bar.h" exists, then
    realpath_idx will resolve to that, and the preprocessor will emit
    code like "#line 1 reldir/foo/bar.h".  That's correct as-is, no
    munging needed, so we don't bother to put a value in the
    include_closure entry for this realpath.

    If, however, "<cwd>/reldir/foo/bar.h" does not exist, but
    "/usr/foo/bar.h" exists, then realpath_idx will resolve to that,
    and the preprocessor will emit code like "#line 1
    /tmp/distcc/usr/foo/bar.h".  We'll want to munge that to be
    "/usr/foo/bar.h", so we do put a value in the include_closure
    entry for this realpath, to tell us what to munge to.

    (Note we *could* use realpath to tell us the "real" filename,
    without needing a separate index-pair, but that's not as
    user-friendly, since realpath is the filename after symlinks are
    resolved.  Thus, on some setups the realpath of /usr/foo/bar.h
    could be /netapp1/mnt/foo/bar.h or something equally unhelpful.)

    This method to be overridden by derived class.
    """

    raise Exception("RunAlgorithm not implemented.")

  def ClearStatCaches(self):
    """Clear caches used for, or dependent on, stats."""
    self.generation += 1
    # Tabula rasa: for this analysis, we must forget everything recorded in the
    # client_root directory about source files, directories, and symbolic links.
    # But we cannot delete any such information, because slow-poke distcc
    # clients that have received earlier include manifests perhaps only now get
    # around to reading a previous generation client root directory.
    self.client_root_keeper.ClientRootMakedir(self.generation)
    self._InitializeAllCaches()
