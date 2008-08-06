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

from Project import Project
from compiler import CompilerSpec, prepare_shell_script_farm
import buildutil
from buildutil import make_dir, run_cmd, rm_files
import re, os, sys, time

# For parsing output of 'time -p'.
RE_TIME = re.compile(r"""^real \s* (\d*\.\d*)\n
                          user \s* (\d*\.\d*)\n
                          sys  \s* (\d*\.\d*)""",
                     re.VERBOSE | re.MULTILINE)

class TimeInfo:
   """A record of real, system, and user time."""
   def __init__(self, real=None, system=None, user=None, include_server=None):
      self.real = real
      self.system = system
      self.user = user
      self.include_server = include_server


class Build:
    """A Build is a combination of a Project and CompilerSpec.

    Note: when done with an object of this type, call its restore function;
    otherwise PATH will remain changed to an inappropriate value.
    """
    
    def __init__(self, project, compiler, n_repeats):
        self.project = project
        self.compiler = compiler
        self.n_repeats = n_repeats

        self.base_dir = os.path.join(os.getcwd(), "build", self.project.name,
                                     self.compiler.name)
        self.unpacked_dir = os.path.join(self.base_dir,
                                         self.project.unpacked_subdir)

        # Some packages need to be started from a subdirectory of their
        # unpacked form.  For example, Samba is compiled from the "source/"
        # subdirectory of the unpacked source.
        if self.project.build_subdir:
            self.build_dir = os.path.join(self.unpacked_dir,
                                          project.build_subdir)
        else:
            self.build_dir = self.unpacked_dir

        self.log_dir = self.build_dir
        self.configure_done = os.path.join(self.log_dir, "bench-configure.done")

    def __repr__(self):
        return "Build(%s, %s)" % (`self.project`, `self.compiler`)

    def _run_cmd_with_redirect_farm(self, cmd):
        """Initialize shell script farm for given compiler,
        augment PATH, and run cmd.

        A shell script farm is a set of scripts for dispatching a
        chosen compiler using distcc. For example, the 'cc' script may
        contain the one line:
          dist /usr/mine/gcc "$@"
        """
        farm_dir = os.path.join(self.build_dir, 'build-cc-script-farm')
        make_dir(farm_dir)
        print ("** Creating masquerading shell scripts in '%s'" % farm_dir)
        masquerade = os.path.join(self.build_dir, 'masquerade')
        prepare_shell_script_farm(self.compiler, farm_dir, masquerade)
        old_path = os.environ['PATH'] 
        try:
            os.environ['PATH'] = farm_dir + ":" + old_path
            return run_cmd(cmd)
        finally:
            os.environ['PATH'] = old_path

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


    def configure(self):
        """Run configuration command for this tree, if any."""
        make_dir(self.log_dir)

        configure_log = os.path.join(self.log_dir, "bench-configure.log")
        distcc_log = os.path.join(self.log_dir, "bench-configure-distcc.log")

        rm_files((configure_log, distcc_log, self.configure_done))

        make_dir(self.build_dir)
        print "** Configuring..."
        cmd = ("cd %s && \\\nDISTCC_LOG='%s' \\\nCC='%s' \\\nCXX='%s' \\\n%s \\\n>%s 2>&1"
               % (self.build_dir, distcc_log,
                  self.compiler.cc, self.compiler.cxx,
                  self.project.configure_cmd, configure_log))
        self._run_cmd_with_redirect_farm(cmd)
        # Touch a file if the configure was successfully done, so we know.
        open(self.configure_done, 'w').close()

    @staticmethod
    def _extract_time_info(log_file_name):
	"""Open log file and look for output of 'time -p' and include server 
	time."""
	log_file = open(log_file_name, 'r')
	text = log_file.read()
	log_file.close()

	match = RE_TIME.search(text)
	if not match:
	    sys.exit('Could not locate time information in log %s.'
		     % log_file_name)
	time_info = TimeInfo(float(match.group(1)),
			     float(match.group(2)),
			     float(match.group(3)))
	# Now locate include server cpu time if present.
	lines = text.splitlines()
	for line in lines:
	   if line.startswith('Include server timing.  '):
	      is_time = float(
		 line[len('Include server timing.  '):].split()[9][:-1])
	      time_info.include_server = is_time
	      break
	return time_info

    def did_configure(self):
        """Returns true if configure was successfully run for this
        build in the past.
        """
        return os.path.isfile(self.configure_done)

    def build(self):
        """Actually build the package."""

        build_log = os.path.join(self.log_dir, "bench-build.log")
        prebuild_log = os.path.join(self.log_dir, "bench-prebuild.log")

        distcc_log = os.path.join(self.log_dir, "bench-build-distcc.log")

        rm_files((build_log, distcc_log))

        make_dir(self.build_dir)
        print "** Building..."
        if self.project.pre_build_cmd:
            cmd = ("cd %s && %s > %s 2>&1" % (self.build_dir,
                                              self.project.pre_build_cmd,
                                              prebuild_log))
            self._run_cmd_with_redirect_farm(cmd)
        distcc_hosts = buildutil.tweak_hosts(os.getenv("DISTCC_HOSTS"),
                                             self.compiler.num_hosts,
                                             self.compiler.host_opts)
        # We use built-in 'time' to measure real, system, and user time.  To
        # allow its stderr to be grabbed, the time command is executed in a
        # subshell.
        cmd = ("cd %s && \\\n"
               "(time -p \\\n"
               "DISTCC_HOSTS='%s' \\\n"
               "INCLUDE_SERVER_ARGS='-t --unsafe_absolute_includes %s' \\\n"
               "%s%s \\\nDISTCC_LOG='%s' \\\nCC='%s' \\\nCXX='%s' "
               "\\\n%s)"
               "\\\n>%s 2>&1"
               %
               (self.build_dir,
                distcc_hosts,
                self.project.include_server_args,
                self.compiler.pump_cmd,
                self.project.build_cmd,
                distcc_log,
                self.compiler.cc,
                self.compiler.cxx,
                self.compiler.make_opts,
                build_log))
        result, unused_elapsed = self._run_cmd_with_redirect_farm(cmd)
        return (result, Build._extract_time_info(build_log))

    def clean(self):
        clean_log = os.path.join(self.log_dir, "bench-clean.log")
        make_dir(self.build_dir)
        print "** Cleaning build directory"
        cmd = "cd %s && make clean >%s 2>&1" % (self.build_dir, clean_log)
        self._run_cmd_with_redirect_farm(cmd)

    def scrub(self):
        print "** Removing build directory"
        rm_files((self.configure_done, ))
        run_cmd("rm -rf %s" % self.unpacked_dir)


    def build_actions(self, actions, summary):
        """Carry out selected actions.

        Catch exceptions and handle."""
        try:
            # The time_info_accumulator is normally a list.  But if something
            # goes wrong, it will contain a short string indicating the problem.
            time_info_accumulator = []
            if 'sweep' in actions:
                self.scrub()
            if 'unpack' in actions:
                self.unpack()
            if 'configure' in actions:
                self.configure()
            # This is a safety measure, in case a previous benchmark
            # run left the build in an incomplete state.
            if 'clean' in actions:
                self.clean()
            for i in range(self.n_repeats):
                if 'build' in actions:
                    (result, time_info) = self.build()
                    if result:  # that is, if result is bad!
                       time_info_accumulator = 'NON-ZERO STATUS'
                    elif isinstance(time_info_accumulator, list):
                       time_info_accumulator.append(time_info)
                if 'clean' in actions:
                    self.clean()
            if 'scrub' in actions:
                self.scrub()
            summary.store(self.project, self.compiler, time_info_accumulator)
        except KeyboardInterrupt:
            raise
        except:
            apply(sys.excepthook, sys.exc_info()) # print traceback
            summary.store(self.project, self.compiler, 'FAIL WITH EXCEPTION')
