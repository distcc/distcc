#! /bin/bash

# Run autoconfig and autoheader in a safe way. Running these commands directly
# will likely not work because of caching mechanisms: the information in
# version.sh will not be propagated correctly.

# We need a stamp file to compare version.sh against, see the Makefile.
AUTOCONF_STAMP=autoconf_stamp

if  ! [ -e ./run_all_autoconf.sh ]; then
  echo "Must run this script from the directory containing run_all_autoconf.sh"
     1>&2;
  exit 1
fi

# Now run autoconf and autoheader through autoreconf -f. This way autoconf is
# apparently not getting confused by old configure files and m4 caches.
echo "Now running autoreconf in order to run autoconf and autoheader."
autoreconf -f -v && touch $AUTOCONF_STAMP

echo "System must be configured. Run: ./configure."
