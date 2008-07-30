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

def count_hosts(hosts):
    """Parse a distcc Hosts Specification and count the number of hosts."""
    num_hosts = 0
    for host in hosts.split():
      if host == '+zeroconf':
        raise ValueError, "Can't count hosts when +zeroconf is in DISTCC_HOSTS"
      if host.startswith('-'):
        # Don't count options such as '--randomize', '--localslots=N',
        # or '--localslots_cpp=N' as hosts.
        continue
      num_hosts += 1
    return num_hosts

def tweak_hosts(hosts, max_hosts, opts):
    """
    Parse a distcc Hosts Specification and construct a new one
    that has at most 'max_hosts' hosts in it, appending 'opts'
    to each host in the Hosts Specification.

    Arguments:
      hosts: the original hosts specification; a string.
      max_hosts: the number of hosts to allow; an integer.
    Returns:
      the new hosts specification; a string.
    """
    num_hosts = 0
    hosts_list = []
    for host in hosts.split():
      if host == '+zeroconf':
        raise ValueError, "Can't limit hosts when +zeroconf in DISTCC_HOSTS"
      if host.startswith('-'):
        # Don't count options such as '--randomize', '--localslots=N',
        # or '--localslots_cpp=N' as hosts; but keep them in the host list.
        hosts_list.append(host)
        continue
      else:
        hosts_list.append(host + opts)
      num_hosts += 1
      if num_hosts >= max_hosts:
        break
    return ' '.join(hosts_list)
