#!/bin/sh -e

# Run this from the 'packaging' directory, just under rootdir

# We can only build rpm packages, if the rpm build tools are installed
if [ \! -x /usr/bin/rpmbuild ]
then
  echo "Cannot find /usr/bin/rpmbuild. Not building an rpm." 1>&2
  exit 0
fi

# Check if we have a bad kernel + RPM combination; that is, a kernel
# that does not support vdso, and a find-requires script that doesn't
# notice that and special-case linux-gate as a result.
# Background:
#    Versions of RPM prior to 4.4.1 incorrectly add a dependency on
#    'linux-gate.so.1', which is a virtual dso in 2.6+ Linux
#    kernels. Since this is not a real lib provided by any package, the
#    resulting RPMs won't install due to failed dependency checks.
# For the curious, more linux-gate details are documented at:
#    http://www.trilithium.com/johan/2005/08/linux-gate/
if ! cat /proc/self/maps | grep -q vdso; then
  if ! grep -q 'linux-gate' /usr/lib/rpm/find-requires; then
    echo 'ERROR: Your combination of RPM and kernel is buggy.'
    echo 'Upgrade to RPM 4.4.1-5 or later, or patch /usr/lib/rpm/find-requires'
    echo 'to special-case the "linux-gate" dependency.'
    echo 'see also http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=338515'
    exit 1
  fi
fi

# Check the commandline flags
PACKAGE="$1"
VERSION="$2"
fullname="${PACKAGE}-${VERSION}"
archive=../$fullname.tar.gz

if [ -z "$1" -o -z "$2" ]
then
  echo "Usage: $0 <package name> <package version>" 1>&2
  exit 0
fi

# Double-check we're in the packages directory, just under rootdir
if [ \! -r ../Makefile -a \! -r ../INSTALL ]
then
  echo "Must run $0 in the 'packaging' directory, under the root directory." 1>&2
  echo "Also, you must run \"make dist\" before running this script." 1>&2
  exit 0
fi

if [ \! -r "$archive" ]
then
  echo "Cannot find $archive. Run \"make dist\" first." 1>&2
  exit 0
fi

# Create the directory where the input lives, and where the output should live
RPM_SOURCE_DIR="/tmp/rpmsource-$fullname"
RPM_BUILD_DIR="/tmp/rpmbuild-$fullname"

trap 'rm -rf $RPM_SOURCE_DIR $RPM_BUILD_DIR; exit $?' EXIT HUP INT TERM

rm -rf "$RPM_SOURCE_DIR" "$RPM_BUILD_DIR"
mkdir "$RPM_SOURCE_DIR"
mkdir "$RPM_BUILD_DIR"

cp "$archive" "$RPM_SOURCE_DIR"

rpmbuild -bb RedHat/rpm.spec \
  --define "NAME $PACKAGE" \
  --define "VERSION $VERSION" \
  --define "_sourcedir $RPM_SOURCE_DIR" \
  --define "_builddir $RPM_BUILD_DIR" \
  --define "_rpmdir $RPM_SOURCE_DIR"

# Clean out any existing rpms from a previous build.
rm -f "$PACKAGE"*[-._]"$VERSION"[-._]*.rpm

# We want to get not only the main package but devel etc, hence the middle *
mv "$RPM_SOURCE_DIR"/*/"$PACKAGE"-*"$VERSION"*.rpm .

echo
echo "The rpm package file(s) are located in $PWD:"
ls *.rpm
