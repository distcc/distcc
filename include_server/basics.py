#!/usr/bin/env python3
#
# Copyright 2007 Google Inc.
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
#
#
"""Common and low-level stuff for include server."""

__author__ = 'Nils Klarlund'

import glob
import os.path
import resource
import signal
import shutil
import sys
import tempfile


# MANAGEMENT OF TEMPORARY LOCATIONS FOR GENERATIONS OF COMPRESSED FILES


class ClientRootKeeper(object):
  """Determine the tmp directory to use for compressed files.

  Use the RAM disk-like /dev/shm as default place to store compressed files if
  available.  The protocol between the include server and distcc client
  stipulates that the top three directories constitute the prefix prepended to
  absolute file paths.

  Instance vars:
    client_tmp: a path, the place for creation of temporary directories.
    client_root: a path, the current such temporary directory
    _client_root_before_padding: a path kept for testing purposes

  A typical client root looks like:

  -  /tmp/tmpBDoZQV.include_server-6642-13/padding, or
  -  /dev/shm/tmpBDoZQV.include_server-6642-19

  Note that each path has exactly three directory components to it.  This is an
  invariant.  Some client roots are padded with '/padding' to satisfy the
  invariant.
  """

  # This constant is embedded in names of client root directories.
  INCLUDE_SERVER_NAME = 'include_server'

  def __init__(self):
    """Constructor."""
    if 'DISTCC_CLIENT_TMP' in os.environ:
      self.client_tmp = os.environ['DISTCC_CLIENT_TMP']
    elif os.path.isdir('/dev/shm') and os.access('/dev/shm',
                                                 os.X_OK + os.W_OK + os.R_OK):
      self.client_tmp = '/dev/shm'
    else:
      self.client_tmp = '/tmp'
    if not self.client_tmp or self.client_tmp[0] != '/':
      sys.exit("""DISTCC_CLIENT_TMP must start with '/'.""")
    self.client_tmp = self.client_tmp.rstrip('/')
    # To have room to make a temp directory, we'll need to have less than two
    # levels at this point.  Note: '/a/b'.split('/') == ['', 'a', 'b'].
    if len(self.client_tmp.split('/')) > 3:
      sys.exit('DISTCC_CLIENT_TMP must have at most two directory levels.')
    self.number_missing_levels = 3 - len(self.client_tmp.split('/'))
    self.client_root = None

  def Glob(self, pid_expr):
    """Glob unpadded client roots whose pid is matched by pid expression."""
    return glob.glob('%s/*.%s-%s-*'
                     % (self.client_tmp, self.INCLUDE_SERVER_NAME,
                        pid_expr))

  def ClientRootMakedir(self, generation):
    """Make a new client directory for a generation of compressed files.

    Arguments:
      generation: a natural number, usually 1 or slightly bigger; this number,
        minus 1, indicates how many times a reset of the caches has taken place.
    """
    try:
      # Create a unique identifier that will never repeat. Use pid as suffix for
      # cleanout mechanism that wipes files not associated with a running pid.
      self._client_root_before_padding = tempfile.mkdtemp(
          '.%s-%s-%d' %
          (self.INCLUDE_SERVER_NAME,
           os.getpid(), generation),
          dir=self.client_tmp)
      self.client_root = (self._client_root_before_padding
                          + '/padding' * self.number_missing_levels)
      if not os.path.isdir(self.client_root):
        os.makedirs(self.client_root)
    except (IOError, OSError) as why:
      sys.exit('Could not create client root directory %s: %s' %
               (self.client_root, why))

  def CleanOutClientRoots(self, pid=None):
    """Delete client root directories pertaining to this process.
    Args:
      pid: None (which means 'pid of current process') or an integer
    """
    if not pid:
      pid = os.getpid()
    for client_root in self.Glob(str(pid)):
      shutil.rmtree(client_root, ignore_errors=True)

  def CleanOutOthers(self):
    """Search for left-overs from include servers that have passed away."""
    # Find all client root subdirectories whether abandoned or not.
    distcc_directories = self.Glob('*')
    for directory in distcc_directories:
      # Fish out pid from end of directory name.
      hyphen_ultimate_position = directory.rfind('-')
      assert hyphen_ultimate_position != -1
      hyphen_penultimate_position = directory.rfind('-', 0,
                                                    hyphen_ultimate_position)
      assert hyphen_penultimate_position != -1
      pid_str = directory[hyphen_penultimate_position + 1:
                          hyphen_ultimate_position]
      try:
        pid = int(pid_str)
      except ValueError:
        continue  # Happens only if a spoofer is around.
      try:
        # Got a pid; does it still exist?
        os.getpgid(pid)
        continue
      except OSError:
        # Process pid does not exist. Nuke its associated files. This will
        # of course only succeed if the files belong the current uid of
        # this process.
        if not os.access(directory, os.W_OK):
          continue  # no access, not ours
        Debug(DEBUG_TRACE,
              "Cleaning out '%s' after defunct include server." % directory)
        self.CleanOutClientRoots(pid)

