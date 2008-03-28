#!/bin/sh
# Simple shell script to build distcc SRPM and RPM from sources

# When releasing a new version, you need to do four things:
# 1. edit the 'Release' field of the spec file
# 2. add a comment to the %changelog section of the spec file
# 3. edit the 'DISTCC_VERSION' and 'PUMP_SUBVERSION' variables in version.sh.
# It would be better if this was easier, but that's how it is right now.

source version.sh 

PKG=distcc
TOP=`pwd`

# Check if we have a bad kernel + RPM combination.
# Background:
# Versions of RPM prior to 4.4.1 incorrectly add a dependency on
# 'linux-gate.so.1', which is a virtual dso in 2.6+ Linux kernels. Since this
# is not a real lib provided by any package, the resulting RPMs won't install
# due to failed dependency checks.
# For the curious, more linux-gate details are documented at:
# http://www.trilithium.com/johan/2005/08/linux-gate/
#
# See if running on a kernel with the virtual dso.
HAS_VSDO=0
cat /proc/self/maps | grep -q vdso
if [ $? -eq 0 ]; then
  HAS_VSDO=1
fi
# See if we have incompatible RPM tools.
if [ $HAS_VSDO -eq 0 ]; then
  grep -q 'linux-gate' /usr/lib/rpm/find-requires
  if [ $? -ne 0 ]; then
    echo 'ERROR: Your combination of RPM and kernel is buggy.'
    echo 'Upgrade to RPM 4.4.1-5 or later, or patch /usr/lib/rpm/find-requires'
    echo 'to special-case the "linux-gate" dependency.'
    echo 'see also http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=338515'
    exit 1
  fi
fi

set -xe

rm -rf tmp
mkdir tmp
cd tmp

# Set up rpm build environment
RPM_SOURCE_DIR=$TOP
RPM_BUILD_DIR=$TOP
RPM_RPM_DIR=$TOP/tmp/rpms
RPM_SRPM_DIR=$TOP/tmp/srpms
# lifted from gboilerplate
USERNAME="$(whoami)"
PACKAGER="$(ldapsearch -x -LLL -b 'ou=People,dc=google,dc=com' "uid=$USERNAME" cn|grep '^cn'|colrm 1 4) <$USERNAME@google.com>"

# Temporary directories
rm -rf $RPM_RPM_DIR
mkdir $RPM_RPM_DIR
rm -rf $RPM_SRPM_DIR
mkdir $RPM_SRPM_DIR
rm -rf /tmp/$PKG-$DISTCC_VERSION-root
mkdir /tmp/$PKG-$DISTCC_VERSION-root


CROSS=${CROSS-}
CC=${CROSS}gcc \
CXX=${CROSS}g++ \
AR=${CROSS}ar \
RANLIB=${CROSS}ranlib \
rpmbuild -bb $TOP/$PKG.spec \
  --define "_sourcedir $RPM_SOURCE_DIR" \
  --define "_builddir $RPM_BUILD_DIR" \
  --define "_rpmdir $RPM_RPM_DIR" \
  --define "_srcrpmdir $RPM_SRPM_DIR" \
  --define "name $PKG" \
  --define "version $DISTCC_VERSION" \
  --define "release $PUMP_SUBVERSION" \
  --define "packager $PACKAGER" \
  --define "bundle_dir $TOP"
# Determine the name of the directory where the package went
RPMARCH=`rpmbuild --showrc $TOP/$PKG.spec | grep "^build arch.*:" | sed -e 's/^build arch[[:space:]]*:[[:space:]]*//'`

find . -type f | xargs chmod 644
find . -type d | xargs chmod 755

echo Results in $RPM_SRPM_DIR and $RPM_RPM_DIR/${RPMARCH} :
ls -l $RPM_SRPM_DIR $RPM_RPM_DIR/$RPMARCH

# Convert RPMs to DEBs for goobuntu.
fakeroot alien -c -k -v $RPM_RPM_DIR/$RPMARCH/*.rpm

