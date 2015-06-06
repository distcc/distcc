# distcc: a fast, free distributed C/C++ compiler #

## NEWS: distcc 3.1 released! ##

Distcc release 3.1 is now available at [Downloads](http://code.google.com/p/distcc/downloads/list).
See [NEWS](http://distcc.googlecode.com/svn/trunk/NEWS) for details.

## Overview ##

distcc is a program to distribute builds of C, C++, Objective C or Objective C++ code across several machines on a network. distcc should always generate the same results as a local build, is simple to install and use, and is usually much faster than a local compile.

distcc does not require all machines to share a filesystem, have synchronized clocks, or to have the same libraries or header files installed. They can even have different processors or operating systems, if cross-compilers are installed.

> "Just wanted to drop you a line to say that we are now using distcc at work and it is excellent. We have a rack of opteron machines that we use for computational tasks and we are now using them as a compile farm to compile our ~1MLOC C++ tree (which can take an hour on a single CPU to recompile if we change certain header files). We tried using Sun's grid engine to do this (we already use it to schedule our computational jobs), but the combination of its polled operation and the overhead of NFS led to little improvement (and greatly stressed our network). With distcc compile times are way down and my productivity has greatly improved; the two best features for me are its low impact on the network and its simplicity.

> "Thanks a lot for a great tool!" — Jeremy Barnes

## 60-second instructions ##

  1. For each machine, download distcc, unpack, and do
> > `./configure && make && sudo make install`
  1. On each of the servers, run `distccd --daemon`, with `--allow` options to restrict access.
  1. Put the names of the servers in your environment:
> > `export DISTCC_POTENTIAL_HOSTS='localhost red green blue'`
  1. Build!
> > `cd ~/work/myproject; pump make -j8 CC=distcc`

## Full documentation ##

See our [documentation pages](http://distcc.googlecode.com/svn/trunk/doc/web/index.html).

You may also be interested in an explanation of [common pump-mode errors](http://distcc.googlecode.com/svn/trunk/doc/web/man/include_server_1.html#TOC_5).

## distcc 3 ##

The major improvement in distcc 3 is the inclusion of "pump" mode.
In "pump" mode, distcc sends source file with their included header files to the compilation servers, which now carry out both preprocessing and compilation. As a result, distcc-pump can distribute files up to 10 times faster to compilation servers than distcc.

distcc-pump is easily deployed through a wrapper script around an existing build command, such as 'make'.  Pump mode uses the system header files from the compilation servers, so it works best if all of your compilation servers are configured identically, or if you use cross-compilers that come with their own system header files.


---
