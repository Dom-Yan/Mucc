#include "test.h"
#include <stddef.h>

typedef struct {
  int a;
  char b;
  int c;
  double d;
} T;

typedef struct {
  char tag;
  struct { short x; int y[4]; } inner;
} U;

// offsetof is an integer constant expression, even with a member path,
// so it can size an array in a struct (a compile-time check, as BusyBox
// does) and isn't a VLA.
struct Check {
  char inner_y_at_8[offsetof(U, inner.y) == 8 ? 1 : -1];
  char y2_at_16[offsetof(U, inner.y[2]) == 16 ? 1 : -1];
};

int main() {
  ASSERT(0, offsetof(T, a));
  ASSERT(4, offsetof(T, b));
  ASSERT(8, offsetof(T, c));
  ASSERT(16, offsetof(T, d));
  ASSERT(4, offsetof(U, inner.x));
  ASSERT(8, offsetof(U, inner.y));
  ASSERT(16, offsetof(U, inner.y[2]));
  ASSERT(2, sizeof(struct Check));
  ASSERT(8, ({ char a[offsetof(U, inner.y)]; sizeof(a); }));
  ASSERT(1, __builtin_constant_p(offsetof(U, inner.y)));

  printf("OK\n");
  return 0;
}
