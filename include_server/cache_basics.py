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
#

"""Classes enabling definition and composition of caches.

This file defines caches used to speed up the does-this-file-exist
test that forms the basis of the C preprocessor's include-file
handling, and takes most of its time.

When the preprocessor sees a line like "#include <foo/bar.h>" it looks
for a file named "bar.h" in many directories: /usr/include/foo/bar.h,
./foo/bar.h, and so forth.  More precisely, the preprocessor is given
a "search path", which is a list of directory-names.  (By default, the
search-path looks like ['/usr/include', '/usr/local/include', ...],
but it's often extended via gcc flags like -I, -isystem, -iprefix,
etc.)  To resolve a single #include like "#include <foo/bar.h>", the
preprocessor goes through every directory in the search path, running

   os.stat(os.path.join(current_working_dir, search_dir, 'foo/bar.h'))

until the stat call succeeds.  With dozens of search-dirs to look
through, dozens of #include lines per source file, and hundreds of
source files per compilation, this can add up to millions of stat
calls.  Many of these calls are exactly the same, so caching is a big
win.

The cache of stat calls takes a filename as input and produces a bool
as output, saying if the filename exists.  For reasons that will
become clear in a moment, we actually represent the input filename as
a triple that breaks the filename into its three components:

   1) currdir: the current working directory (usually os.path.absdir('.'))
   2) searchdir: an element of the search path (eg '/usr/include', 'base')
   3) includepath: the thing that comes after "#include" in source files
                   ("foo/bar.h" in our examples above).

Why do we break the input into three parts?  Consider what cache-lookups
we have to do for a single source file:
   cache[os.path.join(currdir, searchdir1, includepath1)]   # #include <ipath1>
   cache[os.path.join(currdir, searchdir2, includepath1)]   # #include <ipath1>
   cache[os.path.join(currdir, searchdir3, includepath1)]   # #include <ipath1>
   [etc...until the cache-lookup returns True]
   cache[os.path.join(currdir, searchdir1, includepath2)]   # #include <ipath2>
   cache[os.path.join(currdir, searchdir2, includepath2)]   # #include <ipath2>
   cache[os.path.join(currdir, searchdir3, includepath2)]   # #include <ipath2>
   [etc]

By having the key be a triple, we avoid all those unnecessary
os.path.join calls.  But even if we do this, we notice bigger fish
to fry: the Python interpreter still has to do a string-hash of
currdir for every lookup, and also has to string-hash searchdirX and
includepathX many times.  It would be much more efficient if we did
those hashes ourselves, reducing the number of string-hashes from
O(|search-path| * |#include lines|) to
O(|search-path| + |#include lines|).

This motivates (finally!) the data structures in this file.  We have
three string-to-number maps, for mapping each currdir, searchdir, and
includepath to a small integer.  We put that all together in a cache,
that takes a triple of integers as its key and produces True if the
file exists, False if it does not, or None if its status is unknown.

The String-to-number Map(s)
---------------------------
The basic map that converts a filepath-path -- a currdir, searchdir,
or includepath -- to a small integer is called MapToIndex.  MapToIndex
provides mapping in both directions:
   index: a dictionary mapping paths (strings) to indices in 1..N, and
   string: an array of size N + 1 that implements the reverse mapping

So:
   obj.string[obj.index[path_as_string]] == path_as_string
   obj.index[obj.string[path_as_number]] == path_as_number

Note we map from 1..N, and not 0..N-1, which leave us 0 free to use as
a synonym for None or False.

There are also classes that specialize MapToIndex for specific purposes.

DirectoryMapToIndex assumes the input is a directory, and in
particular a directory that does not have a slash at the end of it (eg
"/etc").  It adds the trailing slash before inserting into the map.
This is useful because it allows us to use + to join this directory
with a relative filename, rather than the slower os.path.join().

RelpathMapToIndex assumes the input is a relative filepath, that is,
one that does not start with /.  When combined with DirectoryMapToIndex
entries, + can be used as a fast alternative to os.path.join().

CanonicalMapToIndex is a MapToIndex that canonializes its input before
inserting it into the map: resolving symlinks, getting rid of ..'s,
etc.  It takes an absolute path as input.

Other Caches
------------
Besides the maps from strings to integers, there are three other caches.
One is the realpath-cache, that takes a filename and returns
os.path.realpath(filename).  We cache this because os.path.realpath()
is very slow.  This is called CanonicalPath.

The second cache, the DirnameCache, maps an arbitrary pathname to
dirname(pathname), that is, the directory the pathname is in.  The
input pathname is represented by a (currdir_idx, searchdir_idx,
includepath_idx) triple.  The output is likewise represented as a
number: an index into the DirectoryMapToIndex structure.

The third cache is called SystemdirPrefixCache.  It tells you, for a
given absolute filepath, whether it is prefixed by a systemdir (that
is, one of the searchdirs that's built into cpp, such as /usr/include).
This is useful to cache because there are several systemdirs, and it's
expensive to check them all each time.

Naming Conventions
------------------
  currdir: the current working dir.
  searchdir: an element of the search-path (places cpp looks for .h files).
  includepath: the string a source file #includes.
  realpath: a full filepath with all its symlinks resolved:
     os.path.realpath(os.path.join(currdir, searchdir, includepath))
  FOO_idx: the small integer associated with the string FOO.

  includepath_map: the map that takes includepaths to their idx and back
     (a RelpathMapToIndex).
  directory_map: the map that takes currdirs and searchdirs to their
     idx and back.  It also is used to store dirname(filepath) for arbitrary
     filepaths -- basically, anything we know is a directory (a
     DirectoryMapToIndex).
  realpath_map: the map that takes full filepaths to their idx and back,
     canonicalizing them first (by resolving symlinks) (a
     CanonicalMapToIndex).

  searchlist: a list of searchdirs.  In gcc/cpp documentation, this is
     called the "search path", but for consistency, in this code we reserve
     the name "path" to mean "filesystem component," never "list of dirs".
     (A list of strings).
  systemdir: a searchdir that's built into cpp, rather than set via -I.
     (A string.)

  resolved_filepath: given an includepath, and a (possibly implicit)
     currdir and searchlist, the resolved_filepath is
        os.path.join(currdir, searchdir, includepath)
     for the first searchdir in searchlist for which the joined string
     exists.  This path can be represented in many ways: 1) a string like
     "foo/bar/baz.h" (if so, this string has been canonicalized to resolve
     symlinks and the like); 2) an index into realpath_map associated with
     that string; 3) a triple of indices; or 4) a pair of indices plus an
     assumption that os.getcwd() == currdir.

Pair Representation of Filepaths
-------------------------------
A file is uniquely determined by the triple
   (currdir_idx, searchdir_idx, includepath_idx)
For a single compilation unit, the code will often start with a
chdir(currdir).  After that, we often refer to a file by the pair
   (searchdir_idx, includepath_idx)
which might be either an absolute filename or relative to $PWD.

We refer to this pair as a filepath_pair.
TODO(csilvers): find a better name?

The function IsFilepathPair(x) tests whether x is a pair that could
plausibly have a searchdir_idx as its first element and an
includepath_idx as its second.

Tests
-----
This code is currently only tested by regression tests of modules
using this one.
"""

