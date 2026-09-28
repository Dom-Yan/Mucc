#!/bin/bash
# The single binary, build/mucc (see the Makefile), copied alone into an
# empty directory, with gcc, cc, as, ld and ar replaced by commands that
# fail: it still compiles and links programs, from the headers and C
# library inside it.
binary=$1

tmp=`mktemp -d /tmp/mucc-single-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing single binary $1 ... passed"
    else
        echo "testing single binary $1 ... failed"
        exit 1
    fi
}

mkdir $tmp/alone $tmp/bin
cp $binary $tmp/alone/mucc
for t in gcc cc as ld ar; do
    printf '#!/bin/sh\necho "%s $*" >> %s/calls\nexit 1\n' $t $tmp > $tmp/bin/$t
    chmod +x $tmp/bin/$t
done
mucc() { PATH="$tmp/bin:$PATH" $tmp/alone/mucc "$@"; }

cat > $tmp/prog.c <<'EOF'
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static void *thread(void *arg) { return arg; }

int main(void) {
  pthread_t t;
  void *r;
  pthread_create(&t, 0, thread, (void *)7);
  pthread_join(t, &r);
  bool b = true;
  printf("%ld %.4f %d %zu\n", (long)r, sqrt(2.0), b,
         offsetof(struct { char c; int i; }, i));
  return 0;
}
EOF
mucc --libc=mucc -o $tmp/prog $tmp/prog.c -lm -lpthread &&
    [ "$($tmp/prog)" = "7 1.4142 1 4" ] && [ ! -s $tmp/calls ] &&
    file $tmp/prog | grep -q 'statically linked'
check 'builds a program with --libc=mucc'

# Its own headers are inside it too, with the system's C library.
echo '#include <stdbool.h>
#include <stdio.h>
bool f(void) { return puts("x") > 0; }' > $tmp/sys.c
mucc -c -o $tmp/sys.o $tmp/sys.c
check 'compiles with --libc=system'

# Embedded headers are no files make could find, so -M leaves them out.
[ "$(mucc --libc=mucc -M $tmp/prog.c | grep -c '<mucc>')" = 0 ]
check '-M'
