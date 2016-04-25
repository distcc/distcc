#!/usr/bin/env python3

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

# """Memoizing, piecemeal mirroring of directory and link structure."""

__author__ = "Nils Klarlund"


import os
import os.path

import cache_basics

class MirrorPath(object):
  """Make a caching structure for copying all parts of the paths that
  method DoPath is called with. This includes replication of symbolic
  links.  But the targets of symbolic links are absolutized: they are
  replaced by the realpath of the original target, whether this target
  was relative or absolute.

  Also, remember all directories that had to be followed to find out what paths
  mean.  This is of particular importance to the '..' operator, which may
  involve temporary excursions into directories that otherwise contain no files
  of relevance to the build. But the directories must still be replicated on the
  server for the semantics of '..' to work.  These are called must_exist_dirs.
  """

  def __init__(self,
	       simple_build_stat,
	       canonical_path,
               realpath_map,
               systemdir_prefix_cache):
    """Constructor.

    Arguments:
      simple_build_stat: object of type SimpleBuildStat
      canonical_path: function of type CanonicalPath
      realpath_map: a CanonicalMapToIndex; see cache_basics.py
      systemdir_prefix_cache: a SystemdirPrefixCache; see cache_basics.py.
    """
    assert isinstance(simple_build_stat, cache_basics.SimpleBuildStat)
    assert isinstance(canonical_path, cache_basics.CanonicalPath)
    # All links encountered so far.
    self.links = []
    # We cache tuples (filepath, current_dir_idx) for which we've already fixed
    # up the symbolic links.
    self.link_stat = set([])
    # Usual abbreviations.
    self.simple_build_stat = simple_build_stat
    self.canonical_path = canonical_path
    self.must_exist_dirs = []
    self.realpath_map = realpath_map
    self.systemdir_prefix_cache = systemdir_prefix_cache

  def Links(self):
    """Return the list of symbolic links created."""
    return self.links

  def MustExistDirs(self):
    return self.must_exist_dirs

  def DoPath(self, filepath, current_dir_idx, root):
    """Mirror the parts of filepath not yet created under root.

    Arguments:
      filepath: a string, which is relative or absolute filename
      current_dir_idx: a directory index
      root: a string denoting an absolute path for an existing directory
    """
    assert isinstance(filepath, str)
    assert isinstance(current_dir_idx, int)
    assert isinstance(root, str)
    assert root[0] == '/' and root[-1] != '/'
    assert os.path.isdir(root), root

    link_stat = self.link_stat
    lookup = self.simple_build_stat.Lookup
    # Working from the end (in the hope that a cache lookup will reveal
    # the futility of further work), make sure that intermediate
    # destinations exist, and replicate symbolic links where necessary.
    while filepath and filepath != '/':
      if (filepath, current_dir_idx) in link_stat:
        # Filepath is already mirrored
        return
      link_stat.add((filepath, current_dir_idx))

      # Process suffix of filepath by
      # - making sure that the mirrored real path of the prefix exists,
      # - and that the suffix if a symbolic link
      # is replicated as a symbolic link.
      assert filepath[-1] != '/', filepath

      # Now identify the potential symbolic link at the end of filepath
      (prefix_filepath, suffix) = os.path.split(filepath)
      # Calculate the real position of the destination of the prefix
      prefix_real = self.canonical_path.Canonicalize(prefix_filepath)

      if prefix_real == '/': prefix_real = ''

      # And, its counterpart under root
      root_prefix_real = root + prefix_real

      # Make sure that the parent, root_prefix_real, is there
      if not lookup(root_prefix_real):
        # We have not been in this real location before.
        if not os.path.isdir(root_prefix_real):
          # Now check that the parent of the link is not under a default system
          # dir.  If it is, then we assume that the parent and indeed the
          # link itself exist on the server as well, and thus, don't need to
          # be mirrored.
          realpath_map = self.realpath_map
          realpath_idx = realpath_map.Index(prefix_real)
          if not self.systemdir_prefix_cache.StartsWithSystemdir(realpath_idx,
                                                                 realpath_map):
            # Not under default system dir.  Mark this directory as one that
            # must always be created on the server.
            self.must_exist_dirs.append(root_prefix_real)
            # Create parent path in mirror directory.
            os.makedirs(root_prefix_real)
          else:
            break
        self.simple_build_stat.cache[root_prefix_real] = True
      assert os.path.isdir(root_prefix_real)
      # Create the mirrored symbolic link if applicable.
      if os.path.islink(filepath):
        link_name = root_prefix_real + '/' + suffix
        if not os.path.exists(link_name):
          os.symlink(self.canonical_path.Canonicalize(filepath),
                     link_name)
          self.links.append(link_name)
      filepath = prefix_filepath