__author__ = "opensource@google.com (Nils Klarlund, Craig Silverstein)"

import os
import os.path
import sys

import basics
import statistics
import compiler_defaults

DIR_ARRAY_SIZE = 500

# We currently use the stat and realpath of GNU libc stat and
# realpath. They are about an order of magnitude faster than their
# Python counterparts, even when called through the Python/C
# interface.

try:
  import distcc_pump_c_extensions
  _OsPathExists = distcc_pump_c_extensions.OsPathExists
  _OsPathIsFile = distcc_pump_c_extensions.OsPathIsFile
  _PathRealpath = distcc_pump_c_extensions.Realpath
  _path_realpath_works = True
except ImportError:
  _OsPathExists = os.path.exists
  _OsPathIsFile = os.path.isfile
  _PathRealpath = os.path.realpath
  # os.path.realpath might have some bugs.  TODO(csilvers): check that here
  _path_realpath_works = False

Debug = basics.Debug
DEBUG_TRACE = basics.DEBUG_TRACE
DEBUG_TRACE1 = basics.DEBUG_TRACE1
DEBUG_TRACE2 = basics.DEBUG_TRACE2
DEBUG_WARNING = basics.DEBUG_WARNING
NotCoveredError = basics.NotCoveredError


####
#### SIMPLE CACHES
####

