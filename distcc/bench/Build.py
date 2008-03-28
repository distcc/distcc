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

from Project import Project
from compiler import CompilerSpec
import buildutil
from buildutil import make_dir, run_cmd, rm_files
import re, os, sys, time



class Build:
    """A Build is a combination of a Project and CompilerSpec.

    """
    def __init__(self, project, compiler, n_repeats):
        self.project = project
        self.compiler = compiler
        self.n_repeats = n_repeats

        self.base_dir = os.path.join(os.getcwd(), "build", self.project.name, self.compiler.name)
        self.unpacked_dir = os.path.join(self.base_dir, self.project.unpacked_subdir)

        # Some packages need to be started from a subdirectory of their
        # unpacked form.  For example, Samba is compiled from the "source/"
        # subdirectory of the unpacked source.
        if self.project.build_subdir:
            self.build_dir = os.path.join(self.unpacked_dir, project.build_subdir)
        else:
            self.build_dir = self.unpacked_dir

        self.log_dir = self.build_dir

    def __repr__(self):
        return "Build(%s, %s)" % (`self.project`, `self.compiler`)


    def unpack(self):
        """Unpack from source tarball into build directory"""
        if re.search(r"\.tar\.bz2$", self.project.package_file):
            tar_fmt = "tar xf %s --bzip2"
        else:
            tar_fmt = "tar xfz %s"

        tar_cmd = tar_fmt % os.path.join(os.getcwd(), self.project.package_dir,
                                         self.project.package_file)

        make_dir(self.base_dir)
        print "** Unpacking..."
        run_cmd("cd %s && %s" % (self.base_dir, tar_cmd))


    def configure(self, compiler):
        """Run configuration command for this tree, if any."""
        self.compiler = compiler

        make_dir(self.log_dir)

        configure_log = os.path.join(self.log_dir, "bench-configure.log")
        distcc_log = os.path.join(self.log_dir, "bench-configure-distcc.log")

        rm_files((configure_log, distcc_log))

        print "** Configuring..."
        run_cmd("cd %s && \\\nDISTCC_LOG='%s' \\\nCC='%s' \\\nCXX='%s' \\\n%s \\\n>%s 2>&1" %
                (self.build_dir, distcc_log, self.compiler.cc,
                 self.compiler.cxx,
                 self.project.configure_cmd, configure_log))


    def build(self, sum):
        """Actually build the package."""

        build_log = os.path.join(self.log_dir, "bench-build.log")
        prebuild_log = os.path.join(self.log_dir, "bench-prebuild.log")

        distcc_log = os.path.join(self.log_dir, "bench-build-distcc.log")

        rm_files((build_log, distcc_log))

        print "** Building..."
        if self.project.pre_build_cmd:
            cmd = ("cd %s && %s > %s 2>&1" % (self.build_dir,
                                              self.project.pre_build_cmd,
                                              prebuild_log))
            run_cmd(cmd)
            
        cmd = ("cd %s && \\\n%s \\\nDISTCC_LOG='%s' \\\nCC='%s' \\\nCXX='%s' \\\n%s \\\n>%s 2>&1" % 
               (self.build_dir, self.project.build_cmd, distcc_log,
                self.compiler.cc,
                self.compiler.cxx,
                self.compiler.make_opts,
                build_log))
        result, elapsed = run_cmd(cmd)
        return elapsed


    def clean(self):
        clean_log = os.path.join(self.log_dir, "bench-clean.log")
        print "** Cleaning build directory"
        cmd = "cd %s && make clean >%s 2>&1" % (self.build_dir, clean_log)
        run_cmd(cmd)
        

    def scrub(self):
        print "** Removing build directory"
        run_cmd("rm -rf %s" % self.unpacked_dir)


    def build_actions(self, actions, summary):
        """Carry out selected actions.

        Catch exceptions and handle."""
        try:
            times = []
            if 'sweep' in actions:
                self.scrub()
            if 'unpack' in actions:
                self.unpack()
            if 'configure' in actions:
                self.configure(self.compiler)
            for i in range(self.n_repeats):
                if 'build' in actions:
                    times.append(self.build(summary))
                if 'clean' in actions:
                    self.clean()
            if 'scrub' in actions:
                self.scrub()
            summary.store(self.project, self.compiler, times)
        except KeyboardInterrupt:
            raise
        except:
            apply(sys.excepthook, sys.exc_info()) # print traceback
            summary.store(self.project, self.compiler, 'FAIL')
