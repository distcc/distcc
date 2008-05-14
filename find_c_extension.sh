#! /bin/sh

# Usage: find_c_extension.sh BUILDDIR
#
# Write path of directory containing C-extension of build directory.
#
# More precisely, locate the unique directory of the form:
#
#   _include_server/lib.*/include_server/
#
# that contains file 'distcc_pump_c_extensions.so'. Write the path of this 
# directory to stdout and exit with status 0. If such a path does not exist 
# then write error message to stderr and exit with status 1.

builddir="$1"
so_files=`ls $builddir/_include_server/lib.*/include_server/\
distcc_pump_c_extensions.so`
if [ -z "$so_files" ]; then
  echo \
    '__________Could not find shared libraries for distcc-pump' 1>&2
  exit 1
elif echo "$so_files" | grep ' ' >/dev/null; then
  echo \
    '__________Shared libraries for multiple architectures discovered.' \
    1>&2
  echo \
    "__________Cannot determine which one to use among: $so_files" \
    1>&2
  exit 1
else
  # There was only one such file.
  dirname "$so_files"
  exit 0
fi
