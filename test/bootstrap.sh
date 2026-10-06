#!/bin/bash
# Builds the single binary from this tree with only mucc: the stage-1
# compiler is the given mucc (the previous release, or build/mucc), and
# gcc, cc, as, ld, ar, ranlib, strip and objcopy are commands that fail,
# so nothing else can take part. Works on a copy, so the tree's own
# build is left alone.
#
#   test/bootstrap.sh MUCC OUT [LDFLAGS]   builds OUT (LDFLAGS=-s strips)
#   test/bootstrap.sh MUCC                 checks it is MUCC, byte for byte
mucc=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
out=${2:+$(cd "$(dirname "$2")" && pwd)/$(basename "$2")}
src=$(cd "$(dirname "$0")/.." && pwd)

tmp=`mktemp -d /tmp/mucc-bootstrap-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

mkdir $tmp/bin $tmp/tree
for t in gcc cc c99 as ld ar ranlib strip objcopy; do
    printf '#!/bin/sh\necho "%s $*" >> %s/calls\nexit 1\n' $t $tmp > $tmp/bin/$t
    chmod +x $tmp/bin/$t
done
cp -R $src/src $src/include $src/thirdparty $src/Makefile $tmp/tree/
rm -f $tmp/tree/src/*.o

j=$(nproc 2> /dev/null || echo 4)
(cd $tmp/tree && export PATH=$tmp/bin:$PATH &&
    make -j$j CC="$mucc" mucc && make libc &&
    make -j$j build/mucc LDFLAGS_SINGLE="$3") > $tmp/log 2>&1
if [ $? -ne 0 ]; then
    echo "testing bootstrap ... failed to build"
    tail -20 $tmp/log
    exit 1
fi

# musl's configure asks whether the linker takes flags for a libc.so
# (see test/libc.sh); those calls don't count.
if grep -v -E '^ld -o /dev/null .* -shared( |$)' $tmp/calls 2> /dev/null; then
    echo "testing bootstrap ... failed (the calls above were made)"
    exit 1
fi

if [ -n "$out" ]; then
    cp $tmp/tree/build/mucc "$out"
    echo "testing bootstrap ... built $2 with $1 only"
elif cmp -s $tmp/tree/build/mucc "$mucc"; then
    echo "testing bootstrap ... passed (built by itself, the same bytes)"
else
    echo "testing bootstrap ... failed (built by itself, different bytes)"
    exit 1
fi
