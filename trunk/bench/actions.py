# benchmark -- automated system for testing distcc correctness
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

# Tuples of (name, default, descr)
all_actions = [('download', True, ''),
               ('md5check', True, 'check file was downloaded correctly'),
               ('sweep', True, 'remove build directory before unpacking'),
               ('unpack', True, 'unpack source'),
               ('configure', True, ''),
               ('clean', True, 'run "make clean" or equivalent'),
               ('build', True, ''),
               ('scrub', True, 'remove build directory')]


def action_help():
    print "Actions:"
    for action, default, descr in all_actions:
        default_ch = default and '*' or ' '
        print " %c  %-20s %s" % (default_ch, action, descr)
    print " (* = on by default)"


# Filter out only actions where 'default' is true
default_actions = [a[0] for a in all_actions if a[1]]


def parse_opt_actions(optarg):
    import sys
    opt_actions = optarg.split(',')
    action_names = [a[0] for a in all_actions]
    for oa in opt_actions:
        if oa not in action_names:
            raise ValueError, ("no such action: %s" % `oa`)
    return opt_actions