class CanonicalPath(object):
  """Memoizing calculation of realpaths.  realpath(x) is the 'canonical'
  version of x, with all symbolic links eliminated.
  """

  def __init__(self):
    self.cache = {}

  def Canonicalize(self, filepath):
    """Find a really canonical path, possibly memoized.

    Arguments:
      filepath: a filepath (string)
    Returns:
      the realpath of filepath (string)

    The following is irrelevant if we always use the distcc_pump_c_extensions
    realpath function.
    ---
    Apparently, in some versions of Python 2.4 at least, realpath does
    *not* resolve the last component of a filepath if it is a link:
       https://sourceforge.net/tracker/?func=detail&atid=105470&aid=1213894&group_id=5470
    Make up for that: follow that final link until a real realpath has
    been found.

    Also, realpath is not idempotent.

    Solution (?): turn filepath into abspath before applying realpath;
    then we can cache results as well (without worring about value of
    current directory).

    The final problem -- that os.path.realpath is very slow, at least
    an order of magnitude slower than the gnu libc one --- is solved
    through caching all uses through an object of the present class.
    """
    assert isinstance(filepath, str)
    try:
      return self.cache[filepath]
    except KeyError:
      if _path_realpath_works:
        r = _PathRealpath(filepath)
        self.cache[filepath] = r
        return r
      # Fix for os.path.realpath idempotencey bug (Python 2.4).
      filepath_ = os.path.abspath(filepath)
      filepath_ = _PathRealpath(filepath_)
      # Fix for os.path.realpath bug (Python 2.4): symlinks at end not
      # resolved.
      for unused_i in range(10):
        if not os.path.islink(filepath_):
          break
        filepath_ = os.path.join(os.path.dirname(filepath_),
                                 os.readlink(filepath_))
      else:
        raise NotCoveredError("Too many symlinks in '%s'." % filepath)
      self.cache[filepath] = filepath_
      return filepath_

class DirnameCache(object):
  """Cache the mapping from filepath pairs to index of their directory names.

  The key is a triple (currdir_idx, searchdir_idx, includepath_idx).  The
  value is

    (dir_idx, dir_realpath_idx)

  where dir_idx is the index of dirname of the corresponding filepath, which
  possibly is relative, and dir_realpath_idx is the realpath index of the
  absolute location of the dirname.  The value currdir_idx is of possible
  importance for deteterming dir_realpath_idx, but plays no role in determining
  dir_idx."""

  def __init__(self, includepath_map, directory_map, realpath_map):
    """Constructor.
      Arguments:
        includepath_map: the map used to construct the includepath_idx
           that will be passed in as arguments to Lookup().
        directory_map: the map used to construct both the currdir_idx
           and searchdir_idx that will be passed in as arguments to
           Lookup().  It's also the data structure that produces dir_idx.
        realpath_map: a string-to-int map of canonicalized filepaths
    """
    self.includepath_map = includepath_map
    self.directory_map = directory_map
    self.realpath_map = realpath_map
    self.cache = {}

  def Lookup(self, currdir_idx, searchdir_idx, includepath_idx):
    """Return the directory and realpath indices of the dirname of the input.

    Arguments:
      currdir_idx: the directory index of the current directory
      searchdir_idx: a directory_map index
      includepath_idx: an includepath index
    Returns:
      a pair (directory map index, realpath index)

    See class documentation.

    Example: if the strings of the arguments indices put together make
    '/usr/include/foo/bar.h', then this routine will insert '/usr/include/foo/'
    into self.directory_map, and then return the corresponding pair (directory
    index of /usr/include/foo/, real path index of /usr/include/foo/).  If the
    arguments put together form "foo.h", then the directory index returned is
    that of "", the current directory, and the realpath index is that of
    currdir.
    """
    try:
      return self.cache[(currdir_idx, searchdir_idx, includepath_idx)]
    except KeyError:
      directory = os.path.dirname(os.path.join(
         self.directory_map.string[searchdir_idx],
         self.includepath_map.string[includepath_idx]))
      dir_idx = self.directory_map.Index(directory)
      rp_idx = self.realpath_map.Index(
          os.path.join(self.directory_map.string[currdir_idx],
                       directory))
      self.cache[(currdir_idx, searchdir_idx, includepath_idx)] = (dir_idx,
                                                                   rp_idx)
      return (dir_idx, rp_idx)


