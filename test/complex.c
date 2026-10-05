#include "test.h"
#include <complex.h>
#include <stdarg.h>

typedef _Complex float cf;
typedef _Complex double cd;
typedef _Complex long double cl;

// Equal, with the sign of a zero and NaN equal to NaN
static int same(long double x, long double y) {
  if (x != x)
    return y != y;
  return x == y && __builtin_signbit(x) == __builtin_signbit(y);
}

static int is(cd z, double re, double im) {
  return same(__real__ z, re) && same(__imag__ z, im);
}

static int isf(cf z, float re, float im) {
  return same(__real__ z, re) && same(__imag__ z, im);
}

static int isl(cl z, long double re, long double im) {
  return same(__real__ z, re) && same(__imag__ z, im);
}

// Passing and returning: in SSE registers (two parts of a float complex
// share one), on the stack once they run out, and a long double complex
// returned in %st0 and %st1
static cf add_f(cf a, float x, cf b) { return a + b + x; }
static cd many(cd a, cd b, cd c, cd d, cd e, int n, cd f) { return a + b + c + d + e + f * n; }
static cl add_l(cl a, cl b) { return a + b; }
static cd from_real(double x) { return x; }
static cd narrow(cl z) { return z; }

static cd va_sum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  cd s = 0;
  for (int i = 0; i < n; i++)
    s += va_arg(ap, cd);
  va_end(ap);
  return s;
}

struct S { char c; cd z; cf w; };

cd g1 = 1.0 + 2.0i;
cf g2 = 3;
cl g3 = (1.0 + 2.0i) * (3.0 - 1.0i);
cd g4 = __builtin_complex(5.0, -6.0);
cd g5[] = {1.0i, 2, -3.0 - 4.0i};
cd g6 = {7.0i};
struct S g7 = {1, 2.0i, 3};
double g8 = (double)(4.0 + 5.0i);
int g9 = (3.9 + 1.0i) * 2;
float g10 = 2.5 + 1.0i;
unsigned long g11 = (cd)1e19;

