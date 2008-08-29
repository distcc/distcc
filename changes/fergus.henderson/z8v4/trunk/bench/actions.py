# benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003 by Martin Pool
# Copyright 2008 Google Inc.
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

# Tuples of (name, default, descr)
all_actions = [('download', True, ''),
               ('md5check', True, 'check file was downloaded correctly'),
               ('sweep', True, 'remove build directory before unpacking'),
               ('unpack', True, 'unpack source'),
               ('configure', True, ''),
               ('build', True, ''),
               ('clean', True, 'run "make clean" or equivalent'),
               ('scrub', False, 'remove build directory')]

# Actions done on a per-project (rather than a per-build) basis
project_actions = ('download', 'md5check')



def action_help():
    print "Actions:"
    for action, default, descr in all_actions:
        default_ch = default and '*' or ' '
        print " %c  %-20s %s" % (default_ch, action, descr)
    print " (* = on by default)"


# Filter out only actions where 'default' is true
default_actions = [a[0] for a in all_actions if a[1]]


def parse_opt_actions(optarg):
    opt_actions = optarg.split(',')
    action_names = [a[0] for a in all_actions]
    for oa in opt_actions:
        if oa not in action_names:
            raise ValueError, ("no such action: %s" % `oa`)
    return opt_actions


def remove_unnecessary_actions(opt_actions, force, did_download, did_configure):
    """Given a list of actions (as a string), and a force value
    (as described in the help text for benchmark.py), and a
    bool indicating whether 'configure' was successfully run
    for this build or not, return a new list which is the actions
    to actually perform for this build.

    Returns two lists: one that can be done on a per-project basis,
    and one that has to be done on a per-build basis (as we build the
    project with various different flags).
    """

    if force == 0 and did_configure and did_download:
        remove = ('download', 'md5check', 'sweep', 'unpack', 'configure')
    elif force <= 1 and did_download:
        remove = ('download', )
    else:
        remove = ()

    new_project_actions = [oa for oa in opt_actions
                           if oa in project_actions and oa not in remove]
    new_build_actions = [oa for oa in opt_actions
                         if oa not in project_actions and oa not in remove]
    return new_project_actions, new_build_actions
