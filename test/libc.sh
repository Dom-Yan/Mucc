#!/bin/bash
# Builds the bundled C library (make libc) with gcc, as, ld and ar
# replaced by commands that fail: mucc compiles, assembles and archives
# all of musl itself. Then checks that a program built against it runs.
# (It is linked by ld until mucc does that itself; see PLAN.md, 3.5.)

tmp=`mktemp -d /tmp/mucc-libc-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing libc $1 ... passed"
    else
        echo "testing libc $1 ... failed"
        exit 1
    fi
}

mkdir $tmp/bin
for t in gcc cc c99 as ld ar ranlib; do
    printf '#!/bin/sh\necho "%s $*" >> %s/calls\nexit 1\n' $t $tmp > $tmp/bin/$t
    chmod +x $tmp/bin/$t
done
PATH=$tmp/bin:$PATH make -s libc > $tmp/log 2>&1 && [ ! -s $tmp/calls ]
check 'make libc with no gcc, as, ld or ar'

B=build/musl
make -s -C $B install-headers DESTDIR= includedir=$PWD/$B/include > /dev/null
cat > $tmp/prog.c <<'EOF'
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *thread(void *arg) { return (void *)(long)(strlen(arg) * 2); }
static int cmp(const void *a, const void *b) { return *(int *)a - *(int *)b; }

int main(void) {
  int v[] = {5, 3, 9, 1, 7};
  qsort(v, 5, sizeof(int), cmp);
  char *s = malloc(64);
  snprintf(s, 64, "%d%d%d%d%d %.3f %Lg", v[0], v[1], v[2], v[3], v[4], sqrt(2.0), 1.5L);
  pthread_t t;
  void *r;
  pthread_create(&t, 0, thread, "abcd");
  pthread_join(t, &r);
  jmp_buf jb;
  volatile int n = 0;
  if (setjmp(jb) < 3) {
    n++;
    longjmp(jb, n);
  }
  printf("%s %ld %d %g\n", s, (long)r, n, exp(1.0));
  free(s);
  return 0;
}
EOF
./mucc -I$B/include -c -o $tmp/prog.o $tmp/prog.c &&
    ld -static -o $tmp/prog $B/lib/crt1.o $B/lib/crti.o $tmp/prog.o $B/lib/libc.a $B/lib/crtn.o &&
    [ "$($tmp/prog)" = "13579 1.414 1.5 8 3 2.71828" ]
check 'a program built against it runs'
