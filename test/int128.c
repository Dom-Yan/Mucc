#include "test.h"
#include <stdarg.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

// A 128-bit value from its halves
static u128 mk(unsigned long hi, unsigned long lo) {
  return (u128)hi << 64 | lo;
}

static unsigned long hi(u128 x) { return x >> 64; }
static unsigned long lo(u128 x) { return x; }

static int same(u128 x, unsigned long h, unsigned long l) {
  return hi(x) == h && lo(x) == l;
}

// Passing: the third __int128 doesn't fit in the registers left, so it
// goes on the stack, and `z`, after it, takes the last register.
static i128 sum(int a, i128 b, i128 c, i128 d, long z) {
  return a + b + c + d + z;
}

static i128 on_stack(long a, long b, long c, long d, long e, i128 x, long f) {
  return x * 2 + a + b + c + d + e + f;
}

static i128 va_sum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  i128 s = 0;
  for (int i = 0; i < n; i++)
    s += va_arg(ap, i128);
  va_end(ap);
  return s;
}

struct S { char c; i128 x; };

static i128 member(struct S s) { return s.x + s.c; }

static struct S make_s(i128 x) { return (struct S){1, x}; }

i128 g1 = -5;
u128 g2 = 0xffffffffffffffffUL;
u128 g3 = -1;
i128 garr[] = {1, -2, 3};
__int128_t g4 = 7;
__uint128_t g5 = 8;

// Constants of any 128-bit value
i128 big1 = (i128)1 << 100;
u128 big2 = (u128)0xffffffffffffffffUL * 0xffffffffffffffffUL;
i128 big3 = -((i128)1 << 64) + 5;
i128 big4 = ((i128)-1 << 100) >> 99;
long big_low = (long)(((i128)1 << 70) >> 68);
int big_cond = (i128)1 << 64 ? 1 : 2;

// A switch, with cases of any 128-bit value and ranges
static int sw128(i128 v) {
  switch (v) {
  case 0: return 1;
  case -1: return 2;
  case 0xffffffffffffffffU: return 3;
  case (i128)1 << 100: return 4;
  case 100 ... 200: return 5;
  case (i128)1 << 64 ... ((i128)1 << 64) + 2: return 6;
  default: return 0;
  }
}

static int usw128(u128 v) {
  switch (v) {
  case -1: return 1;
  case 0xffffffffffffffffU: return 2;
  case (u128)-5 ... (u128)-2: return 3;
  default: return 0;
  }
}

// An overflow builtin's result: whether it overflowed, and *r
static int ovf(int ov, u128 r, unsigned long h, unsigned long l) {
  return ov * 2 + same(r, h, l);
}

