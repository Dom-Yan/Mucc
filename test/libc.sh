#!/bin/bash
# Builds the bundled C library (make libc) with gcc, as, ld and ar
# replaced by commands that fail: mucc compiles, assembles and archives
# all of musl itself. Then checks that a program built against it with
# --libc=mucc, still with no ld, runs.

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
# musl's configure asks whether the linker takes flags for a libc.so
# (`-nostdlib -shared ... -o /dev/null`), which isn't built. Where there's
# no ld, the answer is no, as here, so those calls don't count.
PATH=$tmp/bin:$PATH make -s libc > $tmp/log 2>&1 &&
    ! grep -v -E '^ld -o /dev/null .* -shared( |$)' $tmp/calls 2> /dev/null
check 'make libc with no gcc, as, ld or ar'
rm -f $tmp/calls

cat > $tmp/prog.c <<'EOF'
#include <complex.h>
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
  double complex z = cexp(I * 0.5) * 2;
  printf("%s %ld %d %g %g %.3f\n", s, (long)r, n, exp(1.0), cabs(z), cimag(z));
  free(s);
  return 0;
}
EOF
PATH=$tmp/bin:$PATH ./mucc --libc=mucc -o $tmp/prog $tmp/prog.c -lm -lpthread &&
    [ ! -s $tmp/calls ] && file $tmp/prog | grep -q 'statically linked' &&
    [ "$($tmp/prog)" = "13579 1.414 1.5 8 3 2.71828 2 0.959" ]
check 'a program built against it runs'
