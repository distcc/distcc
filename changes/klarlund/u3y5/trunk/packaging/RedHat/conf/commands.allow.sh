#!/bin/sh
# --- /etc/site/current/distcc/commands.allow.sh ----------------------
#
# This file is a shell script that gets sourced by /etc/init.d/distcc.
# It's purpose is to optionally set the following environment
# variables, which affect the behaviour of distccd:
#
#     DISTCC_CMDLIST
#         If the environment variable DISTCC_CMDLIST is set, distccd will
#         load a list of supported commands from the file named by
#         DISTCC_CMDLIST, and will refuse to serve any command whose last
#         DISTCC_CMDLIST_MATCHWORDS last words do not match those of a
#         command in that list.  See the comments in src/serve.c.
#
#     DISTCC_CMDLIST_NUMWORDS
#         The number of words, from the end of the command, to match.  The
#         default is 1.
#
# The interface to this script is as follows.
# Input variables:
#    CMDLIST: this variable will hold the full path of the commands.allow file.
# Side effects:
#    This script should write into the commands.allow file specified by
#    $CMDLIST.  It should write the list of allowable commands, one per line.
# Output variables:
#    DISTCC_CMDLIST and DISTCC_CMDLIST_NUMWORDS. See above.
#-----------------------------------------------------------------------------#

# Here are the parts that you may want to modify.

numwords=1
allowed_compilers="
  /usr/bin/cc
  /usr/bin/c++
  /usr/bin/c89
  /usr/bin/c99
  /usr/bin/gcc
  /usr/bin/g++
  /usr/bin/*gcc-*
  /usr/bin/*g++-*
"

# You shouldn't need to alter anything below here.

[ "$CMDLIST" ] || {
   echo "$0: don't run this script directly!" >&2
   echo "Run /etc/init.d/distcc (or equivalent) instead." >&2
   exit 1
}

echo $allowed_compilers | tr ' ' '\n' > $CMDLIST
DISTCC_CMDLIST=$CMDLIST
DISTCC_CMDLIST_NUMWORDS=$numwords
