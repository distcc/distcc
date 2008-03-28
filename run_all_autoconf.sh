#! /bin/bash

# Run autoconfig and autoheader in a safe way. Running these commands directly
# will likely not work because of caching mechanisms: the information in
# version.sh will not be propagated correctly.

# We need a stamp file to compare version.sh against, see the Makefile.
AUTOCONF_STAMP=autoconf_stamp

if  ! [ $(basename $(pwd)) = "distcc_pump" ]; then
  echo "Must be in distcc_pump directory" 1>&2;
  exit 1
fi

# Now run autoconf and autoheader through autoreconf -f. This way autoconf is
# apparently not getting confused by old configure files and m4 caches.
echo "Now running autoreconf in order to run autoconf and autoheader."
autoreconf -f -v && touch $AUTOCONF_STAMP

echo "System must be configured. Run: ./configure."
