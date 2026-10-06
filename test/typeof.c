#include "test.h"

int main() {
  ASSERT(3, ({ typeof(int) x=3; x; }));
  ASSERT(3, ({ typeof(1) x=3; x; }));
  ASSERT(4, ({ int x; typeof(x) y; sizeof(y); }));
  ASSERT(8, ({ int x; typeof(&x) y; sizeof(y); }));
  ASSERT(4, ({ typeof("foo") x; sizeof(x); }));
  ASSERT(12, sizeof(typeof(struct { int a,b,c; })));

  // Arithmetic on qualified operands has an unqualified type.
  ASSERT(1, ({ const int ci = 1; _Generic(&(typeof(ci + ci)){0}, int *: 1, default: 2); }));
  ASSERT(1, ({ const int ci = 1; _Generic(&(typeof(-ci)){0}, int *: 1, default: 2); }));
  ASSERT(1, ({ volatile double vd = 1; _Generic(&(typeof(vd * vd)){0}, double *: 1, default: 2); }));
  ASSERT(1, ({ _Atomic long al = 1; _Generic(&(typeof(al - al)){0}, long *: 1, default: 2); }));
  ASSERT(7, ({ const int ci = 3; typeof(ci + 1) x = 0; x = 7; x; }));

  printf("OK\n");
  return 0;
}
