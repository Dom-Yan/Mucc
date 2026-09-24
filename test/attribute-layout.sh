#!/bin/bash
# Struct layouts with GNU attributes must match gcc's exactly: compile
# test/attribute-layout.c with mucc and with gcc, and compare the output.
#
# Like test/errors.sh, point at the repo's include/ so this also works for
# stage2/mucc, which has no include/ next to it.
mucc="$1 -Iinclude"

tmp=`mktemp -d /tmp/mucc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

$mucc -o $tmp/mucc-layout test/attribute-layout.c || exit 1
gcc -o $tmp/gcc-layout test/attribute-layout.c || exit 1
$tmp/mucc-layout > $tmp/mucc.out
$tmp/gcc-layout > $tmp/gcc.out

if ! diff $tmp/gcc.out $tmp/mucc.out; then
  echo "testing attribute layout against gcc ... failed"
  exit 1
fi
echo "testing attribute layout against gcc ($(wc -l < $tmp/gcc.out) lines) ... passed"