class SystemdirPrefixCache(object):
  """A cache of information about whether a file exists in a systemdir.

  A systemdir is a searchdir that is built in to the C/C++
  preprocessor.  That is, when the preprocessor is figuring out what
  directory an #include is in, these are the directories it's
  hard-coded in to check (you can add other directories via -I).  This
  cache records, for a given filepath, whether it starts with a
  systemdir.  This is useful to identify whether the path is likely to
  correspond to a system include-file (such as stdio.h).  Such files are
  unlikely to change, and are likely to already exist on the distcc
  servers, both of which are useful things to know for optimization.

  For speed, users can access self.cache directly, rather than going
  through the StartsWithSystemdir API.  Be sure to call FillCache() to
  make sure the cache is populated, before accessing it!
  """

  def __init__(self, systemdirs):
    """Constructor.

    Argument:
      systemdirs: the list of system-directories the preprocessor
        uses.  It's a list of strings, probably extracted from the
        preprocessor itself.  Each systemdir should end in a slash.

    In practice, systemdirs will start empty, and later some routine
    (in parse_command.py) will magically fill it.  So be sure to wait
    for that before calling FillCache!
    TODO(csilvers): normalize this; ideally pass systemdirs in to FillCache.
    """
    self.systemdirs = systemdirs
    # self.cache[i] will be True, False, or None for not-yet-checked.
    self.cache = [None]

  def FillCache(self, realpath_map):
    """Ensures that there's a cache entry for every index in realpath_map.

    Argument:
     realpath_map: a string-to-int map of canonicalized filepaths we know.

    After this function is called, the cache entry is True iff
    realpath.startswith(systemdir) is True for any of the systemdirs
    passed in to our constructor.
    """
    if len(self.cache) >= realpath_map.Length():
      return    # we're already all full
    for realpath_idx in range(len(self.cache), realpath_map.Length()):
      realpath = realpath_map.string[realpath_idx]
      for systemdir in self.systemdirs:
        if realpath.startswith(systemdir):
          self.cache.append(True)
          break
      else:    # we get here if the for never 'break'ed
        self.cache.append(False)

    assert len(self.cache) == realpath_map.Length()

  def StartsWithSystemdir(self, realpath_idx, realpath_map):
    """Return True iff realpath starts with a systemdir.

    Arguments:
     realpath_idx: the index of the realpath we want to check.
     realpath_map: the map from realpath_idx to a string.

    Return True iff realpath.startswith(systemdir) for any of the
    systemdirs passed in to our constructor.  (For speed, you can
    access self.cache directly instead of calling this, but make
    sure FillCache() has been called first!)
    """
    self.FillCache(realpath_map)
    return self.cache[realpath_idx]


####
#### MAP_TO_INDEX AND ITS SPECIALIZATIONS
####

class MapToIndex(object):
  """Maps every object it sees to a unique small integer.  In
  practice, this class is used to map path-components (which are strings).
  """

  def __init__(self):
    """Constructor.

    Instance variables:
      map: a dictionary such that map[path] is the index of path
      string: a list satisfying: string[i] is the path such that map[path] = i
    """

    # Do not make the mistake of letting a real index be 0. (Hint:
    # because "if path:" then does not distinguish between 0 and None.)
    self.index = {None:None}
    self.string = [None]

  def _Invariant_(self):
    return len(self.index) == len(self.string)

  def Index(self, path):
    """Returns the index i > 0 of path."""
    assert self._Invariant_()
    try:
      return self.index[path]
    except KeyError:
      self.index[path] = len(self.string)
      self.string.append(path)
      return len(self.string) - 1

  def String(self, i):
    """Returns the path such that Index(path) == i."""
    assert self._Invariant_()
    assert 0 < i < self.Length()
    return self.string[i]

  def Length(self):
    """One more than the number of elements indexed."""
    assert self._Invariant_()
    return len(self.string)


