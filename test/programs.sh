#!/bin/bash
# Programs written from scratch the way people write C for Linux
# (test/programs/: processes, networking, files, Linux's own interfaces,
# threads, terminals, the language, inline and .S assembly), each built
# by the single binary alone and then run. They're built inside an empty
# root holding nothing but mucc (no /usr, /bin, /tmp, /proc or /dev),
# with `unshare -r chroot`; where user namespaces aren't allowed, in an
# empty directory with an empty environment instead.
mucc=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
src=$(cd "$(dirname "$0")/programs" && pwd)

tmp=`mktemp -d /tmp/mucc-programs-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
root=$tmp/root
mkdir -p $root/src $root/out
cp $mucc $root/mucc
cp $src/* $root/src/

if unshare -r chroot $root /mucc --version > /dev/null 2>&1; then
    echo "testing programs: built in an empty root holding only mucc"
    build() { unshare -r chroot $root /mucc "$@"; }
    dir=
else
    echo "testing programs: built with an empty environment (no user namespaces here)"
    build() { (cd $root && env -i ./mucc "$@"); }
    dir=.
fi

fail=0
for p in procs net files kernel threads term lang lowlevel; do
    srcs=$dir/src/$p.c
    [ $p = lowlevel ] && srcs="$srcs $dir/src/lowlevel_asm.S"
    if ! build -std=c23 -Wall -o $dir/out/$p $srcs -lm -lpthread 2> $tmp/err; then
        echo "testing programs $p ... failed to build"; cat $tmp/err; fail=1; continue
    fi
    if out=$(cd $tmp && timeout 60 $root/out/$p 2>&1); then
        echo "testing programs $p ... passed ($(echo "$out" | tail -1))"
    else
        echo "testing programs $p ... failed"; echo "$out"; fail=1
    fi
done
exit $fail
