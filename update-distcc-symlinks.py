#!/usr/bin/env python3

import subprocess, string, os, stat, re

distcc_dir = "/usr/lib/distcc"
gcc_dir = "/usr/lib/gcc"
gcccross_dir = "/usr/lib/gcc-cross"
old_symlinks = set()
new_symlinks = set()
standard_names = ["cc", "c++", "c89", "c99"]

if not os.access(distcc_dir, os.X_OK):
  os.mkdir(distcc_dir)

def consider(name):
  if os.access("/usr/bin/%(name)s" % vars(), os.X_OK):
    new_symlinks.add(name)
    print(name)

def consider_gcc(prefix, suffix):
  consider("%(prefix)sgcc%(suffix)s" % vars())
  consider("%(prefix)sg++%(suffix)s" % vars())

def consider_clang(suffix):
  consider("clang%(suffix)s" % vars())
  consider("clang++%(suffix)s" % vars())

for x in standard_names:
  consider(x)

consider_gcc("", "")
consider_gcc("c89-", "")
consider_gcc("c99-", "")
for gnu_host in os.listdir(gcc_dir):
  consider_gcc("%(gnu_host)s-" % vars(), "")
  for version in os.listdir(gcc_dir + "/" + gnu_host):
    consider_gcc("", "-%(version)s" % vars())
    consider_gcc("%(gnu_host)s-" % vars(), "-%(version)s" % vars())
for gnu_host in os.listdir(gcccross_dir):
  consider_gcc("%(gnu_host)s-" % vars(), "")
  for version in os.listdir(gcccross_dir + "/" + gnu_host):
    consider_gcc("", "-%(version)s" % vars())
    consider_gcc("%(gnu_host)s-" % vars(), "-%(version)s" % vars())


consider_clang("")
for ent in os.listdir("/usr/lib"):
  if ent.startswith("llvm-"):
    version = ent.split("-")[1]
    consider_clang("-%(version)s" % vars())

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
    if os.access("/usr/bin/distcc", os.X_OK):
      os.symlink("../../bin/distcc", distcc_dir + "/" + link)
    else:
      os.symlink("../../local/bin/distcc", distcc_dir + "/" + link)
