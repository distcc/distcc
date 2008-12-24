#! /usr/bin/python

# benchmark -- automated system for testing distcc correctness
# and performance on various source trees.

# Copyright (C) 2002, 2003, 2004 by Martin Pool
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


# Unlike the main distcc test suite, this program *does* require you
# to manually set up servers on your choice of machines on the
# network, and make sure that they all have appropriate compilers
# installed.  The performance measurements obviously depend on the
# network and hardware available.

# It also depends on you having the necessary dependencies to build
# the software.  If you regularly build software on Linux you should
# be OK.  Some things (the GIMP) will be harder than others.

# On some platforms, it may be impossible to build some targets -- for
# example, Linux depends on having a real-mode x86 assembler, which
# probably isn't installed on Solaris.

# Note that running this program will potentially download many
# megabytes of test data.


# TODO: Support applying patches after unpacking, before building.
# For example, they might be needed to fix -j bugs in the Makefile.

# TODO: In stats, show ratio of build time to slowest build time.  (Or
# to first one run?)

# TODO: Allow choice of which compiler and make options to use.

# TODO: Perhaps add option to do "make clean" -- this might be faster
# than unzipping and configuring every time.  But perhaps also less
# reproducible.

# TODO: Add option to run tests on different sets or orderings of
# machines.

import os
import re
import sys
import time

from getopt import getopt

from Summary import Summary
from Project import Project, trees
from compiler import CompilerSpec
from Build import Build
import actions, compiler

import ProjectDefs         # this adds a lot of definitions to 'trees'


def error(msg):
    sys.stderr.write(msg + "\n")


def list_projects():
    names = trees.keys()
    names.sort()
    for n in names:
        print n

        
def find_project(name):
    """
    Return the nearest unique match for name.
    """
    best_match = None
    for pn in trees.keys():
        if pn.startswith(name):
            if best_match:
                raise ValueError, "ambiguous prefix %s" % name
            else:
                best_match = pn
                
    if not best_match:
        raise ValueError, "nothing matches %s" % name
    else:
        return trees[best_match]



def show_help():
    print """Usage: benchmark.py [OPTION]... [PROJECT]...
Test distcc relative performance building different projects.
By default, all known projects are built.

Options:
  --help                     show brief help message
  --list-projects            show defined projects
  --cc=PATH                  specify base value of CC to pass to configure
  --cxx=PATH                 specify base value of CXX to pass to configure
  --output=FILE              print final summary to FILE in addition to stdout
  -c, --compiler=COMPILER    specify one compiler to use; format is
                             {local|dist|lzo|pump},h<NUMHOSTS>,j<NUMJOBS>
  -n N                       repeat compilation N times
  -a, --actions=ACTIONS      comma-separated list of action phases
                             to perform
  -f N, --force=N            If set to 0, skip download, unpack, and
                             configure actions if they've already been
                             successfully performed; if set to 1 (the
                             default), only skip the download action;
                             if set to 2, do not skip any action

The C and C++ compiler versions used can be set with the --cc and --cxx
options.

Use of distcc features is set with the -c/--compiler option.  The argument
to -c/--compiler has three components, separated by commas.  The first
component specifies which distcc features to enabled: "local" means
run cc locally, "distcc" means use plain distcc, "lzo" means enabled
compression, and "pump" means to enable pump mode and compression.
The second component specifies how many distcc hosts to use.  The third
component specifies how many jobs to run (the -j/--jobs option to "make").
Multiple -c/--compiler options specify different scenarios to measure.
The default is to measure a few reasonable scenarios.

"""
    actions.action_help()


# -a is for developer use only and not documented; unless you're
# careful the results will just be confusing.

            


######################################################################
def main():
    """Run the benchmark per arguments"""

    # Ensure that stdout and stderr are line buffered, rather than
    # block buffered, as might be the default when running with
    # stdout/stderr redirected to a file; this ensures that the
    # output is prompt, even when the script takes a long time for
    # a single step, and it also avoids confusing intermingling of
    # stdout and stderr.
    sys.stdout = os.fdopen(1, "w", 1)
    sys.stderr = os.fdopen(2, "w", 1)

    sum = Summary()
    options, args = getopt(sys.argv[1:], 'a:c:n:f:',
                           ['list-projects', 'actions=', 'help', 'compiler=',
                            'cc=', 'cxx=', 'output=', 'force='])
    opt_actions = actions.default_actions
    opt_cc = 'cc'
    opt_cxx = 'c++'
    opt_output = None
    opt_compilers = []
    opt_repeats = 1
    opt_force = 1

    for opt, optarg in options:
        if opt == '--help':
            show_help()
            return
        elif opt == '--list-projects':
            list_projects()
            return
        elif opt == '--actions' or opt == '-a':
            opt_actions = actions.parse_opt_actions(optarg)
        elif opt == '--cc':
            opt_cc = optarg
        elif opt == '--cxx':
            opt_cxx = optarg
        elif opt == '--output':
            opt_output = optarg
        elif opt == '--compiler' or opt == '-c':
            opt_compilers.append(optarg)
        elif opt == '-n':
            opt_repeats = int(optarg)
        elif opt == '-f' or opt == '--force':
            opt_force = int(optarg)

    if opt_compilers:
        set_compilers = [compiler.parse_compiler_opt(c, cc=opt_cc, cxx=opt_cxx)
                         for c in opt_compilers]
    else:
        set_compilers = compiler.default_compilers(cc=opt_cc, cxx=opt_cxx)

    # Find named projects, or run all by default
    if args:
        chosen_projects = [find_project(name) for name in args]
    else:
        chosen_projects = trees.values()

    for proj in chosen_projects:
        # Ignore actions we did in a previous benchmark run, absent -f.
        # We only run the project's pre-actions if one of the builds
        # needs it because it hasn't successfully run 'configure' yet.
        project_actions, _ = actions.remove_unnecessary_actions(
                opt_actions, opt_force, proj.did_download(), 0)
        proj.pre_actions(project_actions)

        for comp in set_compilers:
            build = Build(proj, comp, opt_repeats)
            _, build_actions = actions.remove_unnecessary_actions(
                opt_actions, opt_force,
                proj.did_download(), build.did_configure())

            build.build_actions(build_actions, sum)

    sum.print_table()
    # If --output was specified, print the table to the output-file too
    if opt_output:
        old_stdout = sys.stdout
        sys.stdout = open(opt_output, 'w')
        try:
            sum.print_table()
        finally:
            sys.stdout.close()
            sys.stdout = old_stdout

if __name__ == '__main__':
    main()
