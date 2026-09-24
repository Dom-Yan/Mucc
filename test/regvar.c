// Register variables, constant folding and branches (cgen.c).
#include "test.h"
#include <limits.h>
#include <setjmp.h>

// More variables than registers, all used in a loop.
static long many_vars(int n) {
  long a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7;
  for (int i = 0; i < n; i++) {
    a += b; b += c; c += d; d += e; e += f; f += g; g += 1;
  }
  return a + b + c + d + e + f + g;
}

// Parameters in registers, and some passed on the stack.
static long params(long a, long b, long c, long d, long e, long f, long g, long h) {
  long s = 0;
  for (int i = 0; i < 3; i++)
    s += a + b + c + d + e + f + g + h;
  return s;
}

// Small types in registers must keep their exact values.
static int small_types(void) {
  signed char c = -1;
  unsigned char uc = 255;
  short s = -2;
  unsigned short us = 65535;
  _Bool b = 0;
  int sum = 0;
  for (int i = 0; i < 3; i++) {
    c++; uc++; s--; us++; b = !b;
    sum += c + uc + s + us + b;
  }
  return sum; // c: 0,1,2  uc: 0,1,2  s: -3,-4,-5  us: 0,1,2  b: 1,0,1 -> -1
}

// Wrapping arithmetic on register variables.
static int wraps(void) {
  unsigned char uc = 250;
  int n = 0;
  for (int i = 0; i < 10; i++)
    if (uc++ == 255)
      n = i;
  return n * 1000 + uc; // uc wrapped to 4
}

// Taking a variable's address keeps it in memory.
static void bump(int *p) { (*p)++; }
static int addr_taken(void) {
  int x = 0;
  for (int i = 0; i < 5; i++)
    bump(&x);
  return x;
}

// Callee-saved registers survive calls, including recursive ones.
static int depth_sum(int n) {
  int a = n, b = n * 2, c = n * 3;
  if (n == 0)
    return 0;
  int r = depth_sum(n - 1);
  return r + a + b + c;
}

// A function that calls setjmp keeps its variables in memory, so
// values changed after setjmp survive longjmp.
static jmp_buf jb;
static void jump(void) { longjmp(jb, 1); }
static int with_setjmp(void) {
  int count = 0;
  for (int i = 0; i < 3; i++)
    count++;
  if (setjmp(jb) == 0) {
    count += 10;
    jump();
  }
  return count;
}

// A struct returned in memory: its hidden pointer may be in a register.
typedef struct { long a, b, c; } Big;
static Big make_big(long x) {
  Big r = {x, x * 2, x * 3};
  for (int i = 0; i < 3; i++)
    r.a += i;
  return r;
}

int main() {
  ASSERT(13556, many_vars(10));
  ASSERT(108, params(1, 2, 3, 4, 5, 6, 7, 8));
  ASSERT(-1, small_types());
  ASSERT(5004, wraps());
  ASSERT(5, addr_taken());
  ASSERT(90, depth_sum(5));
  ASSERT(13, with_setjmp());
  ASSERT(8, ({ Big b = make_big(5); b.a; }));
  ASSERT(15, ({ Big b = make_big(5); b.c; }));

  // x op= y, x++ and ++x on register variables
  ASSERT(17, ({ int x = 5; x += 3; x *= 2; x += 1; x; }));
  ASSERT(5, ({ int x = 5; x++; }));
  ASSERT(6, ({ int x = 5; ++x; }));
  ASSERT(6, ({ int x = 5; x++; x; }));
  ASSERT(4, ({ int x = 5; for (int i = 0; i < 3; i++) x--; x + 2; }));
  ASSERT(3, ({ char s[] = "abc"; char *p = s; p++; p += 1; *p - 'a' + 1; }));

  // Constant folding must wrap like the CPU.
  ASSERT(1, UINT_MAX + 1u == 0u);
  ASSERT(0, (unsigned char)256);
  ASSERT(-128, (signed char)128);
  ASSERT(1, (_Bool)256);
  ASSERT(1, -1 < 0);
  ASSERT(0, -1 < 0u);
  ASSERT(2147483647, (int)(0u - 1u >> 1));
  ASSERT(-1, -8 >> 3);
  ASSERT(1, 1000 * 1000 == 1000000);
  ASSERT(1, ({ char *p = 0; p == (void *)0; }));

  // Conditions: comparisons, !, && and || as branches
  ASSERT(3, ({ int a = 1, b = 2, r = 0; if (a < b && b < 3) r = 3; r; }));
  ASSERT(4, ({ int a = 1, b = 2, r = 0; if (a > b || !(b > 3)) r = 4; r; }));
  ASSERT(5, ({ int a = 1, r = 0; if (!(a == 2) && !(a != 1)) r = 5; r; }));
  ASSERT(6, ({ unsigned u = 1; int r = 0; if (u > -1) r = 7; else r = 6; r; }));
  ASSERT(10, ({ int n = 0; for (int i = 0; i < 20 && n < 10; i++) n++; n; }));
  ASSERT(3, ({ int n = 0; do n++; while (n < 3 || n == 1); n; }));
  ASSERT(2, ({ int x = 5; x > 3 && x < 10 ? 2 : 1; }));

  printf("OK\n");
  return 0;
}
