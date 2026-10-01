#include "test.h"

// The caller's frame is one up the chain, at a higher address.
static int __attribute__((noinline)) frame_check(void) {
  return __builtin_frame_address(1) > __builtin_frame_address(0);
}

int main() {
  ASSERT(1, __builtin_types_compatible_p(int, int));
  ASSERT(1, __builtin_types_compatible_p(double, double));
  ASSERT(0, __builtin_types_compatible_p(int, long));
  ASSERT(0, __builtin_types_compatible_p(long, float));
  ASSERT(1, __builtin_types_compatible_p(int *, int *));
  ASSERT(0, __builtin_types_compatible_p(short *, int *));
  ASSERT(0, __builtin_types_compatible_p(int **, int *));
  ASSERT(1, __builtin_types_compatible_p(const int, int));
  ASSERT(0, __builtin_types_compatible_p(unsigned, int));
  ASSERT(1, __builtin_types_compatible_p(signed, int));
  ASSERT(0, __builtin_types_compatible_p(struct {int a;}, struct {int a;}));

  ASSERT(1, __builtin_types_compatible_p(int (*)(void), int (*)(void)));
  ASSERT(1, __builtin_types_compatible_p(void (*)(int), void (*)(int)));
  ASSERT(1, __builtin_types_compatible_p(void (*)(int, double), void (*)(int, double)));
  ASSERT(1, __builtin_types_compatible_p(int (*)(float, double), int (*)(float, double)));
  ASSERT(0, __builtin_types_compatible_p(int (*)(float, double), int));
  ASSERT(0, __builtin_types_compatible_p(int (*)(float, double), int (*)(float)));
  ASSERT(0, __builtin_types_compatible_p(int (*)(float, double), int (*)(float, double, int)));
  ASSERT(1, __builtin_types_compatible_p(double (*)(...), double (*)(...)));
  ASSERT(0, __builtin_types_compatible_p(double (*)(...), double (*)(void)));

  ASSERT(1, ({ typedef struct {int a;} T; __builtin_types_compatible_p(T, T); }));
  ASSERT(1, ({ typedef struct {int a;} T; __builtin_types_compatible_p(T, const T); }));

  ASSERT(1, ({ struct {int a; int b;} x; __builtin_types_compatible_p(typeof(x.a), typeof(x.b)); }));

  // Bit counts, at run time and folded from constants
  ASSERT(24, ({ volatile unsigned x = 0xf0; __builtin_clz(x); }));
  ASSERT(4, ({ volatile unsigned x = 0xf0; __builtin_ctz(x); }));
  ASSERT(4, ({ volatile unsigned x = 0xf0; __builtin_popcount(x); }));
  ASSERT(32, ({ volatile unsigned x = -1; __builtin_popcount(x); }));
  ASSERT(0, ({ volatile unsigned x = 0xf0; __builtin_parity(x); }));
  ASSERT(1, ({ volatile unsigned x = 0x70; __builtin_parity(x); }));
  ASSERT(5, ({ volatile int x = 0xf0; __builtin_ffs(x); }));
  ASSERT(0, ({ volatile int x = 0; __builtin_ffs(x); }));
  ASSERT(31, ({ volatile int x = -1; __builtin_clrsb(x); }));
  ASSERT(31, ({ volatile int x = 0; __builtin_clrsb(x); }));
  ASSERT(23, ({ volatile int x = 0xff; __builtin_clrsb(x); }));
  ASSERT(0, ({ volatile unsigned long x = 1UL << 63; __builtin_clzl(x); }));
  ASSERT(63, ({ volatile unsigned long x = 1; __builtin_clzll(x); }));
  ASSERT(40, ({ volatile unsigned long x = 1UL << 40; __builtin_ctzl(x); }));
  ASSERT(64, ({ volatile unsigned long x = -1; __builtin_popcountll(x); }));
  ASSERT(41, ({ volatile long x = 1L << 40; __builtin_ffsl(x); }));
  ASSERT(63, ({ volatile long x = -1; __builtin_clrsbl(x); }));
  ASSERT(28, __builtin_clz(8));
  ASSERT(3, __builtin_ctz(8));
  ASSERT(2, __builtin_popcountl(0x8000000000000001));
  ASSERT(6, ({ char a[__builtin_ctz(64)]; sizeof(a); }));

  // Byte swaps
  ASSERT(0x3412, ({ volatile unsigned short x = 0x1234; __builtin_bswap16(x); }));
  ASSERT(0x44332211, ({ volatile unsigned x = 0x11223344; __builtin_bswap32(x); }));
  ASSERT(1, ({ volatile unsigned long x = 0x1122334455667788; __builtin_bswap64(x) == 0x8877665544332211; }));
  ASSERT(0x3412, __builtin_bswap16(0x1234));
  ASSERT(1, __builtin_bswap64(0x1122334455667788) == 0x8877665544332211);
  ASSERT(2, sizeof(__builtin_bswap16(0)));
  ASSERT(8, sizeof(__builtin_bswap64(0)));

  // Hints have the value of their first argument.
  ASSERT(3, __builtin_expect(3, 0));
  ASSERT(8, sizeof(__builtin_expect(3, 0)));
  ASSERT(3, ({ int x = 3; __builtin_expect_with_probability(x, 1, 0.9); }));
  ASSERT(1, ({ int a[2]; __builtin_assume_aligned(a, 4) == a; }));
  ASSERT(5, ({ int x = 5; __builtin_prefetch(&x); __builtin_prefetch(&x, 1, 3); x; }));

  // __builtin_constant_p doesn't evaluate its argument.
  ASSERT(1, __builtin_constant_p(3 * 4));
  ASSERT(0, ({ int x = 0; __builtin_constant_p(x++); }));
  ASSERT(0, ({ int x = 0; __builtin_constant_p(x++); x; }));

  ASSERT(1, __builtin_frame_address(0) != 0);
  ASSERT(1, frame_check());
  ASSERT(1, __builtin_return_address(0) != 0);

  printf("OK\n");
  return 0;
}