# COMPILATION SERVER

# An upper bound on the number of directory components in the temporary
# directory on the distccd server that corresponds to the root directory
# on the client machine.  Normally the server root is /tmp/distccd_XXXXXX.
# But it could be something different if TMPDIR is set when distccd is
# started.  See dcc_get_tmp_top() in ../src/tempfile.c.
# It turns out that our own test infrastructure (test/testdistcc.py)
# sets TMPDIR before invoking distccd, so this needs to be reasonably
# high, otherwise 'make distcheck' will fail.
MAX_COMPONENTS_IN_SERVER_ROOT = 20

# EMAILS

# For automated emails, see also src/emaillog.h.
DCC_EMAILLOG_WHOM_TO_BLAME = os.getenv('DISTCC_EMAILLOG_WHOM_TO_BLAME',
                                       'distcc-pump-errors')
EMAIL_SUBJECT = 'distcc-pump include server email'
CANT_SEND_MESSAGE = """Please notify %s that the distcc-pump include server
tried to send them email but failed.""" % DCC_EMAILLOG_WHOM_TO_BLAME
MAX_EMAILS_TO_SEND = 3

# TIME QUOTAS (SOLVING THE HALTING PROBLEM)

# The maximum user time the include server is allowed handling one request. This
# is a critical parameter because all caches are reset if this time is
# exceeded. And if all caches are reset, then the next request may take much
# longer time, possibly again exceeding the quota.  The parameter is also of
# importance to builds that involve compilations that distcc-pump does not grok:
# an amount of time roughly equal to this quota is wasted before CPP is invoked
# instead.
USER_TIME_QUOTA = 3.8  # seconds

# How often the following question is answered: has too much user time been
# spent in the include handler servicing the current request?
#
# FIXME(klarlund): SIGALRM should not be raised in code that has I/O. Fix
# include server so that this is guaranteed not to happen. Until then, we are
# careful to wait a full 4 s before issuing SIGALRM.
USER_TIME_QUOTA_CHECK_INTERVAL_TIME = 4  # seconds, an integer

# ALGORITHMS

SIMPLE = 0     # not implemented
MEMOIZING = 1  # only one currently implemented
ALGORITHMS = [SIMPLE, MEMOIZING]

# PYTHON TUNING

# The default for the first parameter of gc.set_threshold is 700; see
# http://www.isi.edu/~chiang/python.html for a discussion of why this parameter
# can be bumped up considerably for speed-up.  The new default of 10000 was
# tested on a very large application, where include server time CPU time drops
# from 151s to 118s (best times out of 10 runs). There was no seeming changes to
# memory usage.  Trying with 100,000 did not speed up the application further.
GC_THRESHOLD = 10000


# FLAGS FOR COMMAND LINE OPTIONS

opt_algorithm = MEMOIZING  # currently, only choice
opt_debug_pattern = 1  # see DEBUG below
opt_email_bound = MAX_EMAILS_TO_SEND
opt_exact_analysis = False         # use CPP instead of include analyzer
opt_print_times = False
opt_path_observation_re = None
opt_send_email = False
opt_simple_algorithm = False
opt_stat_reset_triggers = {}
opt_statistics = False
opt_unsafe_absolute_includes = False
opt_no_force_dirs = False
opt_verify = False     # whether to compare calculated include closure to that
                       # produced by compiler
opt_write_include_closure = False  # write include closures to file

# HELPER FUNCTION FOR STAT_RESET_TRIGGERS


def Stamp(path):
  """Return a stamp characterizing a file and its modification time."""
  try:
    st_inf = os.stat(path)
    # The inode and device identify a file uniquely.
    return (st_inf.st_mtime, st_inf.st_ino, st_inf.st_dev)
  except OSError:
    return None


# LANGUAGES AND FILE EXTENSIONS

# The languages that we recognize.
LANGUAGES = set(['c', 'c++', 'objective-c', 'objective-c++'])

# The suffixes, following last period, used for source files and
# preprocessed files, each with their corresponding source language.
TRANSLATION_UNIT_MAP = {
    # C
    'c': 'c', 'i': 'c',
    # C++
    'cc': 'c++', 'cpp': 'c++', 'cxx': 'c++', 'C': 'c++', 'CXX': 'c++',
    'ii': 'c++',
    # Objective C
    'm': 'objective-c', 'mi': 'objective-c',
    # Objective C++
    'mm': 'objective-c++', 'M': 'objective-c++', 'mii': 'objective-c++',
    }

# All languages are described by suffixes.
assert set(TRANSLATION_UNIT_MAP.values()) == LANGUAGES


# DEBUG

