#! /bin/sh

# This file, contributed by Dimitri PAPADOPOULOS-ORFANOS <papadopo@shfj.cea.fr>
# may be installed as "cc" somewhere on your $PATH ahead of the real gcc.  That
# allows you to just use regular Makefiles without modifying them to change
# hardcoded calls to cc.  

# This script will be a bit slow because of the overhead of running
# things through a shell.  In a future release, this function should
# be supported directly by distcc, which should be a bit faster.

DISTCC_HOME=/usr/local/distcc

name=`basename $0`

if [ "$name" = distcc ]; then
    echo "In normal use distcc is not called by its real name." 1>&2
    echo "Instead create links to the actual compiler you wish to run, e.g." 1>&2
    echo "  ln -s distcc gcc" 1>&2
    echo "and make sure the link is before the real compiler in your path." 1>&2
    exit 1
fi

unset found
IFS=:
for item in $PATH; do
    if [ -x "$item/$name" -a ! -d "$item/$name" ]; then
	if [ `cd $item; /bin/pwd` != `/bin/pwd` ]; then
	    found=true
	    break
	fi
    fi
done

if [ -n "$found" ]; then
    exec "distcc $item/$name $@"
else
    echo "$name: not found" 1>&2
fi
exit 1
