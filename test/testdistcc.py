#! /usr/bin/env python3

# Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
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

"""distcc test suite, using comfychair

This script is called with $PATH pointing to the appropriate location
for the built (or installed) programs to be tested.

Options:
  --valgrind[=command]    Run the tests under valgrind.
                          Every program invocation will be prefixed
                          with the valgrind command, which defaults to
                          "valgrind --quiet".
  --lzo                   Run the server tests with lzo compression enabled.
  --pump                  Run the server tests with remote preprocessing
                          enabled.
Example:
  PATH="`pwd`:$PATH"
  python test/testdistcc.py --valgrind="valgrind --quiet --num-callers=20"
"""


# There are pretty strong hierarchies of test cases: ones to do with
# running a daemon, compiling a file and so on.  This nicely maps onto
# a hierarchy of object classes.

# It seems to work best if an instance of the class corresponds to an
# invocation of a test: this means each method runs just once and so
# object state is not very useful, but nevermind.

# Having a complicated patterns of up and down-calls within the class
# methods seems to make things more complicated.  It may be better if
# abstract superclasses just provide methods that can be called,
# rather than establishing default behaviour.

# TODO: Some kind of direct test of the host selection algorithm.

# TODO: Test host files containing \r.

# TODO: Test that ccache correctly caches compilations through distcc:
# make up a random file so it won't hit, then compile once and compile
# twice and examine the log file to make sure we got a hit.  Also
# check that the binary works properly.

# TODO: Test cpp from stdin

# TODO: Do all this with malloc debugging on.

# TODO: Redirect daemon output to a file so that we can more easily
# check it.  Is there a straightforward way to test that it's also OK
# when send through syslogd?

# TODO: Check behaviour when children are killed off.

# TODO: Test compiling over IPv6

# TODO: Argument scanning tests should be run with various hostspecs,
# because that makes a big difference to how the client handles them.

# TODO: Test that ccache gets hits when calling distcc.  Presumably
# this is skipped if we can't find ccache.  Need to parse `ccache -s`.

# TODO: Set TMPDIR to be inside the working directory, and perhaps
# also set DISTCC_SAVE_TEMPS.  Might help for debugging.

# Check that without DISTCC_SAVE_TEMPS temporary files are cleaned up.

# TODO: Perhaps redirect stdout, stderr to a temporary file while
# running?  Use os.open(), os.dup2().

# TODO: Test crazy option arguments like "distcc -o -output -c foo.c"

# TODO: Add test harnesses that just exercise the bulk file transfer
# routines.

# TODO: Test -MD, -MMD, -M, etc.

# TODO: Test using '.include' in an assembly file, and make sure that
# it is resolved on the client, not on the server.

# TODO: Run "sleep" as a compiler, then kill the client and make sure
# that the server and "sleep" promptly terminate.

# TODO: Perhaps have a little compiler that crashes.  Check that the
# signal gets properly reported back.

# TODO: Have a little compiler that takes a very long time to run.
# Try interrupting the connection and see if the compiler is cleaned
# up in a reasonable time.

# TODO: Try to build a nonexistent source file.  Check that we only
# get one error message -- if there were two, we would incorrectly
# have tried to build the program both remotely and locally.

# TODO: Test compiling a 0-byte source file.  This should be allowed.

# TODO: Test a compiler that produces 0 byte output.  I don't know an
# easy way to get that out of gcc aside from the Apple port though.

# TODO: Test a compiler that sleeps for a long time; try killing the
# server and make sure it goes away.

# TODO: Test scheduler.  Perhaps run really slow jobs to make things
# deterministic, and test that they're dispatched in a reasonable way.

# TODO: Test generating dependencies with -MD.  Possibly can't be
# done.

# TODO: Test a nasty cpp that always writes to stdout regardless of
# -o.

# TODO: Test giving up privilege using --user.  Difficult -- we may
# need root privileges to run meaningful tests.

# TODO: Test that recursion safeguard works.

# TODO: Test masquerade mode.  Requires us to create symlinks in a
# special directory on the path.

# TODO: Test SSH mode.  May need to skip if we can't ssh to this
# machine.  Perhaps provide a little null-ssh.

# TODO: Test path stripping.

# TODO: Test backoff from downed hosts.

# TODO: Check again in --no-prefork mode.

# TODO: Test lzo is parsed properly

# TODO: Test with DISTCC_DIR set, and not set.

# TODO: Using --lifetime does cause sporadic failures.  Ensure that
# teardown kills all daemon processes and then stop using --lifetime.


import time, sys, string, os, glob, re, socket
import signal, os.path
import comfychair

from stat import *                      # this is safe

EXIT_DISTCC_FAILED           = 100
EXIT_BAD_ARGUMENTS           = 101
EXIT_BIND_FAILED             = 102
EXIT_CONNECT_FAILED          = 103
EXIT_COMPILER_CRASHED        = 104
EXIT_OUT_OF_MEMORY           = 105
EXIT_BAD_HOSTSPEC            = 106
EXIT_COMPILER_MISSING        = 110
EXIT_ACCESS_DENIED           = 113

DISTCC_TEST_PORT             = 42000

_cc                          = None     # full path to gcc
_valgrind_command            = "" # Command to invoke valgrind (or other
                                  # similar debugging tool).
                                  # e.g. "valgrind --quiet --num-callsers=20 "
_server_options              = "" # Distcc host options to use for the server.
                                  # Should be "", ",lzo", or ",lzo,cpp".

def _ShellSafe(s):
    '''Returns a version of s that will be interpreted literally by the shell.'''
    return "'" + s.replace("'", "'\"'\"'") + "'"

# Some tests only make sense for certain object formats
def _FirstBytes(filename, count):
    '''Returns the first count bytes from the given file.'''
    f = open(filename, 'rb')
    try:
        return f.read(count)
    finally:
        f.close()

def _IsElf(filename):
    '''Given a filename, determine if it's an ELF object file or
    executable.  The magic number used ('\177ELF' at file-start) is
    taken from /usr/share/file/magic on an ubuntu machine.
    '''
    contents = _FirstBytes(filename, 5)
    return contents.startswith(b'\177ELF')

def _IsMachO(filename):
    '''Given a filename, determine if it's an Mach-O object file or
    executable.  The magic number used ('0xcafebabe' or '0xfeedface')
    is taken from /usr/share/file/magic on an ubuntu machine.
    '''
    contents = _FirstBytes(filename, 10)
    return (contents.startswith(b'\xCA\xFE\xBA\xBE') or
            contents.startswith(b'\xFE\xED\xFA\xCE') or
            contents.startswith(b'\xCE\xFA\xED\xFE') or
            # The magic file says '4-bytes (BE) & 0xfeffffff ==
            # 0xfeedface' and '4-bytes (LE) & 0xfffffffe ==
            # 0xfeedface' are also mach-o.
            contents.startswith(b'\xFF\xED\xFA\xCE') or
            contents.startswith(b'\xCE\xFA\xED\xFF'))

def _IsPE(filename):
    '''Given a filename, determine if it's a Microsoft PE object file or
    executable.  The magic number used ('MZ') is taken from
    /usr/share/file/magic on an ubuntu machine.
    '''
    contents = _FirstBytes(filename, 5)
    return contents.startswith(b'MZ')

def _Touch(filename):
    '''Update the access and modification time of the given file,
    creating an empty file if it does not exist.
    '''
    f = open(filename, 'a')
    try:
        os.utime(filename, None)
    finally:
        f.close()


class SimpleDistCC_Case(comfychair.TestCase):
    '''Abstract base class for distcc tests'''
    def setup(self):
        self.stripEnvironment()
        self.initCompiler()

    def initCompiler(self):
        self._cc = self._get_compiler()

    def stripEnvironment(self):
        """Remove all DISTCC variables from the environment, so that
        the test is not affected by the development environment."""
        for key in list(os.environ.keys()):
            if key[:7] == 'DISTCC_':
                # NOTE: This only works properly on Python 2.2: on
                # earlier versions, it does not call unsetenv() and so
                # subprocesses may get confused.
                del os.environ[key]
        os.environ['TMPDIR'] = self.tmpdir
        ddir = os.path.join(self.tmpdir, 'distccdir')
        os.mkdir(ddir)
        os.environ['DISTCC_DIR'] = ddir

    def valgrind(self):
        return _valgrind_command;

    def distcc(self):
        return self.valgrind() + "distcc "

    def distccd(self):
        return self.valgrind() + "distccd "

    def distcc_with_fallback(self):
        return "DISTCC_FALLBACK=1 " + self.distcc()

    def distcc_without_fallback(self):
        return "DISTCC_FALLBACK=0 " + self.distcc()

    def _get_compiler(self):
        cc = self._find_compiler("cc")
        if self.is_clang(cc):
            return self._find_compiler("clang")
        elif self.is_gcc(cc):
            return self._find_compiler("gcc")
        raise AssertionError("Unknown compiler")

    def _find_compiler(self, compiler):
        for path in os.environ['PATH'].split (':'):
            abs_path = os.path.join (path, compiler)

            if os.path.isfile (abs_path):
                return abs_path
        return None

    def is_gcc(self, compiler):
        out, err = self.runcmd(compiler + " --version")
        if re.search('Free Software Foundation', out):
            return True
        return False

    def is_clang(self, compiler):
        out, err = self.runcmd(compiler + " --version")
        if re.search('clang', out):
            return True
        return False


