// C23 features, and C11's _Static_assert.
#include "test.h"
#include <stddef.h>

// _Static_assert and static_assert, at file scope.
_Static_assert(sizeof(int) == 4, "int is 4 bytes");
static_assert(sizeof(long) == 8);

struct WithAssert {
  int x;
  static_assert(sizeof(int) == 4, "allowed among struct members");
  int y;
};

// [[attributes]] are accepted and ignored.
[[nodiscard]] static int twice(int x) { return x * 2; }
[[maybe_unused]] static int unused_global;
static int with_param_attr([[maybe_unused]] int a, int b) { return b; }

// __has_include
#if __has_include(<stddef.h>) && !__has_include(<no/such/header.h>)
static int has_include_works = 1;
#else
static int has_include_works = 0;
#endif

#if defined(__has_include)
static int has_include_defined = 1;
#else
static int has_include_defined = 0;
#endif

// #elifdef / #elifndef
#define DEFINED_MACRO
#if 0
static int elifdef = 0;
#elifdef DEFINED_MACRO
static int elifdef = 1;
#else
static int elifdef = 2;
#endif

#ifdef NOT_DEFINED
static int elifndef = 0;
#elifndef NOT_DEFINED
static int elifndef = 1;
#endif

// true in #if is 1.
#if true
static int if_true = 1;
#else
static int if_true = 0;
#endif

// Labels before declarations, and at the end of a block.
static int label_decl(int n) {
  int sum = 0;
  switch (n) {
  case 1:
    int x = 10;
    sum += x;
    break;
  default:
  }
  if (n > 5)
    goto end;
  sum += 100;
end:
  return sum;
}

int main() {
  // bool, true, false
  bool b = true;
  ASSERT(1, b);
  ASSERT(0, false);
  ASSERT(1, sizeof(bool));
  ASSERT(1, sizeof(true));
  ASSERT(1, _Generic(true, bool: 1, int: 2));
  ASSERT(1, ({ bool c = 5; c; }));

  // nullptr
  int *p = nullptr;
  ASSERT(1, p == 0);
  ASSERT(1, !nullptr);
  ASSERT(8, sizeof(nullptr));
  ASSERT(8, sizeof(nullptr_t));
  ASSERT(1, ({ nullptr_t n = nullptr; n == nullptr; }));

  // Digit separators
  ASSERT(1000000, 1'000'000);
  ASSERT(255, 0xF'F);
  ASSERT(5, 0b1'01);
  ASSERT(1, 1'000.5 == 1000.5);

  // C23 spellings
  alignas(16) int aligned = 1;
  ASSERT(0, (long)&aligned % 16);
  ASSERT(8, alignof(long));
  ASSERT(4, ({ typeof_unqual(const int) t = 4; t; }));
  thread_local static int tl = 3;
  ASSERT(3, tl);

  // static_assert in a block, and __asm__
  static_assert(1 + 1 == 2, "math works");
  __asm__("nop");

  ASSERT(4, twice(2));
  ASSERT(7, with_param_attr(1, 7));
  ASSERT(8, sizeof(struct WithAssert));
  ASSERT(1, has_include_works);
  ASSERT(1, has_include_defined);
  ASSERT(1, elifdef);
  ASSERT(1, elifndef);
  ASSERT(1, if_true);
  ASSERT(110, label_decl(1));
  ASSERT(100, label_decl(2));
  ASSERT(0, label_decl(9));

  // [[fallthrough]] in a switch
  int f = 0;
  switch (1) {
  case 1:
    f += 1;
    [[fallthrough]];
  case 2:
    f += 2;
  }
  ASSERT(3, f);

  // Empty initializer
  int zeros[3] = {};
  ASSERT(0, zeros[0] + zeros[1] + zeros[2]);

  printf("OK\n");
  return 0;
}
