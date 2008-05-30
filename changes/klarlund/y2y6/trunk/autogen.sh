#! /bin/sh -e

# Usage: autogen.sh [srcdir]
# Run this script to build distcc from CVS.

## first try the default names
ACLOCAL="aclocal"
AUTOHEADER="autoheader"
AUTOCONF="autoconf"

if which $AUTOCONF > /dev/null
then
    :
else
    echo "$0: need autoconf 2.53 or later to regenerate configure scripts" >&2
    exit 1
fi

srcdir=`dirname "$0"`
builddir=`pwd`

echo "$0: running $ACLOCAL"
(cd $srcdir && $ACLOCAL -I m4 --output=$builddir/aclocal.m4)

echo "$0: running $AUTOHEADER"
[ -d src ] || mkdir src  # Needed for autoheader to generate src/config.h.in.
$AUTOHEADER $srcdir/configure.ac

echo "$0: running $AUTOCONF"
$AUTOCONF $srcdir/configure.ac > configure
chmod +x configure

if [ "$srcdir" = "." ]; then
  echo "Now run './configure' and then 'make'."
else
  echo "Now run './configure --srcdir=$srcdir' and then 'make'."
fi
exit 0
