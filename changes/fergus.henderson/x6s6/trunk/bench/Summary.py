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

import buildutil
import os
import statistics

class Summary:
    """Stores and prints results of building different things"""

    # Table is a sequence, because we prefer to have things printed
    # out in the order they were executed.

    def __init__(self):
        self._table = []

    def store(self, project, compiler, time_info_accumulator):
        """
        Args:
          project: a Project object
          compiler: a Compiler object
          time_info_accumulator: the string 'FAIL' or a list of Build.TimeInfo records
   
        The time information is a list because we can build projects repeatedly.
        
        """
        self._table.append((project.name, compiler.name, time_info_accumulator))

    def print_raw(self):
        from pprint import pprint
        pprint(self._table)

    @staticmethod
    def print_mean_and_sd(times, unit='s', no_sd=False):
        assert len(unit) == 1, unit
        mean = statistics.mean(times)
        sd = statistics.std(times)
        if mean is None:
            print "%s%s  " % ("n/a", sd_space),
        else:
            print "%8.1f%s " % (mean, unit),
        if not no_sd:
            if sd is None:
                print "%9s " % "n/a",
            else:
                print "%8.1f%s " % (sd, unit),

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
        hosts = os.getenv('DISTCC_HOSTS')
        print "DISTCC_HOSTS: %s" % `hosts`
        print "Total hosts: %d" % buildutil.count_hosts(hosts)
        number_CPUs = os.sysconf('SC_NPROCESSORS_ONLN')
        print "Local number of CPUs: %s" % number_CPUs
        sys.stdout.flush()
        os.system("uname -a")

        print ("%-20s  %-30s  %9s  %9s  %9s  %9s  %9s" 
        % ('project', 'compiler', 'time', 's.d.', 
           'CPU time',
           'CPU util', 
           'incl serv'))

        for row in self._table:
            print "%-20s  %-30s " % row[:2],
            time_info_accumulator = row[2]
            if isinstance(time_info_accumulator, str):
                print ' ' * 4, time_info_accumulator
            else:
                real_times = [time_info.real for time_info in time_info_accumulator]
                Summary.print_mean_and_sd(real_times)

                cpu_times = [time_info.user + time_info.system
                             for time_info in time_info_accumulator]
                self.print_mean_and_sd(cpu_times, no_sd=True)

                cpu_util_ratios = (
                    [100 * cpu_times[i]/(number_CPUs * time_info_accumulator[i].real)
                     for i in range(len(time_info_accumulator))])
                self.print_mean_and_sd(cpu_util_ratios, unit='%', no_sd=True)
                include_server_times = [time_info.include_server
                                        for time_info in time_info_accumulator]

                if None not in include_server_times:
                    self.print_mean_and_sd(include_server_times, no_sd=True)

                print