class WithDaemon_Case(SimpleDistCC_Case):
    """Start the daemon, and then run a command locally against it.

The daemon doesn't detach until it has bound the network interface, so
as soon as that happens we can go ahead and start the client."""

    def setup(self):
        SimpleDistCC_Case.setup(self)
        self.daemon_pidfile = os.path.join(os.getcwd(), "daemonpid.tmp")
        self.daemon_logfile = os.path.join(os.getcwd(), "distccd.log")
        self.server_port = DISTCC_TEST_PORT # random.randint(42000, 43000)
        self.startDaemon()
        self.setupEnv()

    def setupEnv(self):
        os.environ['DISTCC_HOSTS'] = ('127.0.0.1:%d%s' %
          (self.server_port, _server_options))
        os.environ['DISTCC_LOG'] = os.path.join(os.getcwd(), 'distcc.log')
        os.environ['DISTCC_VERBOSE'] = '1'


    def teardown(self):
        SimpleDistCC_Case.teardown(self)


    def killDaemon(self):
        try:
            pid = int(open(self.daemon_pidfile, 'rt').read())
        except IOError:
            # the daemon probably already exited, perhaps because of a timeout
            return
        os.kill(pid, signal.SIGTERM)

        # We can't wait on it, because it detached.  So just keep
        # pinging until it goes away.
        while 1:
            try:
                os.kill(pid, 0)
            except OSError:
                break
            time.sleep(0.2)


    def daemon_command(self):
        """Return command to start the daemon"""
        return (self.distccd() +
                "--verbose --lifetime=%d --daemon --log-file %s "
                "--pid-file %s --port %d --allow 127.0.0.1 --enable-tcp-insecure"
                % (self.daemon_lifetime(),
                   _ShellSafe(self.daemon_logfile),
                   _ShellSafe(self.daemon_pidfile),
                   self.server_port))

    def daemon_lifetime(self):
        # Enough for most tests, even on a fairly loaded machine.
        # Might need more for long-running tests.
        return 60

    def startDaemon(self):
        """Start a daemon in the background, return its pid"""
        # The daemon detaches once it has successfully bound the
        # socket, so if something goes wrong at startup we ought to
        # find out straight away.  If it starts successfully, then we
        # can go ahead and try to connect.
        # We run the daemon in a 'daemon' subdirectory to make
        # sure that it has a different directory than the client.
        old_tmpdir = os.environ['TMPDIR']
        daemon_tmpdir = old_tmpdir + "/daemon_tmp"
        os.mkdir(daemon_tmpdir)
        os.environ['TMPDIR'] = daemon_tmpdir
        os.mkdir("daemon")
        os.chdir("daemon")
        try:
          while 1:
            cmd = self.daemon_command()
            result, out, err = self.runcmd_unchecked(cmd)
            if result == 0:
                break
            elif result == EXIT_BIND_FAILED:
                self.server_port += 1
                continue
            else:
                self.fail("failed to start daemon: %d" % result)
          self.add_cleanup(self.killDaemon)
        finally:
          os.environ['TMPDIR'] = old_tmpdir
          os.chdir("..")

class StartStopDaemon_Case(WithDaemon_Case):
    def runtest(self):
        pass


class VersionOption_Case(SimpleDistCC_Case):
    """Test that --version returns some kind of version string.

    This is also a good test that the programs were built properly and are
    executable."""
    def runtest(self):
        for prog in 'distcc', 'distccd':
            out, err = self.runcmd("%s --version" % prog)
            assert out[-1] == '\n'
            out = out[:-1]
            line1,line2,trash = out.split('\n', 2)
            self.assert_re_match(r'^%s [\w.-]+ [.\w-]+$'
                                 % prog, line1)
            self.assert_re_match(r'^[ \t]+\(protocol.*\) \(default port 3632\)$'
                                 , line2)


class HelpOption_Case(SimpleDistCC_Case):
    """Test --help is reasonable."""
    def runtest(self):
        for prog in 'distcc', 'distccd':
            out, err = self.runcmd(prog + " --help")
            self.assert_re_search("Usage:", out)


class BogusOption_Case(SimpleDistCC_Case):
    """Test handling of --bogus-option.

    Now that we support implicit compilers, this is passed to gcc,
    which returns a non-zero status."""
    def runtest(self):
        error_rc, _, _ = self.runcmd_unchecked(self._cc + " --bogus-option")
        assert error_rc != 0
        self.runcmd(self.distcc() + self._cc + " --bogus-option", error_rc)
        self.runcmd(self.distccd() + self._cc + " --bogus-option",
                    EXIT_BAD_ARGUMENTS)


class CompilerOptionsPassed_Case(SimpleDistCC_Case):
    """Test that options following the compiler name are passed to the compiler."""
    def runtest(self):
        out, err = self.runcmd("DISTCC_HOSTS=localhost "
                               + self.distcc()
                               + self._cc + " --help")
        if re.search('distcc', out):
            raise AssertionError("compiler help contains \"distcc\": \"%s\"" % out)
        if self.is_gcc(self._cc):
            self.assert_re_match(r"Usage: [^ ]*gcc", out)
        elif self.is_clang(self._cc):
            self.assert_re_match(r"OVERVIEW: [^ ]*clang", out)
        else:
            raise AssertionError("Unknown compiler found")


class StripArgs_Case(SimpleDistCC_Case):
    """Test -D and -I arguments are removed"""
    def runtest(self):
        cases = (("gcc -c hello.c", "gcc -c hello.c"),
                 ("cc -Dhello hello.c -c", "cc hello.c -c"),
                 ("gcc -g -O2 -W -Wall -Wshadow -Wpointer-arith -Wcast-align -c -o h_strip.o h_strip.c",
                  "gcc -g -O2 -W -Wall -Wshadow -Wpointer-arith -Wcast-align -c -o h_strip.o h_strip.c"),
                 # invalid but should work
                 ("cc -c hello.c -D", "cc -c hello.c"),
                 ("cc -c hello.c -D -D", "cc -c hello.c"),
                 ("cc -c hello.c -I ../include", "cc -c hello.c"),
                 ("cc -c -I ../include  hello.c", "cc -c hello.c"),
                 ("cc -c -I. -I.. -I../include -I/home/mbp/garnome/include -c -o foo.o foo.c",
                  "cc -c -c -o foo.o foo.c"),
                 ("cc -c -DDEBUG -DFOO=23 -D BAR -c -o foo.o foo.c",
                  "cc -c -c -o foo.o foo.c"),

                 # New options stripped in 0.11
                 ("cc -o nsinstall.o -c -DOSTYPE=\"Linux2.4\" -DOSARCH=\"Linux\" -DOJI -D_BSD_SOURCE -I../dist/include -I../dist/include -I/home/mbp/work/mozilla/mozilla-1.1/dist/include/nspr -I/usr/X11R6/include -fPIC -I/usr/X11R6/include -Wall -W -Wno-unused -Wpointer-arith -Wcast-align -pedantic -Wno-long-long -pthread -pipe -DDEBUG -D_DEBUG -DDEBUG_mbp -DTRACING -g -I/usr/X11R6/include -include ../config-defs.h -DMOZILLA_CLIENT -Wp,-MD,.deps/nsinstall.pp nsinstall.c",
                  "cc -o nsinstall.o -c -fPIC -Wall -W -Wno-unused -Wpointer-arith -Wcast-align -pedantic -Wno-long-long -pthread -pipe -g nsinstall.c"),
                 )
        for cmd, expect in cases:
            o, err = self.runcmd("h_strip %s" % cmd)
            if o[-1] == '\n': o = o[:-1]
            self.assert_equal(o, expect)


class IsSource_Case(SimpleDistCC_Case):
    def runtest(self):
        """Test distcc's method for working out whether a file is source"""
        cases = (( "hello.c",          "source",       "not-preprocessed" ),
                 ( "hello.cc",         "source",       "not-preprocessed" ),
                 ( "hello.cxx",        "source",       "not-preprocessed" ),
                 ( "hello.cpp",        "source",       "not-preprocessed" ),
                 ( "hello.c++",        "source",       "not-preprocessed" ),
                 # ".m" is Objective-C; ".M" and ".mm" are Objective-C++
                 ( "hello.m",          "source",       "not-preprocessed" ),
                 ( "hello.M",          "source",       "not-preprocessed" ),
                 ( "hello.mm",         "source",       "not-preprocessed" ),
                 # ".mi" and ".mii" are preprocessed Objective-C/Objective-C++.
                 ( "hello.mi",         "source",       "preprocessed" ),
                 ( "hello.mii",        "source",       "preprocessed" ),
                 ( "hello.2.4.4.i",    "source",       "preprocessed" ),
                 ( ".foo",             "not-source",   "not-preprocessed" ),
                 ( "gcc",              "not-source",   "not-preprocessed" ),
                 ( "hello.ii",         "source",       "preprocessed" ),
                 ( "boot.s",           "not-source",   "not-preprocessed" ),
                 ( "boot.S",           "not-source",   "not-preprocessed" ))
        for f, issrc, iscpp in cases:
            o, err = self.runcmd("h_issource '%s'" % f)
            expected = ("%s %s\n" % (issrc, iscpp))
            if o != expected:
                raise AssertionError("issource %s gave %s, expected %s" %
                                     (f, repr(o), repr(expected)))



