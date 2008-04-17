#!/bin/sh

# This script takes the files produced by "build-distc.sh",
# and uploads them to code.google.com using googlecode_upload.py,
# annotating them with some descritions of what they contain.

echo -n "Enter your code.google.com username: "
read username || exit 1

test -d tmp || { echo "Run build-distcc.sh first!" >&2; exit 1; }
for file in `find tmp -name \*.deb -o -name \*.rpm`; do
  case "$file" in
    *distcc-server*)  name="Distcc server (distccd)" ;;
    *distcc-include*) name="Distcc include server (pump)" ;;
    *distcc*)         name="Distcc client (distcc)" ;;
    *)               name="unknown" ;;
  esac
  case "$file" in
    *.deb)     filetype="Debian Package" ;;
    *.src.rpm) filetype="RPM Source Package" ;;
    *.rpm)     filetype="RPM Binary Package" ;;
    *)         filetype="Unknown" ;;
  esac
  summary="Untested prerelease of $name: $filetype"
  echo "Uploading '$file' with summary '$summary'"
  ./googlecode_upload.py -u "$username" -p distcc -s "$summary" "$file"
done
