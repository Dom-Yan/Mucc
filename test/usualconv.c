#include "test.h"

static int ret10(void) { return 10; }

int main() {
  ASSERT((long)-5, -10 + (long)5);
  ASSERT((long)-15, -10 - (long)5);
  ASSERT((long)-50, -10 * (long)5);
  ASSERT((long)-2, -10 / (long)5);

  ASSERT(1, -2 < (long)-1);
  ASSERT(1, -2 <= (long)-1);
  ASSERT(0, -2 > (long)-1);
  ASSERT(0, -2 >= (long)-1);

  ASSERT(1, (long)-2 < -1);
  ASSERT(1, (long)-2 <= -1);
  ASSERT(0, (long)-2 > -1);
  ASSERT(0, (long)-2 >= -1);

  ASSERT(0, 2147483647 + 2147483647 + 2);
  ASSERT((long)-1, ({ long x; x=-1; x; }));

  ASSERT(1, ({ char x[3]; x[0]=0; x[1]=1; x[2]=2; char *y=x+1; y[0]; }));
  ASSERT(0, ({ char x[3]; x[0]=0; x[1]=1; x[2]=2; char *y=x+1; y[-1]; }));
  ASSERT(5, ({ struct t {char a;} x, y; x.a=5; y=x; y.a; }));

  ASSERT(10, (1 ? ret10 : (void *)0)());

  // `c ? a : b` with pointers: a null pointer constant takes the other's
  // type, void * wins over other pointers, and qualifiers add up.
  ASSERT(8, ({ int x = 5, *p = &x; sizeof(1 ? 0 : p); }));
  ASSERT(4, ({ int x = 5, *p = &x; sizeof(*(1 ? 0 : p)); }));
  ASSERT(4, ({ int x = 5, *p = &x; sizeof(*(1 ? (void *)0 : p)); }));
  ASSERT(4, ({ int x = 5, *p = &x; sizeof(*(1 ? (void *)(1 - 1) : p)); }));
  ASSERT(1, ({ int x = 5, *p = &x; sizeof(*(1 ? (void *)1 : p)); }));
  ASSERT(1, ({ int x = 5, *p = &x; void *v = p; sizeof(*(0 ? p : v)); }));
  ASSERT(1, ({ char s[] = "hi"; sizeof(*(1 ? 0 : s)); }));
  ASSERT(5, ({ int x = 5, *p = &x; *(0 ? 0 : p); }));
  ASSERT(1, ({ int x = 5, *p = &x; const int *cp = p; _Generic(1 ? p : cp, const int *: 1, default: 2); }));
  ASSERT(1, ({ int x = 5, *p = &x; const void *cv = p; _Generic(1 ? p : cv, const void *: 1, default: 2); }));
  ASSERT(1, ({ int x = 5, *p = &x; void *v = p; _Generic(p ?: v, void *: 1, default: 2); }));

  // musl's <tgmath.h> picks its result types that way.
  ASSERT(8, sizeof(*(0 ? (double *)0 : (void *)!(sizeof(1.0) == 8))));
  ASSERT(1, sizeof(*(0 ? (double *)0 : (void *)!(sizeof(1.0) == 4))));

  printf("OK\n");
  return 0;
}
