#include "test.h"

// Several declarators in one file-scope declaration, functions and
// variables mixed, as musl's headers write them.
int decl_f1(void), decl_f2(int), decl_v1 = 5, decl_f3(int, int), *decl_v2 = &decl_v1;
__attribute__((visibility("hidden"))) long decl_h1(long), decl_h2(long);
int decl_v3 = 7, decl_f4(void);
static __inline int decl_inline(void) { return 4; }

int main() {
  ASSERT(1, decl_f1());
  ASSERT(4, decl_f2(2));
  ASSERT(5, *decl_v2);
  ASSERT(7, decl_f3(3, 4));
  ASSERT(11, decl_h1(10));
  ASSERT(22, decl_h2(20));
  ASSERT(11, decl_f4() + decl_v3);
  ASSERT(4, ({ __volatile int x = 4; x; })); // musl writes `__asm__ __volatile__`
  ASSERT(8, ({ __typeof(decl_v1) *p = 0; sizeof(*p) + sizeof(p) - 4; }));
  ASSERT(3, ({ __extension__ int x = 3; x; }));
  ASSERT(4, decl_inline());

  ASSERT(1, ({ char x; sizeof(x); }));
  ASSERT(2, ({ short int x; sizeof(x); }));
  ASSERT(2, ({ int short x; sizeof(x); }));
  ASSERT(4, ({ int x; sizeof(x); }));
  ASSERT(8, ({ long int x; sizeof(x); }));
  ASSERT(8, ({ int long x; sizeof(x); }));

  ASSERT(8, ({ long long x; sizeof(x); }));

  ASSERT(0, ({ _Bool x=0; x; }));
  ASSERT(1, ({ _Bool x=1; x; }));
  ASSERT(1, ({ _Bool x=2; x; }));
  ASSERT(1, (_Bool)1);
  ASSERT(1, (_Bool)2);
  ASSERT(0, (_Bool)(char)256);

  // x++ and x-- give a _Bool's old value, not the new one minus 1.
  ASSERT(1, ({ _Bool x=1; x++; }));
  ASSERT(0, ({ _Bool x=0; x--; }));
  ASSERT(1, ({ _Bool x=0; x--; x; }));

  // Function declarations and variables in one local declaration
  ASSERT(8, ({ int x = 3, decl_f1(void); x + decl_f1() + 4; }));
  ASSERT(6, ({ int decl_f2(int), y = 3; decl_f2(y); }));
  ASSERT(9, ({ int decl_f1(void), decl_f2(int), y = 4; decl_f2(y) + decl_f1(); }));

  // [GNU] __auto_type, as C23's auto
  ASSERT(8, ({ __auto_type x = 5L; sizeof(x); }));
  ASSERT(5, ({ __auto_type x = 5L; __auto_type p = &x; *p; }));
  ASSERT(4, ({ const __auto_type f = 1.5f; sizeof(f); }));
  ASSERT(8, ({ int a[3]; __auto_type p = a; sizeof(p); }));

  printf("OK\n");
  return 0;
}

int decl_f1(void) { return 1; }
int decl_f2(int x) { return x * 2; }
int decl_f3(int a, int b) { return a + b; }
long decl_h1(long x) { return x + 1; }
long decl_h2(long x) { return x + 2; }
int decl_f4(void) { return 4; }
