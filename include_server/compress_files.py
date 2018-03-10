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

"""Compress files in an include closure."""

import os
import sys
import os.path

import distcc_pump_c_extensions

class CompressFiles(object):

  def __init__(self, includepath_map, directory_map, realpath_map, mirror_path):
    """Constructor.

    Arguments:
      includepath_map: MapToIndex, holds idx-to-string info for includepaths
      directory_map: DirectoryMapToIndex
      realpath_map: CanonicalMapToIndex
    """
    self.includepath_map = includepath_map
    self.directory_map = directory_map
    self.realpath_map = realpath_map
    self.mirror_path = mirror_path
    # The realpath_map indices of files that have been compressed already.
    self.files_compressed = set([])

  def Compress(self, include_closure, client_root_keeper, currdir_idx):
    """Copy files in include_closure to the client_root directory, compressing
    them as we go, and also inserting #line directives.

    Arguments:
      include_closure: a dictionary, see IncludeAnalyzer.RunAlgorithm
      client_root_keeper: an object as defined in basics.py
    Returns: a list of filepaths under client_root

    Walk through the files in the include closure. Make sure their compressed
    images (with either .lzo or lzo.abs extension) exist under client_root as
    handled by client_root_keeper. Also collect all the .lzo or .lzo.abs
    filepaths in a list, which is the return value.
    """
    realpath_string = self.realpath_map.string
    files = [] # where we accumulate files

    for realpath_idx in include_closure:
      # Thanks to symbolic links, many absolute filepaths may designate
      # the very same canonical path (as calculated by realpath). The
      # first such one to be discovered is the one used.
      realpath = realpath_string[realpath_idx]
      if len(include_closure[realpath_idx]) > 0:
        # Designate by suffix '.abs' that this file is to become known by an
        # absolute filepath through a #line directive.
        new_filepath = "%s%s.lzo.abs" % (client_root_keeper.client_root,
                                         realpath)
      else:
        new_filepath = "%s%s.lzo" % (client_root_keeper.client_root,
                                     realpath)
      files.append(new_filepath)
      if not new_filepath in self.files_compressed:
        self.files_compressed.add(new_filepath)
        dirname = os.path.dirname(new_filepath)
        try:
          if not os.path.isdir(dirname):
            my_root = client_root_keeper.client_root
            self.mirror_path.DoPath(realpath, currdir_idx, my_root)
        except (IOError, OSError) as why:
          # Kill include server
          sys.exit("Could not make directory '%s': %s" % (dirname, why))
        if new_filepath.endswith('.abs'):
          (searchdir_idx, includepath_idx) = include_closure[realpath_idx][0]
          # TODO(csilvers): can't we use + here instead of os.path.join?
          filepath = os.path.join(self.directory_map.string[searchdir_idx],
                                  self.includepath_map.string[includepath_idx])
          # This file is included through say -I/foo, but /foo does not exist
          # on the compiler server. Instead, this file will put under some
          # /serverrootpath/foo there. The #line directive informs the compiler
          # about the real location. This is useful for error messages.
          prefix = ("""#line 1 "%s"\n""" % filepath)
        else:
          # This file will be relatively resolved on the served. No need to
          # change its name.
          prefix = ""
        try:
          real_file_fd = open(realpath, "r", encoding='latin-1')
        except (IOError, OSError) as why:
          sys.exit("Could not open '%s' for reading: %s" % (realpath, why))
        try:
          new_filepath_fd = open(new_filepath, "wb")
        except (IOError, OSError) as why:
          sys.exit("Could not open '%s' for writing: %s" % (new_filepath, why))
        try:
          new_filepath_fd.write(
            distcc_pump_c_extensions.CompressLzo1xAlloc(
              prefix + real_file_fd.read()))
        except (IOError, OSError) as why:
          sys.exit("Could not write to '%s': %s" % (new_filepath, why))
        new_filepath_fd.close()
        real_file_fd.close()
    return files