class DirectoryMapToIndex(MapToIndex):
  """Like a normal MapToIndex, but assumes the keys are directories,
  and in particular, directories without a trailing slash (eg "/etc").
  It stores the directories in the map, but appends the trailing slash
  first.  This is another type of normalization, and useful for cheap
  path-joining (eg using + instead of os.path.join).
  """

  def Index(self, directory):
    """Return index d > 0 of normalized directory.
    Argument:
      directory: a string, either empty or not ending in '/'.

    The empty string is not changed, but other strings are stored with
    a '/' appended.
    """
    if directory != "" and directory != "/":
      assert directory[-1] != '/', directory
      directory = directory + '/'
    return MapToIndex.Index(self, directory)


class RelpathMapToIndex(MapToIndex):
  """Like a normal MapToIndex, but assumes the keys are relative
  filesystem paths, that is, filesystem paths not starting with /.
  This is useful for "cheap" normalization: this invariant ensures that
  os.path.join(some-directorymap-string, some-relpathmap-string) can
  be implemented using +.

  We actually do allow storing absolute paths if option
  --unsafe_absolute_includes is in use.  But, then, we're careful in Resolve
  (below) to bail out.
  """

  def Index(self, relpath, ignore_absolute_path_warning=False):
    """Return index d > 0 of relative path.
    Args:
      directory: a string not starting with /.
      ignore_absolute_path_warning: a Boolean

    The variable ignore_absolute_path_warning is set to True in order to
    override the requirement that filepaths are relative. This is useful for the
    compilation unit filepath and filepaths of -include's: they are permitted to
    be absolute because the command line can still be rewritten on the server.
    The server tweaks their location to become relative to the server root.
    """
    if os.path.isabs(relpath) and not ignore_absolute_path_warning:
      if basics.opt_unsafe_absolute_includes:
        Debug(DEBUG_WARNING,
              "absolute filepath '%s' was IGNORED"
              " (correctness of build may be affected)", relpath)
      else:
          raise NotCoveredError("Filepath must be relative but isn't: '%s'."
                                " Consider setting INCLUDE_SERVER_ARGS='--"
                                "unsafe_absolute_includes'."
                                % relpath,
                                send_email=False)
    # Now, remove leading "./" so as not to start an infinite regression when
    # say foo.c contains:
    #
    #   #include "./foo.c"
    #
    # which mighy seduce a recursive include analyzer down the forbidden path:
    #
    #   "foo.c", # "./foo.c", "././foo.c." etc.
    while relpath.startswith("./"):
      relpath = relpath[2:]
    return MapToIndex.Index(self, relpath)


class CanonicalMapToIndex(MapToIndex):
  """Like a normal MapToIndex, but assumes the keys are absolute
  filepaths, and canonicalizes them before inserting into the map.
  'Canonicalize' means to do the equivalent of os.path.realpath(),
  which mostly involves resolving symlinks in the filepath.
  """

  def __init__(self, canonicalize):
    """Constructor.
    Argument:
      canonicalize: an instance of the CanonicalPath cache."""
    MapToIndex.__init__(self)
    self.canonicalize = canonicalize

  def Index(self, filepath):
    """Return the realpath index r of filepath.  filepath should be
    an absolute filename.
    """
    return MapToIndex.Index(self, self.canonicalize(filepath))


def RetrieveDirectoriesExceptSys(directory_map, realpath_map,
                                 systemdir_prefix_cache, directory_idxs):
  """Calculate the set of non-system directories of an index list.

  Arguments:
    directory_map: a DirectoryMapToIndex cache
    realpath_map: a CanonicalMapToIndex cache
    directory_idxs: a list or tuple of directory_map indices
  Returns:
    the corresponding tuple of directories except for those whose
    realpath has a prefix that is a sysdir

  The directories in the returned list have their trailing '/'
  stripped.
  """
  result = []
  for dir_idx in directory_idxs:
    # Index the absolute path; this will let us know whether dir_idx is under a
    # default systemdir of the compiler.
    rp_idx = realpath_map.Index(os.path.join(
	os.getcwd(), directory_map.string[dir_idx]))
    systemdir_prefix_cache.FillCache(realpath_map)
    if not systemdir_prefix_cache.cache[rp_idx]:
      result.append(directory_map.string[dir_idx].rstrip('/'))
  return tuple(result)


####
#### THE STAT CACHES
####