int main() {
  ASSERT(8, sizeof(cf));
  ASSERT(16, sizeof(cd));
  ASSERT(32, sizeof(cl));
  ASSERT(4, _Alignof(cf));
  ASSERT(8, _Alignof(cd));
  ASSERT(16, _Alignof(cl));
  ASSERT(16, sizeof(_Complex));
  ASSERT(16, sizeof(__complex__ double));
  ASSERT(32, sizeof(long double _Complex));
  ASSERT(8, __builtin_offsetof(struct S, z));
  ASSERT(1, _Generic(1.0if, cf: 1, default: 0));
  ASSERT(1, _Generic(1.0i, cd: 1, default: 0));
  ASSERT(1, _Generic(1.0il, cl: 1, default: 0));
  ASSERT(1, _Generic((cf)1 + 1.0, cd: 1, default: 0));
  ASSERT(1, _Generic((cf)1 + 1, cf: 1, default: 0));

  // Globals
  ASSERT(1, is(g1, 1, 2));
  ASSERT(1, isf(g2, 3, 0));
  ASSERT(1, isl(g3, 5, 5));
  ASSERT(1, is(g4, 5, -6));
  ASSERT(1, is(g5[0], 0, 1) && is(g5[1], 2, 0) && is(g5[2], -3, -4));
  ASSERT(1, is(g6, 0, 7));
  ASSERT(1, g7.c == 1 && is(g7.z, 0, 2) && isf(g7.w, 3, 0));
  ASSERT(1, g8 == 4);
  ASSERT(7, g9);
  ASSERT(1, g10 == 2.5);
  ASSERT(1, g11 == 10000000000000000000UL);

  // I, imaginary constants, __builtin_complex, __real__ and __imag__
  ASSERT(1, is(2.0 + 3.0 * I, 2, 3));
  ASSERT(1, isf(1.5fi, 0, 1.5));
  ASSERT(1, isf(1.5if, 0, 1.5));
  ASSERT(1, is(__builtin_complex(-0.0, 2.0), -0.0, 2));
  ASSERT(1, ({ cd z = 3 + 4.0i; __real__ z == 3 && __imag__ z == 4; }));
  ASSERT(1, ({ cd z = 3 + 4.0i; __real__ z = 7; __imag__ z *= 2; is(z, 7, 8); }));
  ASSERT(1, ({ cd z = 1; double *p = &__imag__ z; *p = 9; is(z, 1, 9); }));
  ASSERT(5, ({ int x = 5; __real__ x; }));
  ASSERT(0, ({ int x = 5; __imag__ x; }));

  // Conversions: a real number has 0 as its imaginary part, a complex
  // number as a real one is its real part, and as a bool, nonzero if
  // either part is
  ASSERT(1, is((cd)3, 3, 0));
  ASSERT(1, is((cd)(cf)(1.5 + 2.5i), 1.5, 2.5));
  ASSERT(1, isl((cl)(1.0 + 2.0i), 1, 2));
  ASSERT(3, (int)(3.9 + 4.0i));
  ASSERT(1, (double)(2.5 - 1.0i) == 2.5);
  ASSERT(1, (_Bool)(0.0 + 1.0i));
  ASSERT(0, (_Bool)(0.0 + 0.0i));
  ASSERT(1, !(cd)0);
  ASSERT(1, (cd)2.0i ? 1 : 0);
  ASSERT(1, ({ int r = 0; if (1.0i) r = 1; r; }));
  ASSERT(1, ({ int r = 0; cd z = 0; while (z) r = 2; r = 1; r; }));
  ASSERT(1, (0.0 + 1.0i) && 1);
  ASSERT(1, ({ cd z = 1.0i; double d = z; d == 0; }));
  ASSERT(1, ({ cd z; z = 5; is(z, 5, 0); }));

  // Arithmetic
  ASSERT(1, is((1.0 + 2.0i) + (3.0 + 4.0i), 4, 6));
  ASSERT(1, is((1.0 + 2.0i) - (3.0 + 5.0i), -2, -3));
  ASSERT(1, is((1.0 + 2.0i) * (3.0 + 4.0i), -5, 10));
  ASSERT(1, is((-5.0 + 10.0i) / (3.0 + 4.0i), 1, 2));
  ASSERT(1, is((1.0 + 2.0i) * 2, 2, 4));
  ASSERT(1, is(3 * (1.0 + 2.0i), 3, 6));
  ASSERT(1, is((2.0 + 4.0i) / 2, 1, 2));
  ASSERT(1, is(5 / (1.0 + 2.0i), 1, -2));
  ASSERT(1, is(1 + 1.0i, 1, 1));
  ASSERT(1, is(1 - 1.0i, 1, -1));
  ASSERT(1, is(-(1.0 + 2.0i), -1, -2));
  ASSERT(1, is(+(1.0 + 2.0i), 1, 2));
  ASSERT(1, is(~(1.0 + 2.0i), 1, -2));
  ASSERT(1, isf((cf)(1 + 2.0i) * (cf)(3 + 4.0i), -5, 10));
  ASSERT(1, isf((cf)(-5 + 10.0i) / (cf)(3 + 4.0i), 1, 2));
  ASSERT(1, isl((cl)(1 + 2.0i) * (cl)(3 + 4.0i), -5, 10));
  ASSERT(1, isl((cl)(-5 + 10.0i) / (cl)(3 + 4.0i), 1, 2));
  ASSERT(1, is((cf)(1 + 1.0i) * (1.0 + 1.0i), 0, 2));
  ASSERT(1, (1.0 + 2.0i) == (1.0 + 2.0i));
  ASSERT(0, (1.0 + 2.0i) == (1.0 + 3.0i));
  ASSERT(1, (1.0 + 2.0i) != (1.0 + 3.0i));
  ASSERT(1, (cd)3 == 3);
  ASSERT(0, 1.0i == 1);

  // Infinities and NaNs as C's Annex G has them, as gcc gets them from
  // libgcc: an infinite operand gives an infinite product or quotient,
  // and dividing by an infinity gives zero.
  double inf = __builtin_inf(), nan = __builtin_nan("");
  ASSERT(1, ({ cd z = __builtin_complex(inf, nan) * (1.0 + 1.0i); __builtin_isinf(__real__ z) || __builtin_isinf(__imag__ z); }));
  ASSERT(1, is(__builtin_complex(inf, 0.0) * __builtin_complex(inf, 0.0), inf, nan));
  ASSERT(1, is((1.0 + 1.0i) / 0.0, inf, inf));
  ASSERT(1, is((1.0 + 1.0i) / (cd)0, inf, inf));
  ASSERT(1, is((1.0 + 1.0i) / __builtin_complex(inf, inf), 0, 0));
  ASSERT(1, is(__builtin_complex(inf, 1.0) / (1.0 + 1.0i), inf, -inf));
  ASSERT(1, isf((cf)1 / (cf)0, inf, nan));
  ASSERT(1, isl(__builtin_complex((long double)inf, 1.0L) * (cl)2.0il, nan, inf));
  // Huge and tiny values are scaled, so they don't overflow or underflow.
  ASSERT(1, is(__builtin_complex(1e300, 1e300) / __builtin_complex(1e300, 1e300), 1, 0));
  ASSERT(1, is(__builtin_complex(1e-300, 1e-300) / __builtin_complex(1e-300, 1e-300), 1, 0));
  ASSERT(1, isl(__builtin_complex(1e4000L, 1e4000L) / __builtin_complex(1e4000L, 1e4000L), 1, 0));

  // Assignment operators and increments
  ASSERT(1, ({ cd z = 1.0i; z += 2; is(z, 2, 1); }));
  ASSERT(1, ({ cd z = 1.0i; z -= 1.0i; is(z, 0, 0); }));
  ASSERT(1, ({ cd z = 1 + 1.0i; z *= z; is(z, 0, 2); }));
  ASSERT(1, ({ cd z = 2.0i; z /= 2.0i; is(z, 1, 0); }));
  ASSERT(1, ({ cf z = 1.0i; z *= 3; isf(z, 0, 3); }));
  ASSERT(1, ({ cd z = 1.0i; z++; ++z; is(z, 2, 1); }));
  ASSERT(1, ({ cd z = 1.0i, w = z--; is(z, -1, 1) && is(w, 0, 1); }));
  ASSERT(1, ({ double d = 1; d += 0.0 * 1.0i; d == 1; }));
  ASSERT(1, ({ cd a[2] = {1, 2}; cd *p = a; p[1] *= 1.0i; is(a[1], 0, 2); }));
  ASSERT(1, ({ struct S s = {0}; s.z = 1; s.z += 1.0i; is(s.z, 1, 1); }));

  // Calls
  ASSERT(1, isf(add_f(1.0if, 2, 3.0if), 2, 4));
  ASSERT(1, is(many(1, 2, 3, 4, 5, 2, 1.0i), 15, 2));
  ASSERT(1, isl(add_l(1.0il, 2), 2, 1));
  ASSERT(1, isl(add_l(1, 2) * 1.0il, 0, 3));
  ASSERT(1, is(from_real(4), 4, 0));
  ASSERT(1, is(narrow(1.5L + 2.5il), 1.5, 2.5));
  ASSERT(1, is(add_f(1, 2, 3), 6, 0));
  ASSERT(1, is(va_sum(3, (cd)1, 2.0i, 3 + 3.0i), 4, 5));
  ASSERT(1, ({ cd (*fp)(cd, cd, cd, cd, cd, int, cd) = many; is(fp(0, 0, 0, 0, 1.0i, 1, 1), 1, 1); }));

  printf("OK\n");
  return 0;
}
