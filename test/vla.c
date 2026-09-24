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

int main() {
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

  printf("OK\n");
  return 0;
}