class SimpleBuildStat(object):
  """Stat cache that works with strings, not indices."""

  def __init__(self):
    self.cache = {}

  def Lookup(self, filepath):
    """Returns true if filepath exists."""
    try:
      return self.cache[filepath]
    except KeyError:
      result = self.cache[filepath] = _OsPathExists(filepath)
      return result


class BuildStatCache(object):
  """A highly optimized mechanism for stat queries of filepaths,
  as represented by a triple of indexes: currdir_idx, searchdir_idx,
  filepath_idx.  Given this input, we can say whether a regular file
  represented by this triple exists on the filesystem, and if so,
  what its canonical pathname is: that is, the pathname after all
  symlinks have been resolved.

  The hash table is three-level structure:
   - build_stat[currdir_idx] contains an array for each includepath_idx
   - build_stat[currdir_idx][includepath_idx] is this array, and
   - build_stat[currdir_idx][includepath_idx][searchdir_idx] is either
      * False if os.path.join(currdir, searchdir, includepath) does not exist
      * True if it does
      * None when it is not known whether it exists or not
  In addition, we keep a parallel structure for the realpath, that lets us
  quickly map from a filepath to os.path.realpath(filepath).
   - real_stat[currdir_idx] contains an array for each fp
   - real_stat[currdir_idx][includepath_idx] is this array, and
   - real_stat[currdir_idx][includepath_idx][searchdir_idx] is either
      * realpath_idx, such that realpath_map.string[realpath_idx] =
           os.path.realpath(os.path.join(currdir, searchdir, includepath))
        when build_stat[currdir_idx][includepath_idx][searchdir_idx] = True
      * None, otherwise
  """

  def __init__(self, includepath_map, directory_map, realpath_map):
    self.build_stat = {}
    self.real_stat = {}
    self.includepath_map = includepath_map
    self.directory_map = directory_map
    self.realpath_map = realpath_map
    self.path_observations = []

  def _Verify(self, currdir_idx, searchdir_idx, includepath_idx):
    """Verify that the cached result is the same as obtained by stat call.
    Prerequisite: we've done a chdir(currdir) before this call.
    """
    assert 1 <= includepath_idx < self.includepath_map.Length()
    assert 1 <= searchdir_idx < self.directory_map.Length()
    if __debug__: statistics.sys_stat_counter += 1

    # Since we know directory_map entries end in /, and includepaths don't
    # start with / (who does "#include </usr/include/string.h>"??), we can
    # use + instead of the more expensive os.path.join().
    # Make sure $PWD is currdir, so we don't need to include it in our stat().
    assert os.getcwd() + '/' == self.directory_map.string[currdir_idx]
    really_exists = _OsPathIsFile(
      self.directory_map.string[searchdir_idx]
      + self.includepath_map.string[includepath_idx])
    cache_exists = self.build_stat[currdir_idx][includepath_idx][searchdir_idx]
    assert isinstance(cache_exists, bool)
    if cache_exists != really_exists:
      filepath = os.path.join(self.directory_map.string[currdir_idx],
                              self.directory_map.string[searchdir_idx],
                              self.includepath_map.string[includepath_idx])
      sys.exit("FATAL ERROR: "
               "Cache inconsistency: '%s' %s, but earlier this path %s." % (
        filepath,
        really_exists and "exists" or "does not exist",
        cache_exists and "existed" or "did not exist"))

  def WarnAboutPathObservations(self, translation_unit):
    """Print new paths found according to path observation expression option.

    Args:
      translation_unit: a string embedded in warning
    """
    for (includepath, relpath, realpath) in self.path_observations:
      Debug(DEBUG_WARNING,
            "For translation unit '%s',"
            " lookup of file '%s' resolved to '%s' whose realpath is '%s'.",
             translation_unit, includepath, relpath, realpath)
    self.path_observations = []

  def Resolve(self, includepath_idx, currdir_idx, searchdir_idx,
              searchlist_idxs):
    """Says whether (currdir_idx, searchdir_idx, includepath_idx) exists,
    and if so what its canonicalized form is (with symlinks resolved).
    TODO(csilvers): rearrange the order of the arguments.

    Args:
      includepath_idx: The index of an includepath, from e.g. "#include <foo>"
      currdir_idx:     The index of the current working dir.  Note that we
                       require os.getcwd() == currdir before calling Resolve!
      searchdir_idx:   A single searchdir, which is prepended to searchlist,
                       or None to not prepend to the searchlist.
      searchlist_idxs: A list of directory indices.

    Returns:
      1) (None, None) if, for all sl_idx in [searchdir_idx] + searchlist_idxs,
         os.path.join(currdir, sp, includepath) does not exist.
      2) ((sl_idx, includepath_idx), realpath_idx)
         if, for some sl_idx in [searchdir_idx] + searchlist_idxs,
         os.path.join(currdir, sp, includepath) does exist.  In this case,
         sl_idx is the index of the first searchlist entry for which the
         exists-test succeeds, and realpath_idx is the index into the
         realpath_map of os.path.join(currdir, sp, includepath).

    Again, we require as a prequesite that os.getcwd() must equal currdir:
       os.getcwd() + '/' == self.directory_map.string[currdir_idx]
    """
    includepath = self.includepath_map.string[includepath_idx]
    if includepath.startswith('/'):
      # We really don't want to start exploring absolute includepaths; what's
      # the sl_idx to return for example? And what about the use of '+'
      # (as an optimization) below instead of os.path.join.
      return (None, None)
    dir_map_string = self.directory_map.string   # memoize the fn pointer
    build_stat = self.build_stat
    real_stat = self.real_stat
    if __debug__:
      dir_map = self.directory_map
      assert 0 < includepath_idx < self.includepath_map.Length()
      assert 0 < currdir_idx < dir_map.Length()
      assert searchdir_idx is None or 1 <= searchdir_idx < dir_map.Length()
      for sl_idx in searchlist_idxs:
        assert sl_idx < dir_map.Length()
      assert os.getcwd() + '/' == dir_map_string[currdir_idx]
      Debug(DEBUG_TRACE2, "Resolve: includepath: '%s', currdir: '%s', "
            "searchdir: '%s', searchlist: %s" %
            (includepath,
             dir_map_string[currdir_idx],
             searchdir_idx and dir_map_string[searchdir_idx],
             "  \n".join([dir_map_string[idx] for idx in searchlist_idxs])))
    try:
      # Locate the array (list) relative to currdir_idx and includepath_idx
      searchdir_stats = build_stat[currdir_idx][includepath_idx]
      # Locate the corresponding array of realpath names
      searchdir_realpaths = real_stat[currdir_idx][includepath_idx]
    except KeyError:    # We'll need to grow the relevant arrays
      currdir_stats = build_stat.setdefault(currdir_idx, {})
      currdir_realpaths = real_stat.setdefault(currdir_idx, {})
      searchdir_stats = currdir_stats[includepath_idx] = \
                        [None] * DIR_ARRAY_SIZE
      searchdir_realpaths = currdir_realpaths[includepath_idx] = \
                        [None] * DIR_ARRAY_SIZE

    # Try searchdir_idx if not None, then try every index in searchlist_idxs.
    # This inner loop may be executed tens of millions of times.
    # Do not try to form [searchdir_idx] + searchlist_idxs -- too expensive!
    for searchlist in (searchdir_idx and [searchdir_idx] or [],
                       searchlist_idxs):
      for sl_idx in searchlist:
        if __debug__:
          statistics.search_counter += 1
          statistics.build_stat_counter += 1
        try:
          # We expect that searchdir_stats[sl_idx] == False, because
          # we've usually seen sl_idx before for our includepath and
          # our currdir --- and includepath does not usually exist
          # relative to the sp directory.  We're optimizing for this
          # case of course. That should give us a rate of a couple of
          # million iterations per second (for this case).
          if searchdir_stats[sl_idx] == False:
            if __debug__: self._Verify(currdir_idx, sl_idx, includepath_idx)
            continue
          if searchdir_stats[sl_idx]:
            if __debug__: self._Verify(currdir_idx, sl_idx, includepath_idx)
            return ((sl_idx, includepath_idx), searchdir_realpaths[sl_idx])
        except IndexError:   # DIR_ARRAY_SIZE wasn't big enough; let's double
          searchdir_stats.extend([None] * max(sl_idx, len(searchdir_stats)))
          searchdir_realpaths.extend([None] * max(sl_idx, len(searchdir_stats)))

        # If we get here, result is not cached yet.
        if __debug__: statistics.sys_stat_counter += 1
        # We do not explicitly take into account currdir_idx, because
        # of the check above that os.getcwd is set to current_dir.
        relpath = dir_map_string[sl_idx] + includepath
        if _OsPathIsFile(relpath):
          searchdir_stats[sl_idx] = True
          rpath = os.path.join(dir_map_string[currdir_idx], relpath)
          realpath_idx = searchdir_realpaths[sl_idx] = (
            self.realpath_map.Index(rpath))
          # This is the place to catch errant files according to user defined
          # regular expression path_observation_re.
          if basics.opt_path_observation_re:
            realpath = self.realpath_map.string[realpath_idx]
            if basics.opt_path_observation_re.search(realpath):
              self.path_observations.append((includepath, relpath, realpath))
          return ((sl_idx, includepath_idx), realpath_idx)
        else:
          searchdir_stats[sl_idx] = False

    if __debug__: Debug(DEBUG_TRACE2, "Resolve: failed")
    return (None, None)


