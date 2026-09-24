// The default, C17: C23's new keywords are ordinary names, as in older
// code, and `int f()` says nothing about f's parameters.
#include "test.h"

#if __STDC_VERSION__ != 201710L
# error "expected C17"
#endif

struct thread_local { int nullptr; }; // git has a struct thread_local
static int constexpr = 3;
enum boolean { false, true };
typedef enum boolean bool;
static int alignof(int x) { return x; }

static int add(int a, int b) { return a + b; }

int main() {
  struct thread_local t = {4};
  ASSERT(4, t.nullptr);
  ASSERT(3, constexpr);
  bool b = true;
  ASSERT(1, b);
  ASSERT(0, false);
  ASSERT(5, alignof(5));

  int (*fp)() = add;
  ASSERT(3, fp(1, 2));

  printf("OK\n");
  return 0;
}