class ScanArgs_Case(SimpleDistCC_Case):
    '''Test understanding of gcc command lines.'''
    def runtest(self):
        cases = [("gcc -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("gcc hello.c", "local"),
                 ("gcc -o /tmp/hello.o -c ../src/hello.c", "distribute", "../src/hello.c", "/tmp/hello.o"),
                 ("gcc -DMYNAME=quasibar.c bar.c -c -o bar.o", "distribute", "bar.c", "bar.o"),
                 ("gcc -ohello.o -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("ccache gcc -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("gcc hello.o", "local"),
                 ("gcc -o hello.o hello.c", "local"),
                 ("gcc -o hello.o -c hello.s", "local"),
                 ("gcc -o hello.o -c hello.S", "local"),
                 ("gcc -fprofile-arcs -ftest-coverage -c hello.c", "local", "hello.c", "hello.o"),
                 ("gcc -S hello.c", "distribute", "hello.c", "hello.s"),
                 ("gcc -c -S hello.c", "distribute", "hello.c", "hello.s"),
                 ("gcc -S -c hello.c", "distribute", "hello.c", "hello.s"),
                 ("gcc -M hello.c", "local"),
                 ("gcc -ME hello.c", "local"),
                 ("gcc -MD -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("gcc -MMD -c hello.c", "distribute", "hello.c", "hello.o"),

                 # Assemble to stdout (thanks Alexandre).
                 ("gcc -S foo.c -o -", "local"),
                 ("-S -o - foo.c", "local"),
                 ("-c -S -o - foo.c", "local"),
                 ("-S -c -o - foo.c", "local"),

                 # dasho syntax
                 ("gcc -ofoo.o foo.c -c", "distribute", "foo.c", "foo.o"),
                 ("gcc -ofoo foo.o", "local"),

                 # tricky this one -- no dashc
                 ("foo.c -o foo.o", "local"),
                 ("foo.c -o foo.o -c", "distribute", "foo.c", "foo.o"),

                 # Produce assembly listings
                 ("gcc -Wa,-alh,-a=foo.lst -c foo.c", "local"),
                 ("gcc -Wa,--MD -c foo.c", "local"),
                 ("gcc -Wa,-xarch=v8 -c foo.c", "distribute", "foo.c", "foo.o"),

                 # Produce .rpo files
                 ("g++ -frepo foo.C", "local"),

                 ("gcc -xassembler-with-cpp -c foo.c", "local"),
                 ("gcc -x assembler-with-cpp -c foo.c", "local"),

                 ("gcc -specs=foo.specs -c foo.c", "local"),

                 # Fixed in 2.18.4 -- -dr writes rtl to a local file
                 ("gcc -dr -c foo.c", "local"),
                 ]
        for tup in cases:
            self.checkScanArgs(*tup)

    def checkScanArgs(self, ccmd, mode, input=None, output=None):
        o, err = self.runcmd("h_scanargs %s" % ccmd)
        o = o[:-1]                      # trim \n
        os = o.split()
        if mode != os[0]:
            self.fail("h_scanargs %s gave %s mode, expected %s" %
                      (ccmd, os[0], mode))
        if mode == 'distribute':
            if os[1] != input:
                self.fail("h_scanargs %s gave %s input, expected %s" %
                          (ccmd, os[1], input))
            if os[2] != output:
                self.fail("h_scanargs %s gave %s output, expected %s" %
                          (ccmd, os[2], output))


class DotD_Case(SimpleDistCC_Case):
    '''Test the mechanism for calculating .d file names'''

    def runtest(self):
        # Each case specifies:
        #
        # - A compilation command.
        #
        # - A glob expression supposed to match exactly one file, the dependency
        #   file (which is not always a .d file, btw). The glob expression is
        #   our human intuition, based on our reading of the gcc manual pages,
        #   of the range of possible dependency names actually produced.
        #
        # - Whether 0 or 1 such dependency files exist.
        #
        # - The expected target name (or None).
        #

        # The dotd_name is thus divined by examination of the compilation
        # directory where we actually run gcc.

        cases = [
          ("foo.c -o hello.o -MD", "*.d", 1, None),
          ("foo.c -o hello.. -MD", "*.d", 1, None),
          ("foo.c -o hello.bar.foo -MD", "*.d", 1, None),
          ("foo.c -o hello.o", "*.d", 0, None),
          ("foo.c -o hello.bar.foo -MD", "*.d", 1, None),
          ("foo.c -MD", "*.d", 1, None),
          ("foo.c -o hello. -MD", "*.d", 1, None),
# The following test case fails under Darwin Kernel Version 8.11.0. For some
# reason, gcc refuses to produce 'hello.d' when the object file is named
# 'hello.D'.
#         ("foo.c -o hello.D -MD -MT tootoo", "hello.*d", 1, "tootoo"),
          ("foo.c -o hello. -MD -MT tootoo",  "hello.*d", 1, "tootoo"),
          ("foo.c -o hello.o -MD -MT tootoo", "hello.*d", 1, "tootoo"),
          ("foo.c -o hello.o -MD -MF foobar", "foobar", 1, None),
           ]

        # These C++ cases fail if your gcc installation doesn't support C++.
        error_rc, _, _ = self.runcmd_unchecked("touch testtmp.cpp; " +
            self._cc + " -c testtmp.cpp -o /dev/null")
        if error_rc == 0:
          cases.extend([("foo.cpp -o hello.o", "*.d", 0, None),
                        ("foo.cpp -o hello", "*.d", 0, None)])

        def _eval(out):
            map_out = eval(out)
            return (map_out['dotd_fname'],
                    map_out['needs_dotd'],
                    map_out['sets_dotd_target'],
                    map_out['dotd_target'])

        for (args, dep_glob, how_many, target) in cases:

            # Determine what gcc says.
            dotd_result = []  # prepare for some imperative style value passing
            class TempCompile_Case(Compilation_Case):
                def source(self):
                      return """
int main(void) { return 0; }
"""
                def sourceFilename(self):
                    return args.split()[0]
                def compileCmd(self):
                    return self._cc + " -c " + args
                def runtest(self):
                    self.compile()
                    glob_result = glob.glob(dep_glob)
                    dotd_result.extend(glob_result)

            ret = comfychair.runtest(TempCompile_Case, 0, subtest=1)
            if ret:
                raise AssertionError(
                    "Case (args:%s, dep_glob:%s, how_many:%s, target:%s)"
                    %  (args, dep_glob, how_many, target))
            self.assert_equal(len(dotd_result), how_many)
            if how_many == 1:
                expected_dep_file = dotd_result[0]

            # Determine what dcc_get_dotd_info says.
            out, _err = self.runcmd("h_dotd dcc_get_dotd_info gcc -c %s" % args)
            dotd_fname, needs_dotd, sets_dotd_target, dotd_target = _eval(out)
            assert dotd_fname
            assert needs_dotd in [0,1]
            # Assert that "needs_dotd == 1" if and only if "how_many == 1".
            assert needs_dotd == how_many
            # Assert that "needs_dotd == 1" implies names by gcc and our routine
            # are the same.
            if needs_dotd:
                self.assert_equal(expected_dep_file, dotd_fname)

            self.assert_equal(sets_dotd_target == 1, target != None)
            if target:
                # A little convoluted: because target is set in command line,
                # and the command line is passed already, the dotd_target is not
                # set.
                self.assert_equal(dotd_target, "None")


        # Now some fun with DEPENDENCIES_OUTPUT variable.
        try:
            os.environ["DEPENDENCIES_OUTPUT"] = "xxx.d yyy"
            out, _err = self.runcmd("h_dotd dcc_get_dotd_info gcc -c foo.c")
            dotd_fname, needs_dotd, sets_dotd_target, dotd_target = _eval(out)
            assert dotd_fname == "xxx.d"
            assert needs_dotd
            assert not sets_dotd_target
            assert dotd_target == "yyy"

            os.environ["DEPENDENCIES_OUTPUT"] = "zzz.d"
            out, _err = self.runcmd("h_dotd dcc_get_dotd_info gcc -c foo.c")
            dotd_fname, needs_dotd, sets_dotd_target, dotd_target = _eval(out)
            assert dotd_fname == "zzz.d"
            assert needs_dotd
            assert not sets_dotd_target
            assert dotd_target == "None"

        finally:
            del os.environ["DEPENDENCIES_OUTPUT"]


class Compile_c_Case(SimpleDistCC_Case):
  """Unit tests for source file 'compile.c.'

  Currently, only the functions dcc_fresh_dependency_exists() and
  dcc_discrepancy_filename() are tested.
  """

  def getDep(self, line):
      """Parse line to yield dependency name. From say:
          "src/h_compile.c[21010] (dcc_fresh_dependency_exists) Checking dependency: bar_bar"
         return "bar_bar".
      """
      m_obj = re.search(r"Checking dependency: ((\w|[.])*)", line)
      assert m_obj, line
      return m_obj.group(1)

  def runtest(self):

      # Test dcc_discrepancy_filename
      # ********************************
      os.environ['INCLUDE_SERVER_PORT'] = "abc/socket"
      out, err = self.runcmd(
              "h_compile dcc_discrepancy_filename")
      self.assert_equal(out, "abc/discrepancy_counter")

      os.environ['INCLUDE_SERVER_PORT'] = "socket"
      out, err = self.runcmd(
              "h_compile dcc_discrepancy_filename")
      self.assert_equal(out, "(NULL)")

      # os.environ will be cleaned out at start of next test.

      # Test dcc_fresh_dependency_exists
      # ********************************
      dotd_cases = [("""
foo.o: foo\
bar.h bar.h notthisone.h bar.h\
""",
                     ["foobar.h", "bar.h"]),
                    (
                      """foo_foo  :\
bar_bar \
foo_bar""",
                      ["bar_bar", "foo_bar"]),
                    (":", []),
                    ("\n", []),
                    ("", []),
                    ("foo.o:", []),
                    ]

      for dotd_contents, deps in dotd_cases:
          for dep in deps:
              _Touch(dep)
          # Now postulate the time that is the beginning of build. This time
          # is after that of all the dependencies.
          time_ref = time.time() + 1
          # Let real-time advance to time_ref.
          while time.time() < time_ref:
              time.sleep(1)
          # Create .d file now, so that it appears to be no older than
          # time_ref.
          dotd_fd = open("dotd", "w")
          dotd_fd.write(dotd_contents)
          dotd_fd.close()
          # Check: no fresh files here!
          out, err = self.runcmd(
              "h_compile dcc_fresh_dependency_exists dotd '%s' %i" %
              ("*notthis*", time_ref))
          self.assert_equal(out.split()[1], "(NULL)");
          checked_deps = {}
          for line in err.split("\n"):
              if re.search("[^ ]", line):
                  # Line is non-blank
                  checked_deps[self.getDep(line)] = 1
          deps_list = deps[:]
          checked_deps_list = list(checked_deps.keys())
          deps_list.sort()
          checked_deps_list.sort()
          self.assert_equal(checked_deps_list, deps_list)

          # Let's try to touch, say the last dep file. Then, we should expect
          # the name of that very file as the output because there's a fresh
          # file.
          if deps:
              _Touch(deps[-1])
              out, err = self.runcmd(
                  "h_compile dcc_fresh_dependency_exists dotd '' %i" %
                  time_ref)
              self.assert_equal(out.split()[1], deps[-1])


class ImplicitCompilerScan_Case(ScanArgs_Case):
    '''Test understanding of commands with no compiler'''
    def runtest(self):
        cases = [("-c hello.c",            "distribute", "hello.c", "hello.o"),
                 ("hello.c -c",            "distribute", "hello.c", "hello.o"),
                 ("-o hello.o -c hello.c", "distribute", "hello.c", "hello.o"),
                 ]
        for tup in cases:
            # NB use "apply" rather than new syntax for compatibility with
            # venerable Pythons.
            self.checkScanArgs(*tup)


class ExtractExtension_Case(SimpleDistCC_Case):
    def runtest(self):
        """Test extracting extensions from filenames"""
        for f, e in (("hello.c", ".c"),
                     ("hello.cpp", ".cpp"),
                     ("hello.2.4.4.4.c", ".c"),
                     (".foo", ".foo"),
                     ("gcc", "(NULL)")):
            out, err = self.runcmd("h_exten '%s'" % f)
            assert out == e


class DaemonBadPort_Case(SimpleDistCC_Case):
    def runtest(self):
        """Test daemon invoked with invalid port number"""
        self.runcmd(self.distccd() +
                    "--log-file=distccd.log --lifetime=10 --port 80000 "
                    "--allow 127.0.0.1 --enable-tcp-insecure",
                    EXIT_BAD_ARGUMENTS)
        self.assert_no_file("daemonpid.tmp")


class InvalidHostSpec_Case(SimpleDistCC_Case):
    def runtest(self):
        """Test various invalid DISTCC_HOSTS

        See also test_parse_host_spec, which tests valid specifications."""
        for spec in ["", "    ", "\t", "  @ ", ":", "mbp@", "angry::", ":4200"]:
            self.runcmd(("DISTCC_HOSTS=\"%s\" " % spec) + self.valgrind()
                        + "h_hosts -v",
                        EXIT_BAD_HOSTSPEC)


class ParseHostSpec_Case(SimpleDistCC_Case):
    def runtest(self):
        """Check operation of dcc_parse_hosts_env.

        Passes complex environment variables to h_hosts, which is a C wrapper
        that calls the appropriate tests."""
        spec="""localhost 127.0.0.1 @angry   ted@angry
        \t@angry:/home/mbp/bin/distccd  angry:4204
        ipv4-localhost
        angry/44
        angry:300/44
        angry/44:300
        angry,lzo
        angry:3000,lzo    # some comment
        angry/44,lzo
        @angry,lzo#asdasd
        # oh yeah nothing here
        @angry:/usr/sbin/distccd,lzo
        localhostbutnotreally
        """

        expected="""16
   2 LOCAL
   4 TCP 127.0.0.1 3632
   4 SSH (no-user) angry (no-command)
   4 SSH ted angry (no-command)
   4 SSH (no-user) angry /home/mbp/bin/distccd
   4 TCP angry 4204
   4 TCP ipv4-localhost 3632
  44 TCP angry 3632
  44 TCP angry 300
  44 TCP angry 300
   4 TCP angry 3632
   4 TCP angry 3000
  44 TCP angry 3632
   4 SSH (no-user) angry (no-command)
   4 SSH (no-user) angry /usr/sbin/distccd
   4 TCP localhostbutnotreally 3632
"""
        out, err = self.runcmd(("DISTCC_HOSTS=\"%s\" " % spec) + self.valgrind()
                               + "h_hosts")
        assert out == expected, "expected %s\ngot %s" % (repr(expected), repr(out))


class Compilation_Case(WithDaemon_Case):
    '''Test distcc by actually compiling a file'''
    def setup(self):
        WithDaemon_Case.setup(self)
        self.createSource()

    def runtest(self):
        self.compile()
        self.link()
        self.checkBuiltProgram()

    def createSource(self):
        filename = self.sourceFilename()
        f = open(filename, 'w')
        f.write(self.source())
        f.close()
        filename = self.headerFilename()
        f = open(filename, 'w')
        f.write(self.headerSource())
        f.close()

    def sourceFilename(self):
        return "testtmp.c"              # default

    def headerFilename(self):
        return "testhdr.h"              # default

    def headerSource(self):
        return ""                       # default

    def compile(self):
        cmd = self.compileCmd()
        out, err = self.runcmd(cmd)
        if out != '':
            self.fail("compiler command %s produced output:\n%s" % (repr(cmd), out))
        if err != '':
            self.fail("compiler command %s produced error:\n%s" % (repr(cmd), err))

    def link(self):
        cmd = self.linkCmd()
        out, err = self.runcmd(cmd)
        if out != '':
            self.fail("command %s produced output:\n%s" % (repr(cmd), repr(out)))
        if err != '':
            self.fail("command %s produced error:\n%s" % (repr(cmd), repr(err)))

    def compileCmd(self):
        """Return command to compile source"""
        return self.distcc_without_fallback() + \
               self._cc + " -o testtmp.o " + self.compileOpts() + \
               " -c %s" % (self.sourceFilename())

    def compileOpts(self):
        """Returns any extra options to pass when compiling"""
        return ""

    def linkCmd(self):
        """Return command to link object files"""
        return self.distcc() + \
               self._cc + " -o testtmp testtmp.o " + self.libraries()

    def libraries(self):
        """Returns any '-l' options needed to link the program."""
        return ""

    def checkCompileMsgs(self, msgs):
        if len(msgs) > 0:
            self.fail("expected no compiler messages, got \"%s\"" % msgs)

    def checkBuiltProgram(self):
        '''Check compile/link results.  By default, just try to execute.'''
        msgs, errs = self.runcmd("./testtmp")
        self.checkBuiltProgramMsgs(msgs)
        self.assert_equal(errs, '')

    def checkBuiltProgramMsgs(self, msgs):
        pass


class CompileHello_Case(Compilation_Case):
    """Test the simple case of building a program that works properly"""

    def headerSource(self):
        return """
#define HELLO_WORLD "hello world"
"""

    def source(self):
        return """
#include <stdio.h>
#include "%s"
int main(void) {
    puts(HELLO_WORLD);
    return 0;
}
""" % self.headerFilename()

    def checkBuiltProgramMsgs(self, msgs):
        self.assert_equal(msgs, "hello world\n")


class CommaInFilename_Case(CompileHello_Case):

    def headerFilename(self):
      return 'foo1,2.h'


class ComputedInclude_Case(CompileHello_Case):

    def source(self):
        return """
#include <stdio.h>
#define MAKE_HEADER(header_name) STRINGIZE(header_name.h)
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define HEADER MAKE_HEADER(testhdr)
#include HEADER
int main(void) {
    puts(HELLO_WORLD);
    return 0;
}
"""

class BackslashInMacro_Case(ComputedInclude_Case):
    def source(self):
        return """
#include <stdio.h>
#if FALSE
  #define HEADER MAKE_HEADER(testhdr)
  #define MAKE_HEADER(header_name) STRINGIZE(foobar\)
  #define STRINGIZE(x) STRINGIZE2(x)
  #define STRINGIZE2(x) #x
#else
  #define HEADER "testhdr.h"
#endif
#include HEADER
int main(void) {
    puts(HELLO_WORLD);
    return 0;
}
"""

class BackslashInFilename_Case(ComputedInclude_Case):

    def headerFilename(self):
      # On Windows, this filename will be in a subdirectory.
      # On Unix, it will be a filename with an embedded backslash.
      try:
        os.mkdir("subdir")
      except:
        pass
      return 'subdir\\testhdr.h'

    def source(self):
        return """
#include <stdio.h>
#define HEADER MAKE_HEADER(testhdr)
#define MAKE_HEADER(header_name) STRINGIZE(subdir\header_name.h)
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#include HEADER
int main(void) {
    puts(HELLO_WORLD);
    return 0;
}
"""

class LanguageSpecific_Case(Compilation_Case):
    """Abstract base class to test building non-C programs."""
    def runtest(self):
        # Don't try to run the test if the language's compiler is not installed
        source = self.sourceFilename()
        lang = self.languageGccName()
        error_rc, _, _ = self.runcmd_unchecked(
            "touch " + source + "; " +
            "rm -f testtmp.o; " +
            self._cc + " -x " + lang + " " + self.compileOpts() +
                " -c " + source + " " + self.libraries() + " && " +
            "test -f testtmp.o" )
        if error_rc != 0:
            raise comfychair.NotRunError ('GNU ' + self.languageName() +
                                          ' not installed')
        else:
            Compilation_Case.runtest (self)

    def sourceFilename(self):
      return "testtmp" + self.extension()

    def languageGccName(self):
      """Language name suitable for use with 'gcc -x'"""
      raise NotImplementedError

    def languageName(self):
      """Human-readable language name."""
      raise NotImplementedError

    def extension(self):
      """Filename extension, with leading '.'."""
      raise NotImplementedError


class CPlusPlus_Case(LanguageSpecific_Case):
    """Test building a C++ program."""

    def languageName(self):
      return "C++"

    def languageGccName(self):
      return "c++"

    def extension(self):
      return ".cpp"  # Could also use ".cc", ".cxx", etc.

    def libraries(self):
      return "-lstdc++"

    def headerSource(self):
        return """
#define MESSAGE "hello c++"
"""

    def source(self):
        return """
#include <iostream>
#include "testhdr.h"

int main(void) {
    std::cout << MESSAGE << std::endl;
    return 0;
}
"""

    def checkBuiltProgramMsgs(self, msgs):
        self.assert_equal(msgs, "hello c++\n")


class ObjectiveC_Case(LanguageSpecific_Case):
    """Test building an Objective-C program."""

    def languageName(self):
      return "Objective-C"

    def languageGccName(self):
      return "objective-c"

    def extension(self):
      return ".m"

    def headerSource(self):
        return """
#define MESSAGE "hello objective-c"
"""

    def source(self):
        return """
#import <stdio.h>
#import "testhdr.h"

/* TODO: use objective-c features. */

int main(void) {
    puts(MESSAGE);
    return 0;
}
"""

class ObjectiveCPlusPlus_Case(LanguageSpecific_Case):
    """Test building an Objective-C++ program."""

    def languageName(self):
      return "Objective-C++"

    def languageGccName(self):
      return "objective-c++"

    def extension(self):
      return ".mm"

    def libraries(self):
      return "-lstdc++"

    def headerSource(self):
        return """
#define MESSAGE "hello objective-c++"
"""

    def source(self):
        return """
#import <iostream>
#import "testhdr.h"

/* TODO: use Objective-C features. */

int main(void) {
    std::cout << MESSAGE << std::endl;
    return 0;
}
"""

    def checkBuiltProgramMsgs(self, msgs):
        self.assert_equal(msgs, "hello objective-c++\n")


class SystemIncludeDirectories_Case(Compilation_Case):
    """Test -I/usr/include/sys"""

    def compileOpts(self):
        if os.path.exists("/usr/include/sys/types.h"):
          return "-I/usr/include/"
        else:
          raise comfychair.NotRunError (
              "This test requires /usr/include/sys/types.h")

    def headerSource(self):
        return """
#define HELLO_WORLD "hello world"
"""

    def source(self):
        return """
#include "sys/types.h"    /* Should resolve to /usr/include/sys/types.h. */
#include <stdio.h>
#include "testhdr.h"
int main(void) {
    uint val = 1u;
    puts(HELLO_WORLD);
    return val == 1 ? 0 : 1;
}
"""

    def checkBuiltProgramMsgs(self, msgs):
        self.assert_equal(msgs, "hello world\n")


class CPlusPlus_SystemIncludeDirectories_Case(CPlusPlus_Case):
    """Test -I/usr/include/sys for a C++ program"""

    def compileOpts(self):
        if os.path.exists("/usr/include/sys/types.h"):
          return "-I/usr/include/sys"
        else:
          raise comfychair.NotRunError (
              "This test requires /usr/include/sys/types.h")

    def headerSource(self):
        return """
#define MESSAGE "hello world"
"""

    def source(self):
        return """
#include "types.h"    /* Should resolve to /usr/include/sys/types.h. */
#include "testhdr.h"
#include <stdio.h>
int main(void) {
    puts(MESSAGE);
    return 0;
}
"""
    def checkBuiltProgramMsgs(self, msgs):
        self.assert_equal(msgs, "hello world\n")


class Gdb_Case(CompileHello_Case):
    """Test that distcc generates correct debugging information."""

    def sourceFilename(self):
        try:
          os.mkdir("src")
        except:
          pass
        return "src/testtmp.c"

    def compiler(self):
        """Command for compiling and linking."""
        return self._cc + " -g ";

    def compileCmd(self):
        """Return command to compile source"""
        os.mkdir("obj")
        return self.distcc_without_fallback() + self.compiler() + \
               " -o obj/testtmp.o -I. -c %s" % (self.sourceFilename())

    def link(self):
        """
        We do the linking in a subdirectory, so that the 'compilation
        directory' field of the debug info set by the link step (which
        will be done locally, not remotely) does NOT influence the
        behaviour of gdb.  We want gdb to use the 'compilation directory'
        value set by the compilation.
        """
        os.mkdir('link')
        cmd = (self.distcc() + self.compiler() + self.build_id +
               " -o link/testtmp obj/testtmp.o")
        out, err = self.runcmd(cmd)
        if out != '':
            self.fail("command %s produced output:\n%s" % (repr(cmd), repr(out)))
        if err != '':
            self.fail("command %s produced error:\n%s" % (repr(cmd), repr(err)))

    def runtest(self):
        # Don't try to run the test if gdb is not installed
        error_rc, _, _ = self.runcmd_unchecked("gdb --help")
        if error_rc != 0:
            raise comfychair.NotRunError ('gdb could not be found on path')

        # Test if the compiler supports --build-id=0xNNN.
        # If so, we need to use it for this test.
        # If not, try the alternative syntax -Wl,--build-id=0xNNN instead.
        self.build_id = " --build-id=0x12345678 "
        error_rc, _, _ = self.runcmd_unchecked(self.compiler() +
            (self.build_id + " -o junk -I. %s" % self.sourceFilename()))
        if error_rc != 0:
          self.build_id = " -Wl,--build-id=0x12345678 "
          error_rc, _, _ = self.runcmd_unchecked(self.compiler() +
              (self.build_id + " -o junk -I. %s" % self.sourceFilename()))
          if error_rc != 0:
            self.build_id = ""

        CompileHello_Case.runtest (self)

    def checkBuiltProgram(self):
        # On windows, the binary may be called testtmp.exe.  Check both
        if os.path.exists('link/testtmp.exe'):
            testtmp_exe = 'testtmp.exe'
        else:
            testtmp_exe = 'testtmp'

        # Run gdb and verify that it is able to correctly locate the
        # testtmp.c source file.  We write the gdb commands to a file
        # and run them via gdb --command.  (The alternative, to specify
        # the gdb commands directly on the commandline using gdb --ex,
        # is not as portable since only newer gdb's support it.)
        f = open('gdb_commands', 'w')
        f.write('break main\nrun\nnext\n')
        f.close()
        out, errs = self.runcmd("gdb -nh --batch --command=gdb_commands "
                                "link/%s </dev/null" % testtmp_exe)
        # Normally we expect the stderr output to be empty.
        # But, due to gdb bugs, some versions of gdb will produce a
        # (harmless) error or warning message.
        # In these cases, we can safely ignore the message.
        ignorable_error_messages = (
          'Failed to read a valid object file image from memory.\n',
          'warning: Lowest section in system-supplied DSO at 0xffffe000 is .hash at ffffe0b4\n',
          'warning: no loadable sections found in added symbol-file /usr/lib/debug/lib/ld-2.7.so\n',
          'warning: Could not load shared library symbols for linux-gate.so.1.\nDo you need "set solib-search-path" or "set sysroot"?\n',
        )
        if errs and errs not in ignorable_error_messages:
            self.assert_equal(errs, '')
        self.assert_re_search('puts\\(HELLO_WORLD\\);', out)
        self.assert_re_search('testtmp.c:[45]', out)

        # Now do the same, but in a subdirectory.  This tests that the
        # "compilation directory" field of the object file is set
        # correctly.
        # If we're in pump mode, this test should only be run on ELF
        # binaries, which are the only ones we rewrite at this time.
        # If we're not in pump mode, this test should only be run
        # if gcc's preprocessing output stores the pwd (this is true
        # for gcc 4.0, but false for gcc 3.3).
        os.mkdir('run')
        os.chdir('run')
        self.runcmd("cp ../link/%s ./%s" % (testtmp_exe, testtmp_exe))
        pump_mode = _server_options.find('cpp') != -1
        error_rc, _, _ = self.runcmd_unchecked(self.compiler() +
            " -g -E -I.. -c ../%s | grep `pwd` >/dev/null" %
            self.sourceFilename())
        gcc_preprocessing_preserves_pwd = (error_rc == 0);
        if ((pump_mode and _IsElf('./%s' % testtmp_exe))
          or ((not pump_mode) and gcc_preprocessing_preserves_pwd)):
            out, errs = self.runcmd("gdb -nh --batch --command=../gdb_commands "
                                    "./%s </dev/null" % testtmp_exe)
            if errs and errs not in ignorable_error_messages:
                self.assert_equal(errs, '')
            self.assert_re_search('puts\\(HELLO_WORLD\\);', out)
            self.assert_re_search('testtmp.c:[45]', out)
        os.chdir('..')

        # Now recompile and relink the executable using ordinary
        # gcc rather than distcc; strip both executables;
        # and check that the executable generated with ordinary
        # gcc is bit-for-bit identical to the executable that was
        # generated by distcc.  This is just to double-check
        # that we didn't modify anything other than the ".debug_info"
        # section.
        self.runcmd(self.compiler() + self.build_id + " -o obj/testtmp.o -I. -c %s" %
            self.sourceFilename())
        self.runcmd(self.compiler() + self.build_id + " -o link/testtmp obj/testtmp.o")
        self.runcmd("strip link/%s && strip run/%s" % (testtmp_exe, testtmp_exe))
        # On newer versions of Linux, this works only because we pass
        # --build-id=0x12345678.
        # On OS X, the strict bit-by-bit comparison will fail, because
        # mach-o format includes a unique UUID which will differ
        # between the two testtmp binaries.  For Microsoft PE output,
        # I've seen binaries differ in two places, though I don't know
        # why (timestamp?).  We do the best we can in those cases.
        if _IsMachO('link/%s' % testtmp_exe):
            # TODO(csilvers): we can do better (make sure all 16 bytes are
            # consecutive, or even parse Mach-O to remove the UUID first).
            acceptable_diffbytes = 16
        elif _IsPE('link/%s' % testtmp_exe):
            acceptable_diffbytes = 2
        else:
            acceptable_diffbytes = 0
        rc, msgs, errs = self.runcmd_unchecked("cmp -l link/%s run/%s"
                                               % (testtmp_exe, testtmp_exe))
        if (rc != 0 and
            (errs or len(msgs.strip().splitlines()) > acceptable_diffbytes)):
            # Just do the cmp again to give a good error message
            self.runcmd("cmp link/%s run/%s" % (testtmp_exe, testtmp_exe))

class GdbOpt1_Case(Gdb_Case):
    def compiler(self):
        """Command for compiling and linking."""
        return self._cc + " -g -O1 ";

class GdbOpt2_Case(Gdb_Case):
    def compiler(self):
        """Command for compiling and linking."""
        return self._cc + " -g -O2 ";

class GdbOpt3_Case(Gdb_Case):
    def compiler(self):
        """Command for compiling and linking."""
        return self._cc + " -g -O3 ";

class CompressedCompile_Case(CompileHello_Case):
    """Test compilation with compression.

    The source needs to be moderately large to make sure compression and mmap
    is turned on."""

    def source(self):
        return """
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testhdr.h"
int main(void) {
    printf("%s\\n", HELLO_WORLD);
    return 0;
}
"""

    def setupEnv(self):
        Compilation_Case.setupEnv(self)
        os.environ['DISTCC_HOSTS'] = '127.0.0.1:%d,lzo' % self.server_port

class DashONoSpace_Case(CompileHello_Case):
    def compileCmd(self):
        return self.distcc_without_fallback() + \
               self._cc + " -otesttmp.o -c %s" % (self.sourceFilename())

    def runtest(self):
        if sys.platform == 'sunos5':
            raise comfychair.NotRunError ('Sun assembler wants space after -o')
        elif sys.platform.startswith ('osf1'):
            raise comfychair.NotRunError ('GCC mips-tfile wants space after -o')
        else:
            CompileHello_Case.runtest (self)


class WriteDevNull_Case(CompileHello_Case):
    def runtest(self):
        self.compile()

    def compileCmd(self):
        return self.distcc_without_fallback() + self._cc + \
               " -c -o /dev/null -c %s" % (self.sourceFilename())


class MultipleCompile_Case(Compilation_Case):
    """Test compiling several files from one line"""
    def setup(self):
        WithDaemon_Case.setup(self)
        open("test1.c", "w").write("const char *msg = \"hello foreigner\";")
        open("test2.c", "w").write("""#include <stdio.h>

int main(void) {
   extern const char *msg;
   puts(msg);
   return 0;
}
""")

    def runtest(self):
        self.runcmd(self.distcc()
                    + self._cc + " -c test1.c test2.c")
        self.runcmd(self.distcc()
                    + self._cc + " -o test test1.o test2.o")



class CppError_Case(CompileHello_Case):
    """Test failure of cpp"""
    def source(self):
        return '#error "not tonight dear"\n'

    def runtest(self):
        cmd = self.distcc() + self._cc + " -c testtmp.c"
        msgs, errs = self.runcmd(cmd, expectedResult=1)
        self.assert_re_search("not tonight dear", errs)
        self.assert_equal(msgs, '')


class BadInclude_Case(Compilation_Case):
    """Handling of error running cpp"""
    def source(self):
        return """#include <nosuchfilehere.h>
"""

    def runtest(self):
        if _server_options.find('cpp') != -1:
            # Annoyingly, different versions of gcc are inconsistent
            # in how they treat a non-existent #include file when
            # invoked with "-MMD": some versions treat it as an error
            # (rc 1), some as a warning (rc 0).  When distcc is
            # responsible for preprocessing (_server_options includes
            # 'cpp'), we need to figure out which our gcc does, in
            # order to verify distcc is doing the same thing.
            # FIXME(klarlund): this is arguably a bug in gcc, and it
            # is exacerbated by distcc's pump mode because we always
            # pass -MMD, even when the user didn't.  TODO(klarlund):
            # change error_rc back to 1 once that FIXME is fixed.
            error_rc, _, _ = self.runcmd_unchecked(self._cc + " -MMD -E testtmp.c")
        else:
            error_rc = 1
        self.runcmd(self.distcc() + self._cc + " -o testtmp.o -c testtmp.c",
                    error_rc)


class PreprocessPlainText_Case(Compilation_Case):
    """Try using cpp on something that's not C at all"""
    def setup(self):
        self.stripEnvironment()
        self.createSource()
        self.initCompiler()

    def source(self):
        return """#define FOO 3
#if FOO < 10
small foo!
#else
large foo!
#endif
/* comment ca? */
"""

    def runtest(self):
        # -P means not to emit linemarkers
        self.runcmd(self.distcc()
                    + self._cc + " -E testtmp.c -o testtmp.out")
        out = open("testtmp.out").read()
        # It's a bit hard to know the exact value, because different versions of
        # GNU cpp seem to handle the whitespace differently.
        self.assert_re_search("small foo!", out)

    def teardown(self):
        # no daemon is run for this test
        pass


class NoDetachDaemon_Case(CompileHello_Case):
    """Test the --no-detach option."""
    def startDaemon(self):
        # FIXME: This  does not work well if it happens to get the same
        # port as an existing server, because we can't catch the error.
        cmd = (self.distccd() +
               "--no-detach --daemon --verbose --log-file %s --pid-file %s "
               "--port %d --allow 127.0.0.1 --enable-tcp-insecure" %
               (_ShellSafe(self.daemon_logfile),
                _ShellSafe(self.daemon_pidfile),
                self.server_port))
        self.pid = self.runcmd_background(cmd)
        self.add_cleanup(self.killDaemon)
        # Wait until the server is ready for connections.
        time.sleep(0.2)   # Give distccd chance to start listening on the port
        sock = socket.socket()
        while sock.connect_ex(('127.0.0.1', self.server_port)) != 0:
            time.sleep(0.2)

    def killDaemon(self):
        os.kill(self.pid, signal.SIGTERM)
        pid, ret = os.wait()
        self.assert_equal(self.pid, pid)


class ImplicitCompiler_Case(CompileHello_Case):
    """Test giving no compiler works"""
    def compileCmd(self):
        return self.distcc() + "-c testtmp.c"

    def linkCmd(self):
        # FIXME: Mozilla uses something like "distcc testtmp.o -o testtmp",
        # but that's broken at the moment.
        return self.distcc() + "-o testtmp testtmp.o "

    def runtest(self):
        if sys.platform == 'hp-ux10':
            raise comfychair.NotRunError ('HP-UX bundled C compiler non-ANSI')
        # We can't run if cc is not installed on the system (maybe only gcc is)
        error_rc, _, _ = self.runcmd_unchecked("cc -c testtmp.c")
        self.runcmd_unchecked("rm -f testtmp.o")   # clean up the 'cc' output
        if error_rc != 0:
            raise comfychair.NotRunError ('Cannot find working "cc"')
        else:
            CompileHello_Case.runtest (self)


class DashD_Case(Compilation_Case):
    """Test preprocessor arguments"""
    def source(self):
        return """
#include <stdio.h>

int main(void) {
    printf("%s\\n", MESSAGE);
    return 0;
}
"""

    def compileOpts(self):
        # quoting is hairy because this goes through the shell
        return "'-DMESSAGE=\"hello DashD\"'"

    def checkBuiltProgramMsgs(self, msgs):
        self.assert_equal(msgs, "hello DashD\n")


class DashMD_DashMF_DashMT_Case(CompileHello_Case):
    """Test -MD -MFfoo -MTbar"""

    def compileOpts(self):
        return "-MD -MFdotd_filename -MTtarget_name_42"

    def runtest(self):
        try:
          os.remove('dotd_filename')
        except OSError:
          pass
        self.compile();
        dotd_contents = open("dotd_filename").read()
        self.assert_re_search("target_name_42", dotd_contents)


class DashWpMD_Case(CompileHello_Case):
    """Test -Wp,-MD,depfile"""

    def compileOpts(self):
        return "-Wp,-MD,depsfile"

    def runtest(self):
        try:
          os.remove('depsfile')
        except OSError:
          pass
        self.compile()
        deps = open('depsfile').read()
        self.assert_re_search(r"testhdr\.h", deps)
        self.assert_re_search(r"stdio\.h", deps)

class ScanIncludes_Case(CompileHello_Case):
    """Test --scan-includes"""

    def createSource(self):
      CompileHello_Case.createSource(self)
      self.runcmd("mv testhdr.h test_header.h")
      self.runcmd("ln -s test_header.h testhdr.h")
      self.runcmd("mkdir test_subdir")
      self.runcmd("touch test_another_header.h")

    def headerSource(self):
        return """
#define HELLO_WORLD "hello world"
#include "test_subdir/../test_another_header.h"
"""

    def compileCmd(self):
        return self.distcc_without_fallback() + "--scan-includes " + \
               self._cc + " -o testtmp.o " + self.compileOpts() + \
               " -c %s" % (self.sourceFilename())

    def runtest(self):
        cmd = self.compileCmd()
        rc, out, err = self.runcmd_unchecked(cmd)
        log = open('distcc.log').read()
        pump_mode = _server_options.find('cpp') != -1
        if pump_mode:
          if err != '':
              self.fail("distcc command %s produced stderr:\n%s" % (repr(cmd), err))
          if rc != 0:
              self.fail("distcc command %s failed:\n%s" % (repr(cmd), rc))
          self.assert_re_search(
              r"FILE      /.*/ScanIncludes_Case/testtmp.c", out);
          self.assert_re_search(
              r"FILE      /.*/ScanIncludes_Case/test_header\.h", out);
          self.assert_re_search(
              r"FILE      /.*/ScanIncludes_Case/test_another_header\.h", out);
          self.assert_re_search(
              r"SYMLINK   /.*/ScanIncludes_Case/testhdr\.h", out);
          self.assert_re_search(
              r"DIRECTORY /.*/ScanIncludes_Case/test_subdir", out);
          self.assert_re_search(
              r"SYSTEMDIR /.*", out);
        else:
          self.assert_re_search(r"ERROR: '--scan_includes' specified, but "
                                "distcc wouldn't have used include server "
                                ".make sure hosts list includes ',cpp' option",
                                log)
          self.assert_equal(rc, 100)
          self.assert_equal(out, '')
          self.assert_equal(err, '')

class AbsSourceFilename_Case(CompileHello_Case):
    """Test remote compilation of files with absolute names."""

    def compileCmd(self):
        return (self.distcc()
                + self._cc
                + " -c -o testtmp.o %s/testtmp.c"
                % _ShellSafe(os.getcwd()))


class HundredFold_Case(CompileHello_Case):
    """Try repeated simple compilations.

    This used to be a ThousandFold_Case -- but that slowed down testing
    significantly.  It's unclear that testing a 1000 times is much better than
    doing it a 100 times.
    """

    def daemon_lifetime(self):
        return 120

    def runtest(self):
        for unused_i in range(100):
            self.runcmd(self.distcc()
                        + self._cc + " -o testtmp.o -c testtmp.c")


class Concurrent_Case(CompileHello_Case):
    """Try many compilations at the same time"""
    def daemon_lifetime(self):
        return 120

    def runtest(self):
        # may take about a minute or so
        pids = {}
        for unused_i in range(50):
            kid = self.runcmd_background(self.distcc() +
                                         self._cc + " -o testtmp.o -c testtmp.c")
            pids[kid] = kid
        while len(pids):
            pid, status = os.wait()
            if status:
                self.fail("child %d failed with status %#x" % (pid, status))
            del pids[pid]


class BigAssFile_Case(Compilation_Case):
    """Test compilation of a really big C file

    This will take a while to run"""
    def createSource(self):
        """Create source file"""
        f = open("testtmp.c", 'wt')

        # We want a file of many, which will be a few megabytes of
        # source.  Picking the size is kind of hard -- something that
        # will properly exercise distcc may be too big for small/old
        # machines.

        f.write("int main() {}\n")
        for i in range(200000):
            f.write("int i%06d = %d;\n" % (i, i))
        f.close()

    def runtest(self):
        self.runcmd(self.distcc() + self._cc + " -c %s" % "testtmp.c")
        self.runcmd(self.distcc() + self._cc + " -o testtmp testtmp.o")


    def daemon_lifetime(self):
        return 300



class BinFalse_Case(Compilation_Case):
    """Compiler that fails without reading input.

    This is an interesting case when the server is using fifos,
    because it has to cope with the open() on the fifo being
    interrupted.

    distcc doesn't know that 'false' is not a compiler, but it does
    need a command line that looks like a compiler invocation.

    We have to use a .i file so that distcc does not try to preprocess it.
    """
    def createSource(self):
        open("testtmp.i", "wt").write("int main() {}")

    def runtest(self):
        # On Solaris and IRIX 6, 'false' returns exit status 255
        if sys.platform == 'sunos5' or \
        sys.platform.startswith ('irix6'):
            self.runcmd(self.distcc()
                        + "false -c testtmp.i", 255)
        else:
            self.runcmd(self.distcc()
                        + "false -c testtmp.i", 1)


class BinTrue_Case(Compilation_Case):
    """Compiler that succeeds without reading input.

    This is an interesting case when the server is using fifos,
    because it has to cope with the open() on the fifo being
    interrupted.

    distcc doesn't know that 'true' is not a compiler, but it does
    need a command line that looks like a compiler invocation.

    We have to use a .i file so that distcc does not try to preprocess it.
    """
    def createSource(self):
        open("testtmp.i", "wt").write("int main() {}")

    def runtest(self):
        self.runcmd(self.distcc()
                    + "true -c testtmp.i", 0)


class SBeatsC_Case(CompileHello_Case):
    """-S overrides -c in gcc.

    If both options are given, we have to make sure we imply the
    output filename in the same way as gcc."""
    # XXX: Are other compilers the same?
    def runtest(self):
        self.runcmd(self.distcc() +
                    self._cc + " -c -S testtmp.c")
        if os.path.exists("testtmp.o"):
            self.fail("created testtmp.o but should not have")
        if not os.path.exists("testtmp.s"):
            self.fail("did not create testtmp.s but should have")


class NoServer_Case(CompileHello_Case):
    """Invalid server name"""
    def setup(self):
        self.stripEnvironment()
        os.environ['DISTCC_HOSTS'] = 'no.such.host.here'
        self.distcc_log = 'distcc.log'
        os.environ['DISTCC_LOG'] = self.distcc_log
        self.createSource()
        self.initCompiler()

    def runtest(self):
        self.runcmd(self.distcc()
                    + self._cc + " -c -o testtmp.o testtmp.c")
        msgs = open(self.distcc_log, 'r').read()
        self.assert_re_search(r'failed to distribute.*running locally instead',
                              msgs)


class ImpliedOutput_Case(CompileHello_Case):
    """Test handling absence of -o"""
    def compileCmd(self):
        return self.distcc() + self._cc + " -c testtmp.c"


class SyntaxError_Case(Compilation_Case):
    """Test building a program containing syntax errors, so it won't build
    properly."""
    def source(self):
        return """not C source at all
"""

    def compile(self):
        rc, msgs, errs = self.runcmd_unchecked(self.compileCmd())
        self.assert_notequal(rc, 0)
        self.assert_re_search(r'testtmp.c:1:.*error', errs)
        self.assert_equal(msgs, '')

    def runtest(self):
        self.compile()

        if os.path.exists("testtmp") or os.path.exists("testtmp.o"):
            self.fail("compiler produced output, but should not have done so")


class NoHosts_Case(CompileHello_Case):
    """Test running with no hosts defined.

    We expect compilation to succeed, but with a warning that it was
    run locally."""
    def runtest(self):
        # WithDaemon_Case sets this to point to the local host, but we
        # don't want that.  Note that you cannot delete environment
        # keys in Python1.5, so we need to just set them to the empty
        # string.
        os.environ['DISTCC_HOSTS'] = ''
        os.environ['DISTCC_LOG'] = ''
        self.runcmd('env')
        msgs, errs = self.runcmd(self.compileCmd())

        # We expect only one message, a warning from distcc
        self.assert_re_search(r"Warning.*\$DISTCC_HOSTS.*can't distribute work",
                              errs)

    def compileCmd(self):
        """Return command to compile source and run tests"""
        return self.distcc_with_fallback() + \
               self._cc + " -o testtmp.o -c %s" % (self.sourceFilename())



class MissingCompiler_Case(CompileHello_Case):
    """Test compiler missing from server."""
    # Another way to test this would be to break the server's PATH
    def sourceFilename(self):
        # must be preprocessed, so that we don't need to run the compiler
        # on the client
        return "testtmp.i"

    def source(self):
        return """int foo;"""

    def runtest(self):
        msgs, errs = self.runcmd(self.distcc_without_fallback()
                                 + "nosuchcc -c testtmp.i",
                                 expectedResult=EXIT_COMPILER_MISSING)
        self.assert_re_search(r'failed to exec', errs)



class RemoteAssemble_Case(WithDaemon_Case):
    """Test remote assembly of a .s file."""

    # We have a rather tricky method for testing assembly code when we
    # don't know what platform we're on.  I think this one will work
    # everywhere, though perhaps not.
    # We don't use @ because that starts comments for ARM.
    asm_source = """
        .file	"foo.c"
.globl msg
.section	.rodata
.LC0:
  .string	"hello world"
.data
  .align 4
  .type	 msg,object
  .size	 msg,4
msg:
  .long .LC0
"""

    asm_filename = 'test2.s'

    def setup(self):
        WithDaemon_Case.setup(self)
        open(self.asm_filename, 'wt').write(self.asm_source)

    def compile(self):
        # Need to build both the C file and the assembly file
        self.runcmd(self.distcc() + self._cc + " -o test2.o -c test2.s")



class PreprocessAsm_Case(WithDaemon_Case):
    """Run preprocessor locally on assembly, then compile locally."""
    asm_source = """
#define MSG "hello world"
gcc2_compiled.:
.globl msg
.section	.rodata
.LC0:
  .string	 MSG
.data
  .align 4
  .type	 msg,object
  .size	 msg,4
msg:
  .long .LC0
"""

    def setup(self):
        WithDaemon_Case.setup(self)
        open('test2.S', 'wt').write(self.asm_source)

    def compile(self):
        if sys.platform == 'linux2':
            self.runcmd(self.distcc()
                        + "-o test2.o -c test2.S")
        else:
            raise comfychair.NotRunError ('this test is system-specific')

    def runtest(self):
        self.compile()




class ModeBits_Case(CompileHello_Case):
    """Check distcc obeys umask"""
    def runtest(self):
        self.runcmd("umask 0; distcc " + self._cc + " -c testtmp.c")
        self.assert_equal(S_IMODE(os.stat("testtmp.o")[ST_MODE]), 0o666)


class CheckRoot_Case(SimpleDistCC_Case):
    """Stub case that checks this is run by root.  Not used by default."""
    def setup(self):
        self.require_root()


class EmptySource_Case(Compilation_Case):
    """Check compilation of empty source file

    It must be treated as preprocessed source, otherwise cpp will
    insert a # line, which will give a false pass.

    This test fails with an internal compiler error in GCC 3.4.x for x < 5
    (see http://gcc.gnu.org/bugzilla/show_bug.cgi?id=20239
    [3.4 Regression] ICE on empty preprocessed input).
    But that's gcc's problem, not ours, so we make this test pass
    if gcc gets an ICE."""

    def source(self):
        return ''

    def runtest(self):
        self.compile()

    def compile(self):
        rc, out, errs = self.runcmd_unchecked(self.distcc()
                    + self._cc + " -c %s" % self.sourceFilename())
        if not re.search("internal compiler error", errs):
          self.assert_equal(rc, 0)

    def sourceFilename(self):
        return "testtmp.i"

class BadLogFile_Case(CompileHello_Case):
    def runtest(self):
        self.runcmd("touch distcc.log")
        self.runcmd("chmod 0 distcc.log")
        msgs, errs = self.runcmd("DISTCC_LOG=distcc.log " + \
                                 self.distcc() + \
                                 self._cc + " -c testtmp.c", expectedResult=0)
        self.assert_re_search("failed to open logfile", errs)


class AccessDenied_Case(CompileHello_Case):
    """Run the daemon, but don't allow access from this host.

    Make sure that compilation falls back to localhost with a warning."""
    def daemon_command(self):
        return (self.distccd()
                + "--verbose --lifetime=%d --daemon --log-file %s "
                  "--pid-file %s --port %d --allow 127.0.0.2 --enable-tcp-insecure"
                % (self.daemon_lifetime(),
                   _ShellSafe(self.daemon_logfile),
                   _ShellSafe(self.daemon_pidfile),
                   self.server_port))

    def compileCmd(self):
        """Return command to compile source and run tests"""
        return self.distcc_with_fallback() + \
               self._cc + " -o testtmp.o -c %s" % (self.sourceFilename())


    def runtest(self):
        self.compile()
        errs = open('distcc.log').read()
        self.assert_re_search(r'failed to distribute', errs)


class ParseMask_Case(comfychair.TestCase):
    """Test code for matching IP masks."""
    values = [
        ('127.0.0.1', '127.0.0.1', 0),
        ('127.0.0.1', '127.0.0.0', EXIT_ACCESS_DENIED),
        ('127.0.0.1', '127.0.0.2', EXIT_ACCESS_DENIED),
        ('127.0.0.1/8', '127.0.0.2', 0),
        ('10.113.0.0/16', '10.113.45.67', 0),
        ('10.113.0.0/16', '10.11.45.67', EXIT_ACCESS_DENIED),
        ('10.113.0.0/16', '127.0.0.1', EXIT_ACCESS_DENIED),
        ('1.2.3.4/0', '4.3.2.1', 0),
        ('1.2.3.4/40', '4.3.2.1', EXIT_BAD_ARGUMENTS),
        ('1.2.3.4.5.6.7/8', '127.0.0.1', EXIT_BAD_ARGUMENTS),
        ('1.2.3.4/8', '4.3.2.1', EXIT_ACCESS_DENIED),
        ('192.168.1.64/28', '192.168.1.70', 0),
        ('192.168.1.64/28', '192.168.1.7', EXIT_ACCESS_DENIED),
        ]
    def runtest(self):
        for mask, client, expected in ParseMask_Case.values:
            cmd = "h_parsemask %s %s" % (mask, client)
            ret, msgs, err = self.runcmd_unchecked(cmd)
            if ret != expected:
                self.fail("%s gave %d, expected %d" % (cmd, ret, expected))


class HostFile_Case(CompileHello_Case):
    def setup(self):
        CompileHello_Case.setup(self)
        del os.environ['DISTCC_HOSTS']
        self.save_home = os.environ['HOME']
        os.environ['HOME'] = os.getcwd()
        # DISTCC_DIR is set to 'distccdir'
        open(os.environ['DISTCC_DIR'] + '/hosts', 'w').write('127.0.0.1:%d%s' %
            (self.server_port, _server_options))

    def teardown(self):
        os.environ['HOME'] = self.save_home
        CompileHello_Case.teardown(self)


class Lsdistcc_Case(WithDaemon_Case):
    """Check lsdistcc"""

    def lsdistccCmd(self):
        """Return command to run lsdistcc"""
        return "lsdistcc -r%d" % self.server_port

    def runtest(self):
        lsdistcc = self.lsdistccCmd()

        # Test "lsdistcc --help" output is reasonable.
        # (Note: "lsdistcc --help" ought to return exit status 0, really,
        # but currently it returns 1, so that's what we test for.)
        rc, out, err = self.runcmd_unchecked(lsdistcc + " --help")
        self.assert_re_search("Usage:", out)
        self.assert_equal(err, "")
        self.assert_equal(rc, 1)

        # On some systems, 127.0.0.* are all loopback addresses.
        # On other systems, only 127.0.0.1 is a loopback address.
        # The lsdistcc test is more effective if we can use 127.0.0.2 etc.
        # but that only works on some systems, so we need to check whether
        # if will work.  The ping command is not very portable, but that
        # doesn't matter; if it fails, we just won't test quite as much as
        # we would if it succeeds.  So long as it succeeds on Linux, we'll
        # get good enough test coverage.
        rc, out, err = self.runcmd_unchecked("ping -c 3 -i 0.2 -w 1 127.0.0.2")
        multiple_loopback_addrs = (rc == 0)

        # Test "lsdistcc host1 host2 host3".
        out, err = self.runcmd(lsdistcc + " localhost 127.0.0.1 127.0.0.2 "
            + " anInvalidHostname")
        out_list = out.split()
        out_list.sort()
        expected = ["%s:%d" % (host, self.server_port) for host in
                    ["127.0.0.1", "127.0.0.2", "localhost"]]
        if multiple_loopback_addrs:
          self.assert_equal(out_list, expected)
        else:
            # It may be that 127.0.0.2 isn't a loopback address, or it
            # may be that it is, but ping doesn't support -c or -i or
            # -w.  So be happy if 127.0.0.2 is there, or if it's not.
            if out_list != expected:
                del expected[1] # remove 127.0.0.2
                self.assert_equal(out_list, expected)
        self.assert_equal(err, "")

        # Test "lsdistcc host%d".
        out, err = self.runcmd(lsdistcc + " 127.0.0.%d")
        self.assert_equal(err, "")
        self.assert_re_search("127.0.0.1:%d\n" % self.server_port, out)
        if multiple_loopback_addrs:
          self.assert_re_search("127.0.0.2:%d\n" % self.server_port, out)
          self.assert_re_search("127.0.0.3:%d\n" % self.server_port, out)
          self.assert_re_search("127.0.0.4:%d\n" % self.server_port, out)
          self.assert_re_search("127.0.0.5:%d\n" % self.server_port, out)

class Getline_Case(comfychair.TestCase):
    """Test getline()."""
    values = [
        # Input, Line, Rest, Retval
        ('', '', '', -1),
        ('\n', '\n', '', 1),
        ('\n\n', '\n', '\n', 1),
        ('\n\n\n', '\n', '\n\n', 1),
        ('a', 'a', '', 1),
        ('a\n', 'a\n', '', 2),
        ('foo', 'foo', '', 3),
        ('foo\n', 'foo\n', '', 4),
        ('foo\nbar\n', 'foo\n', 'bar\n', 4),
        ('foobar\nbaz', 'foobar\n', 'baz', 7),
        ('foo bar\nbaz', 'foo bar\n', 'baz', 8),
        ]
    def runtest(self):
        for input, line, rest, retval in Getline_Case.values:
            for bufsize in [None, 0, 1, 2, 3, 4, 64, 10000]:
                if bufsize:
                    cmd = "printf '%s' | h_getline %s | cat -v" % (input,
                                                                   bufsize)
                    n = bufsize
                else:
                    cmd = "printf '%s' | h_getline | cat -v" % input
                    n = 0
                ret, msgs, err = self.runcmd_unchecked(cmd)
                self.assert_equal(ret, 0);
                self.assert_equal(err, '');
                msg_parts = msgs.split(',');
                self.assert_equal(msg_parts[0], "original n = %s" % n);
                self.assert_equal(msg_parts[1], " returned %s" % retval);
                self.assert_equal(msg_parts[2].startswith(" n = "), True);
                self.assert_equal(msg_parts[3], " line = '%s'" % line);
                self.assert_equal(msg_parts[4], " rest = '%s'\n" % rest);

# All the tests defined in this suite
tests = [
         CompileHello_Case,
         CommaInFilename_Case,
         ComputedInclude_Case,
         BackslashInMacro_Case,
         BackslashInFilename_Case,
         CPlusPlus_Case,
         ObjectiveC_Case,
         ObjectiveCPlusPlus_Case,
         SystemIncludeDirectories_Case,
         CPlusPlus_SystemIncludeDirectories_Case,
         Gdb_Case,
         GdbOpt1_Case,
         GdbOpt2_Case,
         GdbOpt3_Case,
         Lsdistcc_Case,
         BadLogFile_Case,
         ScanArgs_Case,
         ParseMask_Case,
         DotD_Case,
         DashMD_DashMF_DashMT_Case,
         Compile_c_Case,
         ImplicitCompilerScan_Case,
         StripArgs_Case,
         StartStopDaemon_Case,
         CompressedCompile_Case,
         DashONoSpace_Case,
         WriteDevNull_Case,
         CppError_Case,
         BadInclude_Case,
         PreprocessPlainText_Case,
         NoDetachDaemon_Case,
         SBeatsC_Case,
         DashD_Case,
         DashWpMD_Case,
         BinFalse_Case,
         BinTrue_Case,
         VersionOption_Case,
         HelpOption_Case,
         BogusOption_Case,
         MultipleCompile_Case,
         CompilerOptionsPassed_Case,
         IsSource_Case,
         ExtractExtension_Case,
         ImplicitCompiler_Case,
         DaemonBadPort_Case,
         AccessDenied_Case,
         NoServer_Case,
         InvalidHostSpec_Case,
         ParseHostSpec_Case,
         ImpliedOutput_Case,
         SyntaxError_Case,
         NoHosts_Case,
         MissingCompiler_Case,
         RemoteAssemble_Case,
         PreprocessAsm_Case,
         ModeBits_Case,
         EmptySource_Case,
         HostFile_Case,
         AbsSourceFilename_Case,
         Getline_Case,
         # slow tests below here
         Concurrent_Case,
         HundredFold_Case,
         BigAssFile_Case]


if __name__ == '__main__':
  while len(sys.argv) > 1 and sys.argv[1].startswith("--"):
    if sys.argv[1] == "--valgrind":
      _valgrind_command = "valgrind --quiet "
      del sys.argv[1]
    elif sys.argv[1].startswith("--valgrind="):
      _valgrind_command = sys.argv[1][len("--valgrind="):] + " "
      del sys.argv[1]
    elif sys.argv[1] == "--lzo":
      _server_options = ",lzo"
      del sys.argv[1]
    elif sys.argv[1] == "--pump":
      _server_options = ",lzo,cpp"
      del sys.argv[1]

  # Some of these tests need lots of file descriptors (especially to fork),
  # but sometimes the os only supplies a few.  Try to raise that if we can.
  try:
      import resource
      (_, hard_limit) = resource.getrlimit(resource.RLIMIT_NOFILE)
      resource.setrlimit(resource.RLIMIT_NOFILE, (hard_limit, hard_limit))
  except (ImportError, ValueError):
      pass

  comfychair.main(tests)