# Debugging is controlled by the 5 least significant bits of
# opt_debug_pattern.
DEBUG_WARNING = 1   # For warnings
DEBUG_TRACE = 2     # For tracing functions (upper level)
DEBUG_TRACE1 = 4    # For tracing functions (medium level)
DEBUG_TRACE2 = 8    # For tracing functions (lower level)
DEBUG_DATA = 16     # For printing data
DEBUG_NUM_BITS = 5  # The cardinality of {1,2,4,8,16}


def Debug(trigger_pattern, message, *params):
  """Print message to stderr depending on trigger pattern.

  Args:
    trigger_pattern: a bit vector (as an integer)
    message: a format string
    params: arguments to message
  """
  # TODO(klarlund): use Python's logging module.
  triggered = opt_debug_pattern & trigger_pattern
  if triggered:
    i = 1
    for unused_j in range(DEBUG_NUM_BITS):
      if i & DEBUG_WARNING & triggered:
        print('WARNING include server:', message % params, file=sys.stderr)
      if i & DEBUG_TRACE & triggered:
        print('TRACE:', message % params, file=sys.stderr)
      elif i & DEBUG_TRACE1 & triggered:
        print(sys.stderr, 'TRACE1:', message % params, file=sys.stderr)
      elif i & DEBUG_TRACE2 & triggered:
        print('TRACE2:', message % params, file=sys.stderr)
      elif i & DEBUG_DATA & triggered:
        print('DATA:', message % params, file=sys.stderr)
      i *= 2
    sys.stderr.flush()


# EXCEPTIONS


class Error(Exception):
  """For include server errors."""
  pass


class NotCoveredError(Error):
  """Exception for included file not covered by include processing."""

  def __init__(self, message,
               source_file=None,
               line_number=None,
               send_email=True):
    """Constructor.

    Arguments:
      message: text of error message
      source_file: name of source_file if known
      line_number: an integer, if known
      send_email: a Boolean, if False then never send email

    These arguments are all stored in the exception. However, the source_file
    and line_number are appended, in a syntax defined here, to the message
    before it is stored as self.args[0] through invocation of the Error
    constructor.
    """
    assert not line_number or source_file
    self.source_file = None
    self.line_number = None
    self.send_email = send_email
    if source_file:
      # Mark this exception as mentioning the source_file.
      self.source_file = source_file
      # Line numbers are not currently used.
      if line_number:
        self.line_number = line_number
        message = ("""File: '%s', line: %s: %s"""
                   % (source_file, line_number, message))
      else:
        message = """File: '%s': %s""" % (source_file, message)
    # Message, a string, becomes self.args[0]
    Error.__init__(self, message)


class NotCoveredTimeOutError(NotCoveredError):
  """Raised when spending too much time analyzing dependencies."""
  pass


class IncludeAnalyzerTimer(object):
  """Start a timer limiting CPU time for servicing a single request.

  We use user time so that a network hiccup will not entail a cache reset if,
  say, we are using NFS.

  An object of this class must be instantiated so that, no matter what, the
  Cancel method is eventually called. This reinstates the original timer (if
  present).
  """

  def __init__(self):
    self.start_utime = resource.getrusage(resource.RUSAGE_SELF).ru_utime
    self.old = signal.signal(signal.SIGALRM, self._TimeIsUp)
    signal.alarm(USER_TIME_QUOTA_CHECK_INTERVAL_TIME)

  def _TimeIsUp(self, unused_sig_number, unused_frame):
    """Check CPU time spent and raise exception or reschedule."""
    if (resource.getrusage(resource.RUSAGE_SELF).ru_utime
        > self.start_utime + USER_TIME_QUOTA):
      raise NotCoveredTimeOutError(('Bailing out because include server '
                                    + 'spent more than %3.1fs user time '
                                    + 'handling request') %
                                   USER_TIME_QUOTA)
    else:
      # Reschedule ourselves.
      signal.alarm(USER_TIME_QUOTA_CHECK_INTERVAL_TIME)

  def Stop(self):
    signal.alarm(0)

  def Start(self):
    signal.alarm(USER_TIME_QUOTA_CHECK_INTERVAL_TIME)

  def Cancel(self):
    """Must be called eventually. See class documentation."""
    sys.stdout.flush()
    signal.alarm(0)
    signal.signal(signal.SIGALRM, self.old)


class SignalSIGTERM(Error):
  pass


def RaiseSignalSIGTERM(*unused_args):
  """Raise SignalSIGTERM.

  Use signal.signal for binding this function to SIGTERM.
  """
  raise SignalSIGTERM


# COMMON FUNCTIONS


def SafeNormPath(path):
  """Safe, but limited, version of os.path.normpath.

  Args:
    path: a string

  Returns:
    a string

  Python's os.path.normpath is an unsafe operation; the result may not point to
  the same file as the argument. Instead, this function just removes
  initial './'s and a final '/'s if present.
  """
  if path == '.':
    return ''
  else:
    while path.startswith('./'):
      path = path[2:]
    return path.rstrip('/')
