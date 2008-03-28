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

class Summary:
    """Stores and prints results of building different things"""

    # Table is a sequence, because we prefer to have things printed
    # out in the order they were executed.

    def __init__(self):
        self._table = []

    def store(self, project, compiler, elapsed_times):
        """
        elapsed_times is a sequence of elapsed times to build the project.
        A sequence because we can build projects repeatedly.
        """
        self._table.append((project.name, compiler.name, elapsed_times))

    def print_raw(self):
        from pprint import pprint
        pprint(self._table)

    def print_table(self):
        import time, os, sys
        import statistics

        # if nothing was run, skip it
        if not len(self._table):
            return        

        """Print out in a nice tabular form"""
        print """
                       ========================
                       distcc benchmark results
                       ========================

"""
        print "Date: ", time.ctime()
        print "DISTCC_HOSTS: %s" % `os.getenv('DISTCC_HOSTS')`
        sys.stdout.flush()
        os.system("uname -a")

        print "%-20s  %-30s   %8s  %8s" % ('project', 'compiler', 'time', 's.d.')

        for row in self._table:
            print "%-20s  %-30s " % row[:2],
            times = row[2]
            if times == 'FAIL':
                print '%9s' % 'FAIL'
            else:
                mean = statistics.mean(times)
                sd = statistics.std(times)
                print "%8.4fs" % mean,
                if sd is None:
                    print "%9s" % "n/a"
                else:
                    print "%8.4fs" % sd

