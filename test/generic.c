#include "test.h"

int main() {
  ASSERT(1, _Generic(100.0, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(2, _Generic((int *)0, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(2, _Generic((int[3]){}, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(3, _Generic(100, double: 1, int *: 2, int: 3, float: 4));
  ASSERT(4, _Generic(100f, double: 1, int *: 2, int: 3, float: 4));

  // long and long long, and char, signed char and unsigned char, are
  // different types; long long outranks long.
  ASSERT(2, _Generic(17L, int: 1, long: 2, long long: 3));
  ASSERT(3, _Generic(17LL, int: 1, long: 2, long long: 3));
  ASSERT(3, _Generic(17ULL, unsigned long: 2, unsigned long long: 3));
  ASSERT(1, _Generic((char)0, char: 1, signed char: 2, unsigned char: 3));
  ASSERT(2, _Generic((signed char)0, char: 1, signed char: 2, unsigned char: 3));
  ASSERT(1, _Generic(1L + 1, long: 1, long long: 2));
  ASSERT(2, _Generic(1L + 1LL, long: 1, long long: 2));
  ASSERT(2, _Generic(1UL + 1LL, unsigned long: 1, unsigned long long: 2));
  ASSERT(1, ({ long long x = 1; _Generic(x << 1, long long: 1, default: 2); }));

  // Qualifiers matter, except the controlling expression's own.
  ASSERT(1, ({ int *p = 0; _Generic(p, int *: 1, const int *: 2); }));
  ASSERT(2, ({ const int *p = 0; _Generic(p, int *: 1, const int *: 2); }));
  ASSERT(1, _Generic("lit", char *: 1, const char *: 2));
  ASSERT(1, ({ const int ci = 0; _Generic(ci, int: 1, const int: 2); }));
  ASSERT(1, ({ volatile long v = 0; _Generic(v, long: 1, default: 2); }));
  ASSERT(1, ({ const int ci = 0; _Generic(ci + 1, int: 1, default: 2); }));
  ASSERT(1, ({ struct { const int x; } s = {0}; _Generic(&s.x, const int *: 1, default: 2); }));
  ASSERT(1, ({ const struct { int x; } s = {0}; _Generic(&s.x, const int *: 1, default: 2); }));

  // An enum's value is promoted to int, or to unsigned int if it has no
  // negative values, as with gcc; unary + promotes.
  ASSERT(1, ({ enum { A = -1, B } e = B; _Generic(e + 1, int: 1, default: 2); }));
  ASSERT(1, ({ enum { A, B } e = B; _Generic(e + 1, unsigned: 1, default: 2); }));
  ASSERT(1, ({ unsigned char c = 1; _Generic(+c, int: 1, default: 2); }));

  printf("OK\n");
  return 0;
}
