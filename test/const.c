#include "test.h"

// A const struct declared before the struct is complete gets its members.
struct Later;
static const struct Later *later_p;
typedef const struct Later CLater;
struct Later { int a; long b; };
static struct Later later = {1, 2};

int main() {
  later_p = &later;
  ASSERT(2, later_p->b);
  ASSERT(16, sizeof(*later_p));
  ASSERT(16, sizeof(CLater));
  ASSERT(1, ({ CLater *p = &later; p->a; }));

  { const x; }
  { int const x; }
  { const int x; }
  { const int const const x; }
  ASSERT(5, ({ const x = 5; x; }));
  ASSERT(8, ({ const x = 8; const int *const y=&x; *y; }));
  ASSERT(6, ({ const x = 6; *(const * const)&x; }));

  printf("OK\n");
  return 0;
}
