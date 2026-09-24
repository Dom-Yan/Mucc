#!/bin/bash
# Checks mucc's built-in static linker (src/link.c): every test program
# linked with -static must run and pass, without falling back to `ld`;
# archives, common symbols, -s and the error messages must work; and
# mucc must be able to link itself statically.
#
# Like test/errors.sh, point at the repo's include/ so this also works for
# stage2/mucc, which has no include/ next to it.
mucc="$1 -Iinclude"

tmp=`mktemp -d /tmp/mucc-link-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing link $1 ... passed"
    else
        echo "testing link $1 ... failed"
        exit 1
    fi
}

# Links without falling back to `ld` (which would print a note).
static_link() {
    $mucc -static "$@" 2> $tmp/err
    local rc=$?
    if grep -q 'using the system linker' $tmp/err; then
        cat $tmp/err
        return 1
    fi
    return $rc
}

$mucc -c -o $tmp/common.o -xc test/common || exit 1
n=0
for f in test/*.c; do
    b=$(basename $f .c)
    static_link -Itest $(sed -n 's|^// flags: ||p' $f) -o $tmp/$b $f $tmp/common.o &&
        $tmp/$b > $tmp/out 2>&1 && tail -1 $tmp/out | grep -q '^OK$'
    check "$b (static)"
    n=$((n+1))
done

# Thread-local variables and threads, statically
cat > $tmp/tls.c <<'EOF'
#include <pthread.h>
_Thread_local int tl = 10;
static void *worker(void *arg) { tl += (long)arg; return (void *)(long)tl; }
int main(void) {
  pthread_t t;
  void *ret;
  pthread_create(&t, 0, worker, (void *)5);
  pthread_join(t, &ret);
  return (long)ret == 15 && tl == 10 ? 0 : 1;
}
EOF
static_link -o $tmp/tls $tmp/tls.c -lpthread && $tmp/tls
check 'thread-local variables in threads'

# An archive of our own, found with -L/-l; only needed members are used.
printf 'int counter;\nint bump(void) { return ++counter; }\n' > $tmp/a1.c
printf 'int twice(int x) { return 2 * x; }\n' > $tmp/a2.c
printf 'int never_used(void) { return 99; }\n' > $tmp/a3.c
for i in 1 2 3; do $mucc -c -o $tmp/a$i.o $tmp/a$i.c; done
mkdir -p $tmp/lib && ar rcs $tmp/lib/libmine.a $tmp/a1.o $tmp/a2.o $tmp/a3.o
printf 'int counter;\nint bump(void);\nint twice(int);\nint main(void) { bump(); bump(); return twice(counter) == 4 ? 0 : 1; }\n' > $tmp/m.c
static_link -o $tmp/m $tmp/m.c -L$tmp/lib -lmine && $tmp/m && ! nm $tmp/m | grep -q never_used
check 'archive with -L and -l, common symbols'

static_link -s -o $tmp/ms $tmp/m.c -L$tmp/lib -lmine && $tmp/ms && ! nm $tmp/ms 2>/dev/null | grep -q main
check '-s'

# Errors
printf 'int missing(void);\nint main(void) { return missing(); }\n' > $tmp/u.c
! $mucc -static -o $tmp/u $tmp/u.c 2> $tmp/err && grep -q "undefined reference to 'missing'" $tmp/err
check 'undefined reference'

printf 'int f(void) { return 1; }\n' > $tmp/d1.c
printf 'int f(void) { return 2; }\nint main(void) { return f(); }\n' > $tmp/d2.c
! $mucc -static -o $tmp/d $tmp/d1.c $tmp/d2.c 2> $tmp/err &&
    grep -q "multiple definition of 'f' (in $tmp/d1.c and $tmp/d2.c)" $tmp/err
check 'multiple definition'

# mucc links itself statically, and the result works.
for f in src/*.c; do $mucc -c -o $tmp/src_$(basename $f .c).o $f || exit 1; done
static_link -o $tmp/mucc-static $tmp/src_*.o &&
    echo 'int main() { return 42; }' > $tmp/fortytwo.c &&
    $tmp/mucc-static -Iinclude -static -o $tmp/fortytwo $tmp/fortytwo.c
$tmp/fortytwo
[ $? = 42 ]
check 'mucc links itself'

echo "testing link: $n test programs and the cases above ... passed"
