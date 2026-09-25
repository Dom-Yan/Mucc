#!/bin/bash
# Checks mucc's archiver (src/ar.c): every archive it writes must be the
# same, byte for byte, as GNU ar's deterministic one (ar D) after the same
# operations, and must link, with mucc's linker and with ld, and list the
# same. Needs GNU ar to compare with.
mucc=$1

if ! command -v ar > /dev/null; then
    echo "test/ar.sh: skipped (needs GNU ar)"
    exit 0
fi

tmp=`mktemp -d /tmp/mucc-ar-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing ar $1 ... passed"
    else
        echo "testing ar $1 ... failed"
        exit 1
    fi
}

# Runs the same ar operation on $tmp/m.a with mucc and on $tmp/g.a with GNU
# ar, and compares the archives.
both() {
    local op=$1
    shift
    $mucc -ar $op $tmp/m.a "$@" &&
        ar ${op}D $tmp/g.a "$@" 2> /dev/null &&
        cmp $tmp/m.a $tmp/g.a
}

cd $tmp
echo 'int foo(void) { return 40; } __attribute__((weak)) int bar(void) { return 1; } int common_var;' > a.c
echo 'int a_function_with_a_long_name(void) { return 2; }' > member_with_a_long_name.c
echo 'static int hidden(void) { return 0; } int baz = 5;' > b.c
printf 'odd' > notes.txt
for f in a member_with_a_long_name b; do
    $OLDPWD/$mucc -c -o $f.o $f.c || exit 1
done
echo 'int foo(void); int a_function_with_a_long_name(void); extern int baz;
int main(void) { return foo() + a_function_with_a_long_name() - baz; }' > main.c
cd $OLDPWD

both rcs $tmp/a.o $tmp/member_with_a_long_name.o $tmp/notes.txt $tmp/b.o
check 'rcs: the same as GNU ar'

[ "$($mucc -ar t $tmp/m.a)" = "$(ar t $tmp/g.a)" ]
check 't: the same as ar t'

[ "$(nm -s $tmp/m.a 2> /dev/null)" = "$(nm -s $tmp/g.a 2> /dev/null)" ]
check 'the symbol index nm -s shows'

$mucc -o $tmp/prog $tmp/main.c $tmp/m.a && $tmp/prog
[ $? = 37 ]
check 'linked with ld'

$mucc -static -o $tmp/prog $tmp/main.c $tmp/m.a 2> $tmp/err && $tmp/prog
[ $? = 37 ] && ! grep -q 'system linker' $tmp/err
check 'linked with mucc'"'"'s linker'

echo 'int foo(void) { return 50; } int bar(void) { return 2; }' > $tmp/a.c
$mucc -c -o $tmp/a.o $tmp/a.c
both r $tmp/a.o
check 'r replaces a member'

$mucc -o $tmp/prog $tmp/main.c $tmp/m.a && $tmp/prog
[ $? = 47 ]
check 'the replaced member is linked'

both q $tmp/b.o
check 'q appends'

both d $tmp/notes.txt $tmp/b.o
check 'd deletes'

rm -f $tmp/m.a $tmp/g.a
$mucc -ar rcS $tmp/m.a $tmp/a.o $tmp/b.o && ar rcSD $tmp/g.a $tmp/a.o $tmp/b.o &&
    cmp $tmp/m.a $tmp/g.a
check 'S: no symbol index'

$mucc -ranlib $tmp/m.a && ar sD $tmp/g.a && cmp $tmp/m.a $tmp/g.a
check '-ranlib'

rm -f $tmp/m.a
(umask 022; $mucc -ar rc $tmp/m.a $tmp/a.o) && [ "$(stat -c %a $tmp/m.a)" = 644 ]
check 'a new archive'"'"'s mode'

$mucc -ar rcx $tmp/m.a $tmp/a.o 2>&1 | grep -q "unknown option 'x'"
check 'unknown option'