class SetUpCaches(object):
  """Erect the edifice of caches.

  Instance variables:
    includepath_map: RelpathMapToIndex
    directory_map: DirectoryMapToIndex
    realpath_map: CanonicalMapToIndex

    canonical_path: CanonicalPath
    build_stat_cache: BuildStatCache
    dirname_cache: DirnameCache
    simple_build_stat: SimpleBuildStat

    client_root: a path such as /dev/shm/tmpX.include_server-X-1
                 (used during default system dir determination)

    IsFilepathIndex: test for filepath index
    IsDirectoryIndex: test for director index
    IsRealpathIndex: test for realpath index
    IsFilepathPair: test for filepath pair
  """

  def __init__(self, client_root):

    # A memoizing (caching) class to canonicalize a path: mostly by
    # resolving any symlinks in the path-component.
    self.canonical_path = CanonicalPath()

    # The index-map for includepath names: things seen after '#include'.
    self.includepath_map = RelpathMapToIndex()

    # The index-map for searchdir names and currdir as well.  Also used any
    # other time we have something we know is a directory (eg dirname(foo)).
    self.directory_map = DirectoryMapToIndex()

    # The index-map for realpaths: the full pathname of an include, with
    # symlinks resolved and such (hence the name realpath).
    self.realpath_map = CanonicalMapToIndex(self.canonical_path.Canonicalize)

    # A cache of the directory part of filepaths.  Note it uses the
    # directory_map to actually store the mapping.
    self.dirname_cache = DirnameCache(self.includepath_map, self.directory_map,
                                      self.realpath_map)

    # A cache of whether a realpath starts with a system searchdir or
    # not.  Note: at this time, system_dirs_default_all will be empty.
    # It will get filled via processing in parse_command.py.  This is
    # why we need to store the compiler_defaults instance, to make
    # sure "our" system_dirs_default_all is updated.
    # TODO(csilvers): get rid of this once prefix_cache TODO is cleaned up
    self.compiler_defaults = compiler_defaults.CompilerDefaults(
      self.canonical_path.Canonicalize, client_root)
    self.systemdir_prefix_cache = SystemdirPrefixCache(
      self.compiler_defaults.system_dirs_default_all)

    # The main caches, that say whether a file exists or not.  We have
    # two: a simple one that takes a filepath (string) as an argument,
    # and the complicated one that works with index-triples.
    self.simple_build_stat = SimpleBuildStat()
    self.build_stat_cache = BuildStatCache(self.includepath_map,
                                           self.directory_map,
                                           self.realpath_map)

    # Convenient function closures to test for various semantic datatypes.
    self.IsIncludepathIndex = (lambda x:
                                 isinstance(x, int)
                                 and 0 < x < self.includepath_map.Length())

    self.IsSearchdirIndex = (lambda x:
                               isinstance(x, int)
                               and 0 < x < self.directory_map.Length())

    self.IsCurrdirIndex = (lambda x:
                             isinstance(x, int)
                             and 0 < x < self.directory_map.Length())

    self.IsFilepathPair = (lambda x:
                             isinstance(x, tuple)
                             and len(x) == 2
                             and self.IsSearchdirIndex(x[0])
                             and self.IsIncludepathIndex(x[1]))

    self.IsRealpathIndex = (lambda x:
                              isinstance(x, int)
                              and 0 < x < self.realpath_map.Length())
