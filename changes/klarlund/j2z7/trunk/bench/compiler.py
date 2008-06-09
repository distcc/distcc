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

import commands
import os
import shutil
import stat
import sys
import tempfile

import buildutil

STANDARD_CC_NAMES = ['cc', 'gcc']
STANDARD_CXX_NAMES = ['cxx', 'c++', 'g++' ]

def _find_executable(name):
    (rs, output) = commands.getstatusoutput('which "%s"' % name)
    if rs:
        sys.exit("Could not determine location of '%s'" % name)
    return output.strip()

class CompilerSpec:
    """Describes a compiler/make setup.

    Used to define different situations such as local compilation, and
    various degrees of parallelism."""

    def __init__(self, where, cc, cxx, prefix='', make_opts='',
                 pump_cmd='', num_hosts=1, host_opts='',
                 name=None):
        """Constructor:

        Args:
          where: 'local', 'dist', 'lzo', or 'pump'
          cc: location of the C compiler
          cxx: location of the C++
          prefix: a string, either 'distcc ' or ''
          make_opts: options to make, such as '-j120'
          host_opts: for appending to hosts in DISTCC_HOSTS
                     such as ',lzo,cpp'
          name: a string
        """
        self.where = where
        self.real_cc = _find_executable(cc)
        self.real_cxx = _find_executable(cxx)
        self.cc = prefix + self.real_cc
        self.cxx = prefix + self.real_cxx
        self.make_opts = make_opts
        self.host_opts = host_opts
        self.pump_cmd = pump_cmd
        self.num_hosts = num_hosts
        self.host_opts = host_opts
        self.name = name or (self.pump_cmd + self.real_cc + "__" +
                             self.make_opts).replace(' ', '_')

    def prepare_shell_script_farm(self, farm_dir, masquerade):
        """Prepare farm directory for masquerading.

        Assume the compiler is not local. Each standard name, such as
        'cc', is used for form a shell script, named 'cc', that
        contains the line 'distcc /my/path/gcc "$@"', where
        '/my/path/gcc' is the value of the compiler.gcc field.

        If the compiler is local, then the same procedure is followed
        except that 'distcc' is omitted from the command line.
        """
        assert os.path.isdir(farm_dir)
        assert os.path.isabs(farm_dir)

        def make_shell_script(name, compiler_path, where):
            fd = open(os.path.join(farm_dir, name), 'w')
            fd.write('#!/bin/sh\n%s%s "$@"'
                     % (where != 'local' and 'distcc ' or '',
                        compiler_path))
            fd.close()
            os.chmod(os.path.join(farm_dir, name),
                     stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR)

        for generic_name in STANDARD_CC_NAMES:
            make_shell_script(generic_name, self.real_cc, self.where)

        for generic_name in STANDARD_CXX_NAMES:
            make_shell_script(generic_name, self.real_cxx, self.where)

        # Make shell wrapper to help manual debugging.
        fd = open(masquerade, 'w')
        fd.write("""\
#!/bin/sh
# Execute $@, but force 'cc' and 'cxx'" to be those in the farm of
# masquerading scripts.  Each script in turn executes 'distcc' with the actual
# compiler specified with the benchmark.py command.
PATH=%s:"$PATH" "$@"
""" % farm_dir)
        fd.close()
        os.chmod(masquerade,
                 stat.S_IXUSR | stat.S_IRUSR | stat.S_IWUSR)


def default_compilers(cc, cxx):
    return [parse_compiler_opt('local,h1,j1', cc, cxx),
            parse_compiler_opt('dist,h10,j20', cc, cxx),
            parse_compiler_opt('dist,h10,j40', cc, cxx),
            parse_compiler_opt('pump,h10,j20', cc, cxx),
            parse_compiler_opt('pump,h10,j40', cc, cxx),
            ]

def parse_compiler_opt(optarg, cc, cxx):
    """Parse command-line specification of a compiler (-c/--compiler).

    XXX: I don't really know what the best syntax for this is.  For
    the moment, it is "local", "dist", "lzo", or "pump", followed by ",h"
    and the number of hosts to use, followed by ",j" and the number
    of jobs to use (for the -j option to make).
    """
    where, hosts, jobs = optarg.split(',')
    if hosts.startswith("h"):
      hosts = int(hosts[1:])
      if not os.getenv("DISTCC_HOSTS"):
        raise ValueError, "You must set DISTCC_HOSTS before running benchmarks"
      max_hosts = buildutil.count_hosts(os.getenv("DISTCC_HOSTS"))
      if hosts > max_hosts:
        print ("Warning: can't use %d hosts: DISTCC_HOSTS only has %d" %
               (hosts, max_hosts))
        hosts = max_hosts
    else:
      raise ValueError, ("invalid compiler option: "
                         "expecting '...,h<NUMBER OF HOSTS>,...', found %s"
                         % `hosts`)
    if jobs.startswith("j"):
      jobs = int(jobs[1:])
    else:
      raise ValueError, ("invalid compiler option: "
                         "expecting '...,j<NUMBER OF JOBS>', found %s"
                         % `jobs`)
    if where == 'local':
        return CompilerSpec(where=where,
                            name='local_%02d' % jobs,
                            cc=cc,
                            cxx=cxx,
                            num_hosts=1,
                            make_opts='-j%d' % jobs)
    elif where == 'dist':
        return CompilerSpec(where=where,
                            name='dist_h%02d_j%02d' % (hosts, jobs),
                            cc=cc,
                            cxx=cxx,
                            prefix='distcc ',
                            num_hosts=hosts,
                            make_opts='-j%d' % jobs)
    elif where == 'lzo':
        return CompilerSpec(where=where,
                            name='lzo_h%02d_j%02d' % (hosts, jobs),
                            cc=cc,
                            cxx=cxx,
                            prefix='distcc ',
                            num_hosts=hosts,
                            host_opts=",lzo",
                            make_opts='-j%d' % jobs)
    elif where == 'pump':
        return CompilerSpec(where=where,
                            name='pump_h%02d_j%02d' % (hosts, jobs),
                            cc=cc,
                            cxx=cxx,
                            prefix='distcc ',
                            pump_cmd='pump ',
                            num_hosts=hosts,
                            host_opts=",cpp,lzo",
                            make_opts='-j%d' % jobs)
    else:
      raise ValueError, ("invalid compiler option: don't understand %s"
                         % `where`)


def prepare_shell_script_farm(compiler, farm_dir, masquerade):
    compiler.prepare_shell_script_farm(farm_dir, masquerade)
