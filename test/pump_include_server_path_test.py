#! /usr/bin/env python3

# Copyright 2026 distcc contributors
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.

"""Regression test for installed pump include_server path resolution."""

import os
import shutil
import subprocess
import sys
import tempfile
import textwrap


def BuildFakeIncludeServerPathCandidates(python, version, *search_roots):
    script = """
import sys
import sysconfig

version = sys.argv[1]
search_roots = sys.argv[2:]
python_version = 'python' + version
marker = '/%s/' % python_version

for search_root in search_roots:
  if not search_root:
    continue
  for path_type in ('purelib', 'platlib'):
    lib_dir = sysconfig.get_path(
        path_type, vars={'base': search_root, 'platbase': search_root,
                         'data': search_root})
    if not lib_dir:
      continue
    print('%s/include_server/include_server.py' % lib_dir)
    marker_pos = lib_dir.find(marker)
    if marker_pos == -1:
      continue
    python_root = lib_dir[:marker_pos]
    remainder = lib_dir[marker_pos + len(marker):]
    if remainder:
      print('%s/distcc-pump/%s/%s/include_server/include_server.py' %
            (python_root.rstrip('/'), python_version, remainder))
    else:
      print('%s/distcc-pump/%s/include_server/include_server.py' %
            (python_root.rstrip('/'), python_version))
"""
    output = subprocess.check_output(
        [python, "-c", script, version] + list(search_roots),
        universal_newlines=True)
    candidates = []
    for path in output.splitlines():
        if path and path not in candidates:
            candidates.append(path)
    return candidates


def WriteExecutable(path, contents):
    with open(path, "w", encoding="utf-8") as output:
        output.write(contents)
    os.chmod(path, 0o700)


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
        import sys
        import time

        port = None
        pid_file_path = None
        index = 1
        while index < len(sys.argv):
            if sys.argv[index] == "--port":
                port = sys.argv[index + 1]
                index += 2
            elif sys.argv[index] == "--pid_file":
                pid_file_path = sys.argv[index + 1]
                index += 2
            else:
                index += 1
        if not port or not pid_file_path:
            raise SystemExit(2)

        child_pid = os.fork()
        if child_pid:
            for _ in range(100):
                if os.path.exists(port) and os.path.exists(pid_file_path):
                    raise SystemExit(0)
                time.sleep(0.01)
            raise SystemExit(1)

        server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server_socket.bind(port)
        server_socket.listen(1)

        def handle_signal(_signum, _frame):
            server_socket.close()
            raise SystemExit(0)

        signal.signal(signal.SIGTERM, handle_signal)

        with open(pid_file_path, "w") as pid_file:
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
        universal_newlines=True).strip()

    tempdir = tempfile.mkdtemp(prefix="distcc-pump-path-test.")
    try:
        bindir = os.path.join(tempdir, "bin")
        prefix = os.path.join(tempdir, "prefix")
        os.makedirs(bindir)
        include_server_paths = BuildFakeIncludeServerPathCandidates(
            python, version, prefix, os.path.dirname(bindir))
        for include_server_path in include_server_paths:
            include_server_dir = os.path.dirname(include_server_path)
            if not os.path.isdir(include_server_dir):
                os.makedirs(include_server_dir)

        with open(pump_template) as input_file:
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

        for include_server_path in include_server_paths:
            MakeFakeIncludeServer(include_server_path)

        env = os.environ.copy()
        for key in (
            "DISTCC_FALLBACK",
            "DISTCC_HOSTS",
            "DISTCC_LOCATION",
            "DISTCC_MAX_DISCREPANCY",
            "DISTCC_POTENTIAL_HOSTS",
            "INCLUDE_SERVER_ARGS",
            "LSDISTCC_ARGS",
        ):
            env.pop(key, None)
        env["DISTCC_LOCATION"] = bindir
        env["DISTCC_HOSTS"] = "localhost,lzo,cpp"

        process = subprocess.Popen(
            [os.path.join(bindir, "pump"), "echo", "ok"],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True)
        stdout, stderr = process.communicate()

        if process.returncode != 0:
            raise AssertionError(
                "pump failed with status %d\nstdout:\n%s\nstderr:\n%s" %
                (process.returncode, stdout, stderr))
        if "ok\n" not in stdout:
            raise AssertionError("pump did not run the wrapped command")
    finally:
        shutil.rmtree(tempdir)


if __name__ == "__main__":
    Main()
