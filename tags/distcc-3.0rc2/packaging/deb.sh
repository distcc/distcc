#!/bin/sh -e

# This takes a package name and version,
# and a list of rpms to convert to .deb files.
# It puts them all under a debian-* directory under the current directory.
#
# Run this from the 'packaging' directory, just under rootdir

PACKAGE="$1";
VERSION="$2";
shift; shift;

# Clean out any old .deb files from a previous build.
rm -f "$PACKAGE"*[-._]"$VERSION".deb

for file in "$@"; do
  fakeroot alien -c -k -v "$file"
done

echo
echo "The Debian package files are located in $PWD:"
ls *.deb

