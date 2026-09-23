#!/bin/bash
# Checks mucc's built-in assembler (src/asm.c) against GNU as: for
# test/asm-forms.s (every instruction form mucc emits) and for the
# assembly of every test program, with and without -fPIC, the two object
# files must be the same (test/elfcmp.py) and have the same line tables.
mucc=$1

if ! command -v as > /dev/null || ! command -v python3 > /dev/null; then
    echo "test/asm.sh: skipped (needs GNU as and python3)"
    exit 0
fi

tmp=`mktemp -d /tmp/mucc-asm-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

lines() {
    objdump --dwarf=decodedline $1 | grep -E '^\S+\s+[0-9]+\s+0x' | awk '{print $2, $3}'
}

# Assembles $1 both ways and compares.
check() {
    local s=$1 name=$2
    as -o $tmp/gas.o $s || { echo "testing asm $name ... failed (GNU as)"; exit 1; }
    if ! $mucc -c -o $tmp/mucc.o $s 2> $tmp/err || [ -s $tmp/err ]; then
        echo "testing asm $name ... failed"; cat $tmp/err; exit 1
    fi
    if ! python3 test/elfcmp.py $tmp/gas.o $tmp/mucc.o; then
        echo "testing asm $name ... failed"; exit 1
    fi
    if [ "$(lines $tmp/gas.o)" != "$(lines $tmp/mucc.o)" ]; then
        echo "testing asm $name ... failed (line tables differ)"; exit 1
    fi
}

check test/asm-forms.s asm-forms.s
echo "testing asm asm-forms.s ... passed"

n=0
for f in test/*.c; do
    for pic in "" -fPIC; do
        $mucc $pic -Iinclude -Itest -S -o $tmp/t.s $f 2> /dev/null || continue
        check $tmp/t.s "$f $pic"
        n=$((n+1))
    done
done
echo "testing asm: $n test programs assemble the same as with GNU as ... passed"
