#!/bin/sh

CORES=""
# nproc is a GNU tool, may not be available everywhere, but it is preferable,
# when it is available
# This
if hash nproc 2>/dev/null ; then
  CORES="$(nproc)"
else
  # try BSD / OS X
  if hash sysctl 2>/dev/null ; then
    CORES="$(sysctl -n hw.ncpu)" 2>/dev/null
  fi
fi

if [ -z "${CORES}" ];then
  CORES=1
fi

set -ev # exit on error, be verbose

CONFARGS=""
PKGS=""
CCPKG=${CC}

if [ "${GITHUB_JOB}" = "musl_gcc" ]; then
  CCPKG="musl-tools"
  CONFARGS="--enable-Werror --without-libiberty"
fi

if [ "${RUNNER_OS}" = "macOS" ]; then
  CONFARGS="--without-libiberty"
fi

if [ "${RUNNER_OS}" = "Linux" ]; then
  sudo apt-get update
  sudo apt-get install -y ${CCPKG} python3-dev libiberty-dev libavahi-client-dev libavahi-common-dev
fi

if [ -n "${CC}" ]; then
  $CC --version
fi

./autogen.sh
./configure ${CONFARGS}
make VERBOSE=1 -j${CORES}
make VERBOSE=1 check

