#!/bin/bash
# Checks mucc's x86 intrinsics (include/*intrin.h) against gcc's:
# test/intrin.c, built by mucc and by gcc with its own headers, must print
# the same. Needs gcc, and a CPU with SSE4.2, AES and PCLMUL.
mucc=$1

if ! command -v gcc > /dev/null; then
    echo "testing intrin ... skipped (needs gcc)"
    exit 0
fi
for f in sse4_2 aes pclmulqdq; do
    if ! grep -qw $f /proc/cpuinfo; then
        echo "testing intrin ... skipped (the CPU has no $f)"
        exit 0
    fi
done

tmp=`mktemp -d /tmp/mucc-intrin-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

flags="-msse4.2 -maes -mpclmul"
gcc -w $flags -o $tmp/gcc test/intrin.c -lm || exit 1
$mucc -Iinclude $flags -o $tmp/mucc test/intrin.c -lm || exit 1
$tmp/gcc -v > $tmp/gcc.out
$tmp/mucc -v > $tmp/mucc.out
if ! diff $tmp/gcc.out $tmp/mucc.out > $tmp/diff; then
    echo "testing intrin ... failed: results differ from gcc's (<) in:"
    head -20 $tmp/diff
    exit 1
fi
echo "testing intrin: $(wc -l < $tmp/gcc.out) results the same as gcc's ... passed"
