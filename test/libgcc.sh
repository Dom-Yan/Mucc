#!/bin/bash
# Checks that code mucc generates never calls a helper from gcc's runtime
# library, libgcc (like __udivti3 or __popcountdi2): mucc's bundled C
# library (see PLAN.md) replaces glibc, not libgcc, so nothing may need
# it. Every test program and mucc itself are compiled, and no object may
# refer to a symbol libgcc defines. Needs gcc, to find libgcc.
mucc="$1 -Iinclude"

tmp=`mktemp -d /tmp/mucc-libgcc-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing libgcc $1 ... passed"
    else
        echo "testing libgcc $1 ... failed"
        exit 1
    fi
}

libgcc=$(gcc -print-libgcc-file-name 2> /dev/null)
if [ ! -f "$libgcc" ]; then
    echo "testing libgcc ... skipped (no gcc to find libgcc with)"
    exit 0
fi
nm -g --defined-only $libgcc ${libgcc%.a}_eh.a 2> /dev/null |
    awk 'NF == 3 { print $3 }' | sort -u > $tmp/libgcc

# Prints the symbols the objects given refer to that libgcc defines.
libgcc_refs() {
    nm -u "$@" | awk '{ print $NF }' | sort -u | comm -12 - $tmp/libgcc
}

# The check itself must catch a call to a libgcc helper.
echo 'long __popcountdi2(long); int main(void) { return __popcountdi2(3); }' |
    $mucc -c -o $tmp/helper.o -xc -
[ "$(libgcc_refs $tmp/helper.o)" = __popcountdi2 ]
check 'finds a helper call'

mkdir $tmp/test
for f in test/*.c; do
    $mucc -w -Itest $(sed -n 's|^// flags: ||p' $f) -c \
        -o $tmp/test/$(basename $f .c).o $f || exit 1
done
refs=$(libgcc_refs $tmp/test/*.o)
[ -z "$refs" ] || echo "$refs"
[ -z "$refs" ]
check 'test programs'

mkdir $tmp/src
for f in src/*.c; do
    $mucc -c -o $tmp/src/$(basename $f .c).o $f || exit 1
done
refs=$(libgcc_refs $tmp/src/*.o)
[ -z "$refs" ] || echo "$refs"
[ -z "$refs" ]
check mucc
