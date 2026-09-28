#include "test.h"
#include <float.h>

// Global initializers are folded at compile time, each step in its own
// type, and long double ones keep all 64 bits of mantissa.
static const long double g1 = 0.0416666666666666666136L;
static const long double g2[] = {1.0L / 3, 0.1L + 0.2L, 1e4000L};
struct { int i; long double x; } g3 = {1, 3.14159265358979323846264L};
long double g4 = 18446744073709551615UL;
float g5 = 16777216.0f + 1.0f;
float g6 = 0.1f + 0.2f;
double g7 = 0.1f;

// The x87 stack has 8 registers and must be empty at a call: nesting
// deeper than that, a call in the middle, and assignments used as values
// or thrown away (which must not leave values on it).
static long double ld_twice(long double x) { return x * 2; }
static long double ld_deep(long double z) {
  return z * (1 + z * (2 + z * (3 + z * (4 + z * (5 + z * (6 + z * (7 + z * (8 + z * (9 + z * 10)))))))));
}
static long double ld_balance(void) {
  long double a = 1, b = 0, c = 0;
  for (int i = 0; i < 20; i++) {
    b = a;
    c = b = a + 1;
    ld_twice(a);
    (void)ld_twice(b);
    (a, b);
    a + b;
    b += (c /= 2);
  }
  return a + b + c;
}

// Floating-point exception flags: "invalid" in MXCSR and the x87 status
// word
static void clear_invalid(void) {
  unsigned csr;
  asm volatile("stmxcsr %0" : "=m"(csr));
  csr &= ~0x3f;
  asm volatile("ldmxcsr %0" : : "m"(csr));
  asm volatile("fnclex");
}
static int invalid_raised(void) {
  unsigned csr;
  unsigned short sw;
  asm volatile("stmxcsr %0" : "=m"(csr));
  asm volatile("fnstsw %0" : "=a"(sw));
  return (csr | sw) & 1;
}

