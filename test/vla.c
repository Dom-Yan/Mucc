#include "test.h"

// Parameters whose sizes come from earlier parameters
void vla_param_proto(int n, int a[n]);
void vla_param_proto2(int r, int c, int m[r][c]);
void vla_param_star(int n, int a[*], int m[][*]);
void vla_param_qual(int n, int a[const n], int b[static 1]);

int vla_param_sum(int n, int a[n]) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += a[i];
  return s;
}

// m[i][j] needs the row size, c * sizeof(int), computed on entry.
int vla_param_at(int r, int c, int m[r][c], int i, int j) { return m[i][j]; }
long vla_param_row(int r, int c, int m[r][c]) { return sizeof(*m); }
long vla_param_diff(int r, int c, int m[r][c]) { return (char *)(m + 2) - (char *)m; }

// Sizes that use earlier parameters in expressions and sizeof
long vla_param_expr(int n, char (*p)[n * 2 + 1]) { return sizeof(*p); }
long vla_param_sizeof(int n, char (*p)[sizeof(n)]) { return sizeof(*p); }

// A VLA typedef's size is computed where it's declared, once (C17
// 6.7.8p3), and every use of it has that size, in any branch.
int vla_typedef_once(int n) {
  typedef int A[n++];
  A a, b;
  return n * 100 + sizeof a + sizeof(A) - sizeof b;
}

int vla_typedef_branch(int n, int c) {
  typedef char A[n];
  int s;
  if (c)
    s = sizeof(A);
  else
    s = sizeof(A) + 100;
  return s;
}

int vla_typedef_loop(int n) {
  int r = 0;
  for (int i = 0; i < 3; i++) {
    typedef int A[n + i];
    r += sizeof(A);
  }
  return r;
}

// An array parameter's length is evaluated, though the parameter is a
// pointer.
int vla_param_side(int n, int a[n++]) { return n; }

// Leaving a VLA's scope frees it, so a VLA in a loop gets the same
// memory on every pass instead of using up the stack. Each returns 1.
int vla_free_loop(int n) {
  char *first = 0;
  for (int i = 0; i < 10; i++) {
    char a[n];
    if (!first)
      first = a;
    if (a != first)
      return 0;
    if (i % 2)
      continue;
  }
  return 1;
}

int vla_free_goto(int n) {
  char *first = 0;
  int i = 0;
again:;
  char a[n];
  if (!first)
    first = a;
  if (a != first)
    return 0;
  if (++i < 10)
    goto again;
  return 1;
}

int vla_free_block(int n) {
  char *p;
  { char a[n]; p = a; }
  char b[n];
  return b == p;
}

int vla_free_break(int n) {
  char *p;
  for (;;) { char a[n]; p = a; break; }
  char b[n];
  return b == p;
}

// Sizes that change on every pass, a million times: 2 GB if never freed.
int vla_free_many(void) {
  void *volatile p;
  for (int i = 0; i < 1000000; i++) {
    int x[i % 1000 + 1];
    x[i % 1000] = i;
    p = x;
  }
  return 1;
}

// Labels-as-values in a function with a VLA: taking a label's address
// is no jump into its scope.
static int vla_label(int n) {
  int a[n];
  void *p = &&set;
  a[0] = 0;
  goto *p;
set:
  a[0] = n * 2;
  return a[0];
}

int main() {
  ASSERT(1, vla_free_loop(1000));
  ASSERT(1, vla_free_goto(1000));
  ASSERT(1, vla_free_block(1000));
  ASSERT(1, vla_free_break(1000));
  ASSERT(1, vla_free_many());
  ASSERT(5, ({ int n = 3; int a[n]; a[2] = 5; a[2]; }));

  ASSERT(10, ({ int a[] = {1, 2, 3, 4}; vla_param_sum(4, a); }));
  ASSERT(7, ({ int m[3][4] = {{0}, {0, 0, 7}}; vla_param_at(3, 4, m, 1, 2); }));
  ASSERT(11, ({ int m[2][5] = {{0}, {0, 0, 0, 0, 11}}; vla_param_at(2, 5, m, 1, 4); }));
  ASSERT(20, ({ int m[3][5]; vla_param_row(3, 5, m); }));
  ASSERT(24, ({ int m[4][3]; vla_param_diff(4, 3, m); }));
  ASSERT(7, ({ char b[7]; vla_param_expr(3, &b); }));
  ASSERT(4, ({ char b[4]; vla_param_sizeof(1, &b); }));

  ASSERT(20, ({ int n=5; int x[n]; sizeof(x); }));
  ASSERT((5+1)*(8*2)*4, ({ int m=5, n=8; int x[m+1][n*2]; sizeof(x); }));

  ASSERT(8, ({ char n=10; int (*x)[n][n+2]; sizeof(x); }));
  ASSERT(480, ({ char n=10; int (*x)[n][n+2]; sizeof(*x); }));
  ASSERT(48, ({ char n=10; int (*x)[n][n+2]; sizeof(**x); }));
  ASSERT(4, ({ char n=10; int (*x)[n][n+2]; sizeof(***x); }));

  ASSERT(60, ({ char n=3; int x[5][n]; sizeof(x); }));
  ASSERT(12, ({ char n=3; int x[5][n]; sizeof(*x); }));

  ASSERT(60, ({ char n=3; int x[n][5]; sizeof(x); }));
  ASSERT(20, ({ char n=3; int x[n][5]; sizeof(*x); }));

  ASSERT(0, ({ int n=10; int x[n+1][n+6]; int *p=(int *)x; for (int i = 0; i<sizeof(x)/4; i++) p[i]=i; x[0][0]; }));
  ASSERT(5, ({ int n=10; int x[n+1][n+6]; int *p=(int *)x; for (int i = 0; i<sizeof(x)/4; i++) p[i]=i; x[0][5]; }));
  ASSERT(5*16+2, ({ int n=10; int x[n+1][n+6]; int *p=(int *)x; for (int i = 0; i<sizeof(x)/4; i++) p[i]=i; x[5][2]; }));

  ASSERT(10, ({ int n=5; sizeof(char[2][n]); }));

  ASSERT(6, vla_label(3));

  // The length is an assignment expression, evaluated once.
  ASSERT(12, ({ int k; int a[k = 3]; sizeof(a); }));
  ASSERT(3, ({ int k; int a[k = 3]; k; }));
  ASSERT(4, ({ int n = 0; int a[n += 4][2]; n; }));
  ASSERT(32, ({ int n = 0; int a[n += 4][2]; sizeof(a); }));

  ASSERT(412, vla_typedef_once(3));
  ASSERT(105, vla_typedef_branch(5, 0));
  ASSERT(7, vla_typedef_branch(7, 1));
  ASSERT(24, vla_typedef_loop(1));
  ASSERT(5, ({ int x[1]; vla_param_side(4, x); }));

  printf("OK\n");
  return 0;
}
