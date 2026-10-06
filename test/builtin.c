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
  ASSERT(0, __builtin_types_compatible_p(int *, const int *));
  ASSERT(1, __builtin_types_compatible_p(int *const, int *));
  ASSERT(0, __builtin_types_compatible_p(long, long long));
  ASSERT(0, __builtin_types_compatible_p(char, signed char));
  ASSERT(1, __builtin_types_compatible_p(signed char, signed char));
  ASSERT(1, __builtin_types_compatible_p(long long, long long int));
  ASSERT(0, __builtin_types_compatible_p(char **, const char **));

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

  // C library builtins call the function, declared or not (test.h has
  // memcpy and strlen, not strchr or abs).
  ASSERT(3, ({ char b[8]; __builtin_memcpy(b, "abc", 4); __builtin_strlen(b); }));
  ASSERT('l', *__builtin_strchr("hello", 'l'));
  ASSERT(3, __builtin_abs(-3));
  ASSERT(1, __builtin_llabs(-1LL << 40) == 1LL << 40);

  // Floating-point builtins, which need no library
  ASSERT(1, __builtin_inf() > 1e308 && __builtin_huge_valf() > 1e38f);
  ASSERT(1, __builtin_nan("") != __builtin_nan(""));
  ASSERT(1, __builtin_isnan(__builtin_nanf("")) && !__builtin_isnan(1.0));
  ASSERT(1, __builtin_isinf(-__builtin_inf()) && !__builtin_isinf(1e308));
  ASSERT(1, __builtin_isfinite(-0.0) && !__builtin_isfinite(__builtin_nan("")));
  ASSERT(1, __builtin_isnormal(1.0) && !__builtin_isnormal(1e-310) && !__builtin_isnormal(0.0));
  ASSERT(-1, __builtin_isinf_sign(-__builtin_infl()));
  ASSERT(1, !!__builtin_signbit(-0.0) && !__builtin_signbit(0.0) && !!__builtin_signbitl(-1.0L));
  ASSERT(3, __builtin_fpclassify(0, 1, 2, 3, 4, 1e-310));
  ASSERT(4, __builtin_fpclassify(0, 1, 2, 3, 4, -0.0));
  ASSERT(1, __builtin_isunordered(1.0, __builtin_nan("")) && !__builtin_isgreater(__builtin_nan(""), 1.0));
  ASSERT(1, __builtin_islessgreater(1.0, 2.0) && __builtin_isgreaterequal(2.0, 2.0));
  ASSERT(1, __builtin_fabs(-2.5) == 2.5 && !__builtin_signbit(__builtin_fabs(-0.0)));
  ASSERT(1, __builtin_fabsl(-3.5L) == 3.5L && __builtin_fabsf(-1.5f) == 1.5f);
  ASSERT(1, __builtin_copysign(3.0, -0.0) == -3.0 && __builtin_copysignl(-4.0L, 1.0L) == 4.0L);
  // Each looks at its argument more than once, but evaluates it once.
  ASSERT(2, ({ int n = 0; __builtin_isinf((n++, 1.0)); __builtin_fpclassify(0, 1, 2, 3, 4, (n++, 1.0)); n; }));

  ASSERT(10, __builtin_choose_expr(1, 10, 20.0));
  ASSERT(8, sizeof(__builtin_choose_expr(0, 1, 2.0)));
  ASSERT(16, ({ char b[16]; __builtin_object_size(b, 0); }));
  ASSERT(4, ({ int i; __builtin_object_size(&i, 1); }));
  ASSERT(1, ({ char *p = 0; __builtin_object_size(p, 0) == (unsigned long)-1; }));
  ASSERT(0, ({ char *p = 0; __builtin_object_size(p, 2); }));
  ASSERT(__LINE__, __builtin_LINE());
  ASSERT(0, strcmp(__builtin_FUNCTION(), "main"));
  ASSERT(0, strcmp(__builtin_FILE(), __FILE__));
  ASSERT(7, __builtin_speculation_safe_value(7));
#if !__has_builtin(__builtin_memcpy) || !__has_builtin(__builtin_isnan) || \
    !__has_builtin(__builtin_choose_expr) || __has_builtin(__builtin_nonesuch)
  ASSERT(1, 0);
#endif

  // Reaching __builtin_unreachable() carries on, as with gcc and clang
  // without optimization (test/driver.sh checks that __builtin_trap()
  // traps).
  {
    int reached = 0;
    if (reached == 0)
      __builtin_unreachable();
    reached = 1;
    ASSERT(1, reached);
  }

  printf("OK\n");
  return 0;
}
