#! /usr/bin/env python3

# Copyright 2026 distcc contributors
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.

"""Regression test for installed pump include_server path resolution."""

import os
import subprocess
import sys
import tempfile
import textwrap


def WriteExecutable(path, contents):
    with open(path, "w", encoding="utf-8") as output:
        output.write(contents)
    os.chmod(path, 0o755)


def ReplaceLine(contents, prefix, replacement):
    lines = []
    replaced = False
    for line in contents.splitlines():
        if line.startswith(prefix):
            lines.append(replacement)
            replaced = True
        else:
            lines.append(line)
    if not replaced:
        raise AssertionError("Could not replace line starting with %r" % prefix)
    return "\n".join(lines) + "\n"


def MakeFakeIncludeServer(path):
    WriteExecutable(path, textwrap.dedent("""\
        #! /usr/bin/env python3
        import argparse
        import os
        import signal
        import socket
        import time

        parser = argparse.ArgumentParser()
        parser.add_argument("--port", required=True)
        parser.add_argument("--pid_file", required=True)
        parser.add_argument("-d1", action="store_true")
        args, _ = parser.parse_known_args()

        child_pid = os.fork()
        if child_pid:
            for _ in range(100):
                if os.path.exists(args.port) and os.path.exists(args.pid_file):
                    raise SystemExit(0)
                time.sleep(0.01)
            raise SystemExit(1)

        server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server_socket.bind(args.port)
        server_socket.listen(1)

        def handle_signal(_signum, _frame):
            server_socket.close()
            raise SystemExit(0)

        signal.signal(signal.SIGTERM, handle_signal)

        with open(args.pid_file, "w", encoding="utf-8") as pid_file:
            pid_file.write(str(os.getpid()))

        while True:
            time.sleep(1)
        """))


def Main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pump_include_server_path_test.py PUMP PYTHON")

    pump_template = sys.argv[1]
    python = sys.argv[2]
    version = subprocess.check_output(
        [python, "-c", "import sys; print('%d.%d' % sys.version_info[:2])"],
        text=True).strip()

    with tempfile.TemporaryDirectory(prefix="distcc-pump-path-test.") as tempdir:
        bindir = os.path.join(tempdir, "bin")
        prefix = os.path.join(tempdir, "prefix")
        include_server_dir = os.path.join(
            prefix, "lib", "python%s" % version, "dist-packages",
            "include_server")
        os.makedirs(bindir)
        os.makedirs(include_server_dir)

        with open(pump_template, encoding="utf-8") as input_file:
            pump_contents = input_file.read()
        pump_contents = ReplaceLine(
            pump_contents, "prefix=", "prefix=%s" % prefix)
        pump_contents = ReplaceLine(
            pump_contents, "include_server=",
            "include_server='/missing/include_server.py'")
        WriteExecutable(os.path.join(bindir, "pump"), pump_contents)

        WriteExecutable(os.path.join(bindir, "distcc"), textwrap.dedent("""\
            #! /bin/sh
            if [ "$1" = "--show-hosts" ]; then
              echo localhost,lzo,cpp
              exit 0
            fi
            exit 0
            """))

        MakeFakeIncludeServer(
            os.path.join(include_server_dir, "include_server.py"))

        env = os.environ.copy()
        env["DISTCC_LOCATION"] = bindir
        env["DISTCC_HOSTS"] = "localhost,lzo,cpp"

        result = subprocess.run(
            [os.path.join(bindir, "pump"), "echo", "ok"],
            env=env, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False)

        if result.returncode != 0:
            raise AssertionError(
                "pump failed with status %d\nstdout:\n%s\nstderr:\n%s" %
                (result.returncode, result.stdout, result.stderr))
        if "ok\n" not in result.stdout:
            raise AssertionError("pump did not run the wrapped command")


if __name__ == "__main__":
    Main()
