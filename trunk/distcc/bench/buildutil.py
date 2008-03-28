# distcc/benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003 by Martin Pool

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA


def make_dir(d):
    import os
    if not os.path.isdir(d):
        os.makedirs(d)


def run_cmd(cmd, expected=0):
    import time, os
    
    before = time.time()
    print '%% %s' % cmd
    result = os.system(cmd)
    after = time.time()
    elapsed = (after - before)
    print '%16.4fs elapsed\n' % elapsed
    if expected is not None:
        if expected != result:
            raise AssertionError("command failed: expected status %d, got %d",
                                 expected, result)
    return result, elapsed


def rm_files(file_list):
    import os
    for f in file_list:
        if os.path.exists(f):
            os.unlink(f)

