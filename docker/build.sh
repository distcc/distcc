#!/bin/bash

set -e # exit immediately if a command returns with a nonzero exit code

echo "*** Building distcc/base image"
docker build -t distcc/base -f base/Dockerfile base

if [ $# -eq 0 ]; then
  compilers=("gcc-4.8" "gcc-5" "clang-3.8")
else
  compilers=("$1")
fi

for compiler in "${compilers[@]}"
do
  echo "*** Building distcc/$compiler image"
  docker build -t distcc/$compiler -f compilers/Dockerfile.$compiler .
done

echo "*** Building distcc"
for compiler in "${compilers[@]}"
do
  echo "*** Building distcc with distcc/$compiler image"
  set -x
  docker run --rm -it -v /tmp:/tmp -v `pwd`/..:/src:rw -w /src distcc/$compiler bash -c "./autogen.sh && ./configure && make clean && make && make install && make check" &> distcc-$compiler.log
  set +x
done