int main() {
  ASSERT(16, sizeof(i128));
  ASSERT(16, _Alignof(u128));

  ASSERT(1, same(big1, 1UL << 36, 0));
  ASSERT(1, same(big2, 0xfffffffffffffffeUL, 1));
  ASSERT(1, same(big3, -1UL, 5));
  ASSERT(1, same(big4, -1UL, -2UL));
  ASSERT(4, big_low);
  ASSERT(1, big_cond);

  ASSERT(1, sw128(0));
  ASSERT(2, sw128(-1));
  ASSERT(3, sw128(0xffffffffffffffffU));
  ASSERT(4, sw128((i128)1 << 100));
  ASSERT(5, sw128(150));
  ASSERT(6, sw128((i128)1 << 64));
  ASSERT(6, sw128(((i128)1 << 64) + 2));
  ASSERT(0, sw128(((i128)1 << 64) + 3));
  ASSERT(0, sw128(-2));
  ASSERT(1, usw128(-1));
  ASSERT(2, usw128(0xffffffffffffffffU));
  ASSERT(3, usw128(-3));
  ASSERT(0, usw128(-6));

  // Overflow builtins on __int128: 2^127 - 1 + 1 overflows i128, not u128
  ASSERT(3, ({ i128 r; int o = __builtin_add_overflow((i128)(~(u128)0 >> 1), 1, &r); ovf(o, r, 1UL << 63, 0); }));
  ASSERT(1, ({ u128 r; int o = __builtin_add_overflow((i128)(~(u128)0 >> 1), 1, &r); ovf(o, r, 1UL << 63, 0); }));
  ASSERT(1, ({ u128 r; int o = __builtin_mul_overflow(mk(0, -1UL), mk(0, -1UL), &r); ovf(o, r, -2UL, 1); }));
  ASSERT(3, ({ u128 r; int o = __builtin_mul_overflow(mk(1, 0), mk(1, 0), &r); ovf(o, r, 0, 0); }));
  ASSERT(3, ({ u128 r; int o = __builtin_sub_overflow((i128)0, (i128)1, &r); ovf(o, r, -1UL, -1UL); }));
  ASSERT(1, ({ long r; int o = __builtin_sub_overflow((i128)5, (u128)7, &r); ovf(o, r, -1UL, -2UL); }));
  ASSERT(3, ({ int r; int o = __builtin_add_overflow((i128)1 << 64, 0, &r); ovf(o, r, 0, 0); }));
  ASSERT(16, __SIZEOF_INT128__);
  ASSERT(32, sizeof(struct S));
  ASSERT(16, __builtin_offsetof(struct S, x));

  // Globals
  ASSERT(1, same(g1, -1, -5));
  ASSERT(1, same(g2, 0, -1));
  ASSERT(1, same(g3, -1, -1));
  ASSERT(1, same(garr[1], -1, -2));
  ASSERT(1, g4 == 7 && g5 == 8);

  // Conversions from and to smaller integers
  ASSERT(1, same((i128)-1, -1, -1));
  ASSERT(1, same((i128)-1L, -1, -1));
  ASSERT(1, same((u128)-1U, 0, 0xffffffff));
  ASSERT(1, same((u128)0x8000000000000000UL, 0, 0x8000000000000000UL));
  ASSERT(1, same((i128)(signed char)-3, -1, -3));
  ASSERT(-3, (int)(i128)-3);
  ASSERT(5, (int)mk(7, 5));
  ASSERT(1, (_Bool)mk(1, 0));
  ASSERT(0, (_Bool)mk(0, 0));

  // Arithmetic, with carries and borrows between the halves
  ASSERT(1, same(mk(0, -1) + 1, 1, 0));
  ASSERT(1, same(mk(1, 0) - 1, 0, -1));
  ASSERT(1, same(mk(0, -1) * mk(0, -1), 0xfffffffffffffffe, 1));
  ASSERT(1, same(mk(3, 5) * 7, 21, 35));
  ASSERT(1, same((i128)-3 * 4, -1, -12));
  ASSERT(1, same(-mk(0, 1), -1, -1));
  ASSERT(1, same(~mk(0, 0), -1, -1));
  ASSERT(1, same(mk(0xf0, 0xff) & mk(0x3c, 0xf), 0x30, 0xf));
  ASSERT(1, same(mk(1, 2) | mk(4, 8), 5, 10));
  ASSERT(1, same(mk(3, 3) ^ mk(1, 2), 2, 1));

  // Division: a divisor below 2^64, and above
  ASSERT(1, same(mk(10, 0) / 3, 3, 0x5555555555555555));
  ASSERT(1, same(mk(10, 0) % 3, 0, 1));
  ASSERT(1, same(mk(-1, -1) / mk(1, 0), 0, -1));
  ASSERT(1, same(mk(-1, -1) % mk(1, 0), 0, -1));
  ASSERT(1, same(mk(0x8000000000000000, 5) / mk(0x8000000000000000, 0), 0, 1));
  ASSERT(1, same(mk(5, 7) / mk(5, 7), 0, 1));
  ASSERT(1, same((i128)-7 / 2, -1, -3));
  ASSERT(1, same((i128)-7 % 2, -1, -1));
  ASSERT(1, same((i128)7 / -2, -1, -3));
  ASSERT(1, same((i128)7 % -2, 0, 1));
  ASSERT(1, same(-(i128)mk(9, 0) / (i128)mk(3, 0), -1, -3));

  // Shifts by every kind of count
  ASSERT(1, same(mk(0, 1) << 0, 0, 1));
  ASSERT(1, same(mk(0, 1) << 63, 0, 0x8000000000000000));
  ASSERT(1, same(mk(0, 1) << 64, 1, 0));
  ASSERT(1, same(mk(0, 3) << 127, 0x8000000000000000, 0));
  ASSERT(1, same(mk(1, 0) >> 1, 0, 0x8000000000000000));
  ASSERT(1, same(mk(0x8000000000000000, 0) >> 127, 0, 1));
  ASSERT(1, same((i128)mk(0x8000000000000000, 0) >> 127, -1, -1));
  ASSERT(1, same((i128)mk(0x8000000000000000, 0) >> 64, -1, 0x8000000000000000));
  ASSERT(1, same((i128)-16 >> 2, -1, -4));
  ASSERT(1, ({ int n = 70; same(mk(0, 1) << n, 64, 0); }));
  ASSERT(1, ({ i128 n = 65; same(mk(4, 0) >> n, 0, 2); }));

  // Comparisons, decided by either half
  ASSERT(1, mk(1, 0) > mk(0, -1));
  ASSERT(0, mk(1, 0) < mk(0, -1));
  ASSERT(1, (i128)-1 < 0);
  ASSERT(0, (u128)-1 < 0);
  ASSERT(1, (i128)mk(-1, 0) < (i128)mk(0, 5));
  ASSERT(1, mk(2, 3) == mk(2, 3));
  ASSERT(1, mk(2, 3) != mk(3, 3));
  ASSERT(1, mk(2, 3) <= mk(2, 3));
  ASSERT(1, mk(2, 3) >= mk(2, 3));
  ASSERT(0, mk(2, 3) <= mk(2, 2));
  ASSERT(1, ({ int r = 0; if (mk(1, 0)) r = 1; r; }));
  ASSERT(1, ({ int r = 0; if (!mk(0, 0)) r = 1; r; }));
  ASSERT(1, mk(1, 0) && mk(0, 1));
  ASSERT(7, mk(1, 0) ? 7 : 8);
  ASSERT(1, ({ int r = 0; if (mk(5, 0) > mk(4, 9)) r = 1; r; }));

  // Mixed with other integer types: the other one is converted
  ASSERT(1, same(mk(1, 0) + -1, 0, -1));
  ASSERT(1, same(mk(1, 0) + -1L, 0, -1));
  ASSERT(1, same(mk(1, 0) + 0xffffffffffffffffUL, 1, -1));
  ASSERT(1, -1 < (i128)0);

  // Floating point
  ASSERT(1, (double)mk(1, 0) == 18446744073709551616.0);
  ASSERT(1, (double)(i128)-mk(1, 0) == -18446744073709551616.0);
  ASSERT(1, (float)mk(1, 0) == 18446744073709551616.0f);
  ASSERT(1, (long double)mk(1, 1) == 18446744073709551617.0L);
  ASSERT(1, (double)mk(0, 12345) == 12345.0);
  ASSERT(1, (double)(i128)-7 == -7.0);
  ASSERT(1, (double)mk(-1, -1) == 340282366920938463463374607431768211456.0);
  // Rounded once: 2^64 + 2^11 + 1 is just above halfway between two
  // doubles, so it rounds up.
  ASSERT(1, (double)mk(1, 0x801) == 18446744073709555712.0);
  ASSERT(1, (double)mk(1, 0x800) == 18446744073709551616.0);
  ASSERT(1, same((u128)1e30, 0xc9f2c9cd0, 0x4675000000000000));
  ASSERT(1, same((i128)-1e30, ~0xc9f2c9cd0UL, -0x4675000000000000UL));
  ASSERT(1, same((i128)-2.5, -1, -2));
  ASSERT(1, same((u128)3.99, 0, 3));
  ASSERT(1, same((u128)18446744073709551616.0f, 1, 0));
  ASSERT(1, same((i128)123456789.0L, 0, 123456789));

  // Calls
  ASSERT(1, same(sum(1, 2, mk(1, 0), -4, 5), 1, 4));
  ASSERT(1, same(on_stack(1, 2, 3, 4, 5, mk(1, 1), 6), 2, 23));
  ASSERT(1, same(va_sum(3, (i128)1, mk(2, 0), (i128)-2), 1, -1));
  ASSERT(1, same(va_sum(5, (i128)1, (i128)2, (i128)3, (i128)4, (i128)5), 0, 15));
  ASSERT(1, same(member((struct S){2, mk(3, 4)}), 3, 6));
  ASSERT(1, same(make_s(mk(9, 9)).x, 9, 9));

  // Memory, assignment operators, increments
  ASSERT(1, ({ i128 a[3] = {1, 2, 3}; i128 *p = a; p[1] += mk(1, 0); same(a[1], 1, 2); }));
  ASSERT(1, ({ u128 x = mk(0, -1); x++; same(x, 1, 0); }));
  ASSERT(1, ({ u128 x = mk(1, 0); x--; same(x, 0, -1); }));
  ASSERT(1, ({ u128 x = mk(3, 0); x <<= 1; x |= 1; same(x, 6, 1); }));
  ASSERT(1, ({ u128 x = mk(9, 0); x /= 3; x %= mk(2, 0); same(x, 1, 0); }));
  ASSERT(1, ({ struct S s; s.x = mk(4, 5); s.x *= 2; same(s.x, 8, 10); }));
  ASSERT(1, ({ i128 x = 5, y = x--; same(y, 0, 5) && same(x, 0, 4); }));

  printf("OK\n");
  return 0;
}
