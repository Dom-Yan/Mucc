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
check test/asm-directives.s asm-directives.s
echo "testing asm asm-directives.s ... passed"

# Call frame information is dropped, as mucc makes none for C: it needs
# no system assembler.
printf 'f:\n .cfi_startproc\n push %%rbp\n .cfi_def_cfa_offset 16\n pop %%rbp\n ret\n .cfi_endproc\n' > $tmp/cfi.s
if ! $mucc -c -o $tmp/cfi.o $tmp/cfi.s 2> $tmp/err || [ -s $tmp/err ]; then
    echo "testing asm .cfi_* ... failed"; cat $tmp/err; exit 1
fi
echo "testing asm .cfi_* ... passed"

# musl's x86-64 assembly, which the bundled C library needs (its math
# overrides are left out; mucc builds musl's C versions instead).
n=0
for s in thirdparty/musl/crt/x86_64/*.s thirdparty/musl/src/*/x86_64/*.s; do
    case $s in */math/*) continue;; esac
    check $s $s
    n=$((n+1))
done
# crtn.s has no symbols, so, as with GNU as, no .symtab either.
$mucc -c -o $tmp/crtn.o thirdparty/musl/crt/x86_64/crtn.s
if readelf -S $tmp/crtn.o | grep -q symtab; then
    echo "testing asm: crtn.s has a .symtab ... failed"; exit 1
fi
echo "testing asm: $n musl files assemble the same as with GNU as ... passed"

n=0
for f in test/*.c; do
    for pic in "" -fPIC; do
        $mucc $pic -Iinclude -Itest $(sed -n 's|^// flags: ||p' $f) -S -o $tmp/t.s $f 2> /dev/null || continue
        check $tmp/t.s "$f $pic"
        n=$((n+1))
    done
done
echo "testing asm: $n test programs assemble the same as with GNU as ... passed"
