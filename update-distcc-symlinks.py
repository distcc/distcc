#!/usr/bin/env python3

import subprocess
import string
import os
import stat
import re

distcc_dir = "@prefix@/lib/distcc"
GCC_LIBEXEC_DIRS = (
    '/usr/lib/gcc',  # Debian native GCC compilers
    '/usr/lib/gcc-cross',  # Debian GCC cross-compilers
    '/usr/libexec/gcc', # rpm-based distros
)
old_symlinks = set()
new_symlinks = set()
standard_names = ["cc", "c++", "c89", "c99"]

if not os.access(distcc_dir, os.X_OK):
    os.mkdir(distcc_dir)


def consider(name):
    if os.access(f"/usr/bin/{name}", os.X_OK):
        new_symlinks.add(name)
        print(name)


def consider_gcc(prefix, suffix=""):
    consider(f"{prefix}gcc{suffix}")
    consider(f"{prefix}g++{suffix}")


def consider_clang(suffix):
    consider(f"clang{suffix}")
    consider(f"clang++{suffix}")


for x in standard_names:
    consider(x)

consider_gcc("")
consider_gcc("c89-")
consider_gcc("c99-")


def sloppy_listdir(thedir):
    try:
        return os.listdir(thedir)
    except FileNotFoundError:
        pass
    except NotADirectoryError:
        pass
    return []


def scan_gcc_libexec(gcc_dir):
    for gnu_host in sloppy_listdir(gcc_dir):
        consider_gcc(f"{gnu_host}-")
        for version in sloppy_listdir(gcc_dir + "/" + gnu_host):
            consider_gcc("", f"-{version}")
            consider_gcc(f"{gnu_host}-", f"-{version}")


for gcc_dir in GCC_LIBEXEC_DIRS:
    scan_gcc_libexec(gcc_dir)


consider_clang("")
for ent in os.listdir("/usr/lib"):
    if ent.startswith("llvm-"):
        version = ent.split("-")[1]
        consider_clang(f"-{version}")

for name in os.listdir(distcc_dir):
    mode = os.lstat(distcc_dir + "/" + name).st_mode
    if stat.S_ISLNK(mode):
        if os.access(distcc_dir + "/" + name, os.X_OK):
            old_symlinks.add(name)
        else:
            os.unlink(distcc_dir + "/" + name)

for link in old_symlinks:
    if link not in new_symlinks:
        os.unlink(distcc_dir + "/" + link)

for link in new_symlinks:
    if link not in old_symlinks:
        os.symlink("../../bin/distcc", distcc_dir + "/" + link)