int main() {
  ASSERT(1, ld_deep(0.5L) == 0x1.fdp+0L);
  ASSERT(1, ld_twice(3) * ld_twice(4) + ld_twice(5) / (ld_twice(1) - 1) == 58);
  ASSERT(1, ld_balance() == 5);
  ASSERT(1, ({ long double s = 1, t = (s /= 16); t == 0x1p-4L && s == t; }));
  ASSERT(0, ({ clear_invalid(); ld_balance(); invalid_raised(); }));

  // A NaN is true, unequal to everything, and unordered: == and != are
  // quiet, < and <= raise "invalid", as with gcc.
  ASSERT(1, ({ volatile float x = 0.0f / 0.0f; !!x + (x != x) + !(x == x) + !(x < 0) + (_Bool)x == 5; }));
  ASSERT(1, ({ volatile double x = 0.0 / 0.0; !!x + (x != x) + !(x == x) + !(x < 0) + (x ? 1 : 0) == 5; }));
  ASSERT(1, ({ volatile long double x = 0.0L / 0.0L; !!x + (x != x) + !(x == x) + !(x < 0) + (x || 0) == 5; }));
  ASSERT(0, ({ volatile double x = 0.0 / 0.0; clear_invalid(); (void)(x == x); invalid_raised(); }));
  ASSERT(1, ({ volatile double x = 0.0 / 0.0; clear_invalid(); (void)(x < 0); invalid_raised(); }));
  ASSERT(0, ({ volatile long double x = 0.0L / 0.0L; clear_invalid(); (void)(x != x); invalid_raised(); }));
  ASSERT(1, ({ volatile long double x = 0.0L / 0.0L; clear_invalid(); (void)(x <= 0); invalid_raised(); }));

  // <float.h>: long double is x87 extended precision
  ASSERT(64, LDBL_MANT_DIG);
  ASSERT(16384, LDBL_MAX_EXP);
  ASSERT(1, ({ volatile long double one = 1; one + LDBL_EPSILON > one && one + LDBL_EPSILON / 2 == one; }));
  ASSERT(1, ({ volatile long double m = LDBL_MAX; m * 2 > m && LDBL_MAX > DBL_MAX; }));

  long double one = 1, three = 3, big = 18446744073709551615UL;
  float f1 = 0.1f, f2 = 0.2f;
  ASSERT(1, g1 == 0.0416666666666666666136L);
  ASSERT(1, g1 != (double)0.0416666666666666666136L);
  ASSERT(1, g2[0] == one / three);
  ASSERT(1, g2[1] == 0.1L + 0.2L);
  ASSERT(1, g2[2] == 1e4000L);
  ASSERT(1, g3.x == 3.14159265358979323846264L);
  ASSERT(1, g4 == big);
  ASSERT(1, g5 == 16777216.0f);
  ASSERT(1, g6 == f1 + f2);
  ASSERT(1, g7 == (double)f1);

  ASSERT(35, (float)(char)35);
  ASSERT(35, (float)(short)35);
  ASSERT(35, (float)(int)35);
  ASSERT(35, (float)(long)35);
  ASSERT(35, (float)(unsigned char)35);
  ASSERT(35, (float)(unsigned short)35);
  ASSERT(35, (float)(unsigned int)35);
  ASSERT(35, (float)(unsigned long)35);

  ASSERT(35, (double)(char)35);
  ASSERT(35, (double)(short)35);
  ASSERT(35, (double)(int)35);
  ASSERT(35, (double)(long)35);
  ASSERT(35, (double)(unsigned char)35);
  ASSERT(35, (double)(unsigned short)35);
  ASSERT(35, (double)(unsigned int)35);
  ASSERT(35, (double)(unsigned long)35);

  ASSERT(35, (char)(float)35);
  ASSERT(35, (short)(float)35);
  ASSERT(35, (int)(float)35);
  ASSERT(35, (long)(float)35);
  ASSERT(35, (unsigned char)(float)35);
  ASSERT(35, (unsigned short)(float)35);
  ASSERT(35, (unsigned int)(float)35);
  ASSERT(35, (unsigned long)(float)35);

  ASSERT(35, (char)(double)35);
  ASSERT(35, (short)(double)35);
  ASSERT(35, (int)(double)35);
  ASSERT(35, (long)(double)35);
  ASSERT(35, (unsigned char)(double)35);
  ASSERT(35, (unsigned short)(double)35);
  ASSERT(35, (unsigned int)(double)35);
  ASSERT(35, (unsigned long)(double)35);

  ASSERT(-2147483648, (double)(unsigned long)(long)-1);

  ASSERT(1, 2e3==2e3);
  ASSERT(0, 2e3==2e5);
  ASSERT(1, 2.0==2);
  ASSERT(0, 5.1<5);
  ASSERT(0, 5.0<5);
  ASSERT(1, 4.9<5);
  ASSERT(0, 5.1<=5);
  ASSERT(1, 5.0<=5);
  ASSERT(1, 4.9<=5);

  ASSERT(1, 2e3f==2e3);
  ASSERT(0, 2e3f==2e5);
  ASSERT(1, 2.0f==2);
  ASSERT(0, 5.1f<5);
  ASSERT(0, 5.0f<5);
  ASSERT(1, 4.9f<5);
  ASSERT(0, 5.1f<=5);
  ASSERT(1, 5.0f<=5);
  ASSERT(1, 4.9f<=5);

  ASSERT(6, 2.3+3.8);
  ASSERT(-1, 2.3-3.8);
  ASSERT(-3, -3.8);
  ASSERT(13, 3.3*4);
  ASSERT(2, 5.0/2);

  ASSERT(6, 2.3f+3.8f);
  ASSERT(6, 2.3f+3.8);
  ASSERT(-1, 2.3f-3.8);
  ASSERT(-3, -3.8f);
  ASSERT(13, 3.3f*4);
  ASSERT(2, 5.0f/2);

  ASSERT(0, 0.0/0.0 == 0.0/0.0);
  ASSERT(1, 0.0/0.0 != 0.0/0.0);

  ASSERT(0, 0.0/0.0 < 0);
  ASSERT(0, 0.0/0.0 <= 0);
  ASSERT(0, 0.0/0.0 > 0);
  ASSERT(0, 0.0/0.0 >= 0);

  ASSERT(0, !3.);
  ASSERT(1, !0.);
  ASSERT(0, !3.f);
  ASSERT(1, !0.f);

  ASSERT(5, 0.0 ? 3 : 5);
  ASSERT(3, 1.2 ? 3 : 5);

  printf("OK\n");
  return 0;
}
