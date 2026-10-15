#!/bin/bash
# Checks mucc's built-in assembler (src/asm.c) against GNU as, for each
# target whose GNU as is here: `as` for the CPU this runs on, or the
# cross-compiling one (x86_64-linux-gnu-as, aarch64-linux-gnu-as). For
# test/asm-forms.s (or asm-forms-arm64.s: every instruction form mucc
# knows), musl's assembly and the assembly of every test program, with
# and without -fPIC, the two object files must be the same
# (test/elfcmp.py) and have the same line tables.
mucc=$1

if ! command -v python3 > /dev/null; then
    echo "test/asm.sh: skipped (needs python3)"
    exit 0
fi

arch=$(uname -m | sed 's/arm64/aarch64/')

# GNU binutils program $2 for target $1, if it's here
tool() {
    local name=$2
    [ $1 = $arch ] || name=$1-linux-gnu-$2
    command -v $name
}

tmp=`mktemp -d /tmp/mucc-asm-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

lines() {
    $objdump --dwarf=decodedline $1 | grep -E '^\S+\s+[0-9]+\s+0x' | awk '{print $2, $3}'
}

# Assembles $1 both ways (as for the target, $as, and mucc for it, $mc)
# and compares.
check() {
    local s=$1 name=$2
    $as -o $tmp/gas.o $s || { echo "testing asm $name ... failed (GNU as)"; exit 1; }
    if ! $mc -c -o $tmp/mucc.o $s 2> $tmp/err || [ -s $tmp/err ]; then
        echo "testing asm $name ... failed"; cat $tmp/err; exit 1
    fi
    if ! python3 test/elfcmp.py $tmp/gas.o $tmp/mucc.o; then
        echo "testing asm $name ... failed"; exit 1
    fi
    if [ "$(lines $tmp/gas.o)" != "$(lines $tmp/mucc.o)" ]; then
        echo "testing asm $name ... failed (line tables differ)"; exit 1
    fi
}

# The assembly of every test program, with and without -fPIC (and the
# options in $@)
check_programs() {
    local n=0
    for f in test/*.c; do
        for pic in "" -fPIC; do
            $mc $pic "$@" -Iinclude -Itest $(sed -n 's|^// flags: ||p' $f) -S -o $tmp/t.s $f \
                2> /dev/null || continue
            check $tmp/t.s "$f $pic $*"
            n=$((n+1))
        done
    done
    echo "testing asm: $n test programs assemble the same as with GNU as $* ... passed"
}

x86_64_tests() {
    check test/asm-forms.s asm-forms.s
    echo "testing asm asm-forms.s ... passed"
    check test/asm-directives.s asm-directives.s
    echo "testing asm asm-directives.s ... passed"

    # Call frame information is dropped, as mucc makes none for C: it
    # needs no system assembler.
    printf 'f:\n .cfi_startproc\n push %%rbp\n .cfi_def_cfa_offset 16\n pop %%rbp\n ret\n .cfi_endproc\n' > $tmp/cfi.s
    if ! $mc -c -o $tmp/cfi.o $tmp/cfi.s 2> $tmp/err || [ -s $tmp/err ]; then
        echo "testing asm .cfi_* ... failed"; cat $tmp/err; exit 1
    fi
    echo "testing asm .cfi_* ... passed"

    # Code padded to an alignment, by 1 to 127 bytes, is NOPs, or a jump to
    # the aligned address over them, as GNU as pads it (not byte for byte:
    # that differs between GNU as versions).
    for k in $(seq 1 127); do
        printf ' .p2align 7,0xcc\n .fill %d,1,0xcc\n .p2align 7\n' $k
    done > $tmp/nops.s
    echo ' ret' >> $tmp/nops.s
    $mc -c -o $tmp/nops.o $tmp/nops.s &&
    $objdump -d --no-show-raw-insn $tmp/nops.o | grep -E '^ +[0-9a-f]+:' |
        grep -vE '\s(int3|nop[wl]?|ret|data16|cs nopw|xchg +%ax,%ax)\b' > $tmp/nops.left
    # What's left are the jumps, each to an address aligned to 128.
    if grep -vE 'jmp +(0x)?[0-9a-f]*[08]0( |$)' $tmp/nops.left | grep -q .; then
        echo "testing asm NOP padding ... failed"; grep -vE 'jmp +(0x)?[0-9a-f]*[08]0( |$)' $tmp/nops.left | head; exit 1
    fi
    echo "testing asm NOP padding ... passed"

    # musl's x86-64 assembly, which the bundled C library needs (its math
    # overrides are left out; mucc builds musl's C versions instead).
    local n=0
    for s in thirdparty/musl/crt/x86_64/*.s thirdparty/musl/src/*/x86_64/*.s; do
        case $s in */math/*) continue;; esac
        check $s $s
        n=$((n+1))
    done
    # crtn.s has no symbols, so, as with GNU as, no .symtab either.
    $mc -c -o $tmp/crtn.o thirdparty/musl/crt/x86_64/crtn.s
    if readelf -S $tmp/crtn.o | grep -q symtab; then
        echo "testing asm: crtn.s has a .symtab ... failed"; exit 1
    fi
    echo "testing asm: $n musl files assemble the same as with GNU as ... passed"

    check_programs
}

aarch64_tests() {
    check test/asm-forms-arm64.s asm-forms-arm64.s
    echo "testing asm asm-forms-arm64.s ... passed"

    # musl's ARM64 assembly (but its string functions, NEON in .S files,
    # which aren't built)
    local n=0
    for s in thirdparty/musl/crt/aarch64/*.s thirdparty/musl/src/*/aarch64/*.s; do
        check $s $s
        n=$((n+1))
    done
    echo "testing asm: $n musl ARM64 files assemble the same as with GNU as ... passed"

    # A conditional branch too far for it (1 MiB, 32 KiB for tbz) is the
    # opposite branch over a b, which GNU as doesn't make.
    printf ' b.ne 1f\n cbz x0, 1f\n tbz w1, 3, 1f\n .fill 262200, 4, 0xd503201f\n1:\n ret\n' > $tmp/far.s
    $mc -c -o $tmp/far.o $tmp/far.s &&
    $objdump -d --no-show-raw-insn $tmp/far.o | grep -E '^ +[0-9a-f]+:' |
        awk '{ print $2, $3, $4 }' | head -6 | tr '\n' ';' > $tmp/far.out
    local far="b.eq 0x8 //;b 0x1000f8 ;cbnz x0, 0x10;b 0x1000f8 ;tbnz w1, #3,;b 0x1000f8 ;"
    if [ "$(cat $tmp/far.out)" != "$far" ]; then
        echo "testing asm far conditional branches ... failed"; cat $tmp/far.out; exit 1
    fi
    echo "testing asm far conditional branches ... passed"

    check_programs
    check_programs -g
}

for t in x86_64 aarch64; do
    as=$(tool $t as)
    objdump=$(tool $t objdump)
    if [ -z "$as" ] || [ -z "$objdump" ]; then
        echo "testing asm for $t ... skipped (no GNU as for it)"
        continue
    fi
    mc="$mucc --libc=system --target=$t-linux"
    ${t}_tests
done
