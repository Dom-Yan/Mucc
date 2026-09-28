#include "test.h"
#include <stdarg.h>

int sum1(int x, ...) {
  va_list ap;
  va_start(ap, x);

  for (;;) {
    int y = va_arg(ap, int);
    if (y == 0)
      return x;
    x += y;
  }
}

int sum2(int x, ...) {
  va_list ap;
  va_start(ap, x);

  for (;;) {
    double y = va_arg(ap, double);
    x += y;

    int z = va_arg(ap, int);
    if (z == 0)
      return x;
    x += z;
  }
}

void fmt(char *buf, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  va_list ap2;
  va_copy(ap2, ap);
  vsprintf(buf, fmt, ap2);
  va_end(buf);
}

// A va_list goes to the C library as the psABI lays it out: doubles
// after the named ones, past the 8 XMM registers.
void fmt_doubles(char *buf, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);
  va_end(ap);
}

typedef struct { int a, b; } VaS8;
typedef struct { long a, b; } VaS16;
typedef struct { double x; long n; } VaMixed;
typedef struct { double x, y; } VaDoubles;
typedef struct { float x, y, z; } VaFloats;
typedef struct { long a, b, c; } VaS24;

// Each argument is a kind (0 to 8), then a value of that kind: va_arg
// finds it where the calling convention put it, in a register or on the
// stack. Returns the sum of everything.
double va_kinds(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double sum = 0;
  for (int i = 0; i < n; i++) {
    switch (va_arg(ap, int)) {
    case 0: sum += va_arg(ap, int); break;
    case 1: sum += va_arg(ap, double); break;
    case 2: sum += va_arg(ap, long double); break;
    case 3: { VaS8 s = va_arg(ap, VaS8); sum += s.a + s.b; break; }
    case 4: { VaS16 s = va_arg(ap, VaS16); sum += s.a + s.b; break; }
    case 5: { VaMixed s = va_arg(ap, VaMixed); sum += s.x + s.n; break; }
    case 6: { VaDoubles s = va_arg(ap, VaDoubles); sum += s.x + s.y; break; }
    case 7: { VaFloats s = va_arg(ap, VaFloats); sum += s.x + s.y + s.z; break; }
    case 8: { VaS24 s = va_arg(ap, VaS24); sum += s.a + s.b + s.c; break; }
    }
  }
  va_end(ap);
  return sum;
}

double call_va_kinds(double (*fn)(int, ...));

// Named arguments on the stack come before the variadic ones there.
long va_after_stack(long a, long b, long c, long d, long e, long f, long g, ...) {
  va_list ap;
  va_start(ap, g);
  long x = va_arg(ap, long);
  va_end(ap);
  return g * 10 + x;
}

int main() {
  VaS8 s8 = {1, 2};
  VaS16 s16 = {3, 4};
  VaMixed sm = {5.5, 6};
  VaDoubles sd = {7.5, 8.5};
  VaFloats sf = {1.25f, 2.5f, 3.75f};
  VaS24 s24 = {9, 10, 11};
  ASSERT(0, ({ char buf[100]; fmt_doubles(buf, "%g %g %g %d %g", 1.5, 2.5, 3.5, 7, 4.5); strcmp(buf, "1.5 2.5 3.5 7 4.5"); }));
  ASSERT(0, ({ char buf[100]; fmt_doubles(buf, "%g %g %g %g %g %g %g %g %g %g", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0); strcmp(buf, "1 2 3 4 5 6 7 8 9 10"); }));
  // (Sums of 118.75 and 89, times 4 to be whole.)
  ASSERT(475, 4 * va_kinds(9, 0, 42, 1, 0.5, 2, 1.25L, 3, s8, 4, s16, 5, sm, 6, sd, 7, sf, 8, s24));
  ASSERT(356, 4 * va_kinds(12, 1, 1.0, 1, 2.0, 1, 3.0, 1, 4.0, 6, sd, 6, sd, 6, sd, 4, s16, 5, sm, 3, s8, 0, 7, 2, 2.5L));
  ASSERT(78, va_after_stack(1, 2, 3, 4, 5, 6, 7, 8L));
  ASSERT(346, 4 * call_va_kinds(va_kinds)); // called by gcc's code (test/common)

  ASSERT(6, sum1(1, 2, 3, 0));
  ASSERT(55, sum1(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0));
  ASSERT(21, sum2(1, 2.0, 3, 4.0, 5, 6.0, 0));
  ASSERT(21, sum2(1, 2.0, 3, 4.0, 5, 6.0, 0));
  ASSERT(210, sum2(1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, 9, 10.0, 11, 12.0, 13, 14.0, 15, 16.0, 17, 18.0, 19, 20.0, 0));
  ASSERT(0, ({ char buf[100]; fmt(buf, "%d %d", 2, 3); strcmp(buf, "2 3"); }));

  printf("OK\n");
  return 0;
}
