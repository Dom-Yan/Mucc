// The language and library: C23 features, structs, unions, bit-fields,
// VLAs, varargs, _Generic, setjmp, qsort, long double math, wide
// characters and UTF-8, strtod and printf formats, checked arithmetic,
// #embed, atomics, function pointers, recursion.
#include "check.h"
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdckdint.h>
#include <stdint.h>
#include <wchar.h>

typedef struct Node Node;
struct Node {
  int value;
  Node *left, *right;
};

static Node *insert(Node *n, int v) {
  if (!n) {
    n = calloc(1, sizeof *n);
    n->value = v;
    return n;
  }
  if (v < n->value)
    n->left = insert(n->left, v);
  else
    n->right = insert(n->right, v);
  return n;
}

static int inorder(Node *n, int *out, int k) {
  if (!n)
    return k;
  k = inorder(n->left, out, k);
  out[k++] = n->value;
  return inorder(n->right, out, k);
}

static int sum(int count, ...) {
  va_list ap;
  va_start(ap, count);
  int s = 0;
  for (int i = 0; i < count; i++)
    s += va_arg(ap, int);
  va_end(ap);
  return s;
}

static double vsum(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  double s = 0;
  for (const char *p = fmt; *p; p++)
    s += *p == 'd' ? va_arg(ap, double) : va_arg(ap, long);
  va_end(ap);
  return s;
}

#define type_name(x) _Generic((x), int: "int", double: "double", char *: "char *", default: "other")

struct Big { long a[8]; char tag; };
static struct Big make_big(long base) {
  struct Big b;
  for (int i = 0; i < 8; i++)
    b.a[i] = base + i;
  b.tag = 'B';
  return b;
}

struct Flags {
  unsigned ready : 1;
  unsigned mode : 3;
  signed delta : 4;
};

union Pun { float f; uint32_t u; };

static jmp_buf jb;
static void deep(int n) {
  if (n == 0)
    longjmp(jb, 42);
  deep(n - 1);
}

static int cmp(const void *a, const void *b) { return *(int *)a - *(int *)b; }

static const char embedded[] = {
#embed "check.h"
  , 0
};

int main(void) {
  // A binary tree, recursion and malloc
  Node *root = 0;
  int vals[] = {50, 30, 70, 20, 40, 60, 80, 35, 65};
  for (int i = 0; i < 9; i++)
    root = insert(root, vals[i]);
  int out[9];
  CHECK(inorder(root, out, 0) == 9);
  for (int i = 1; i < 9; i++)
    CHECK(out[i - 1] < out[i]);

  // varargs, with ints, longs and doubles
  CHECK(sum(4, 1, 2, 3, 4) == 10);
  CHECK(vsum("dldl", 1.5, 2L, 0.25, 4L) == 7.75);

  // C23
  constexpr int size = 4;
  int arr[size] = {};
  auto x = 3.5;
  bool flag = true;
  int *np = nullptr;
  typeof(x) y = x * 2;
  CHECK(arr[3] == 0 && y == 7.0 && flag && !np);
  [[maybe_unused]] int unused = 0;
  static_assert(sizeof(int) == 4);
  CHECK(0b1010 == 10 && 1'000'000 == 1000000);

  // _Generic
  CHECK(!strcmp(type_name(1), "int") && !strcmp(type_name(1.0), "double"));
  CHECK(!strcmp(type_name((char *)"s"), "char *"));

  // structs by value, compound literals, designated initializers
  struct Big b = make_big(10);
  CHECK(b.a[7] == 17 && b.tag == 'B');
  struct Flags fl = {.ready = 1, .mode = 5, .delta = -3};
  CHECK(fl.ready == 1 && fl.mode == 5 && fl.delta == -3);
  CHECK(((int[]){1, 2, 3})[2] == 3);
  union Pun pun = {.f = 1.0f};
  CHECK(pun.u == 0x3f800000);

  // VLAs
  int n = 5;
  int m[n][n];
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++)
      m[i][j] = i * j;
  CHECK(m[4][3] == 12 && sizeof m == 100);

  // setjmp/longjmp out of deep recursion
  int r = setjmp(jb);
  if (r == 0)
    deep(100);
  CHECK(r == 42);

  // qsort and bsearch
  int data[] = {9, 3, 7, 1, 8, 2};
  qsort(data, 6, sizeof(int), cmp);
  CHECK(data[0] == 1 && data[5] == 9);
  int key = 7;
  CHECK(bsearch(&key, data, 6, sizeof(int), cmp) == &data[3]);

  // math, including long double
  CHECK(fabs(sqrt(2.0) - 1.41421356237) < 1e-9);
  CHECK(fabsl(expl(1.0L) - 2.718281828459045235L) < 1e-15L);
  CHECK(isnan(nan("")) && isinf(1.0 / 0.0));
  CHECK(lround(2.5) == 3 && fmod(10, 3) == 1);
  CHECK(LDBL_MANT_DIG == 64);

  // strtod and printf round trips
  char buf[128];
  snprintf(buf, sizeof buf, "%.17g", 0.1);
  CHECK(strtod(buf, 0) == 0.1);
  snprintf(buf, sizeof buf, "%a", 1.0);
  CHECK(!strcmp(buf, "0x1p+0"));
  snprintf(buf, sizeof buf, "%5.2f|%-4d|%x|%lld|%s", 3.14159, 7, 255, LLONG_MIN, "s");
  CHECK(!strcmp(buf, " 3.14|7   |ff|-9223372036854775808|s"));
  CHECK(strtoul("ffff", 0, 16) == 65535);

  // UTF-8 and wide characters
  setlocale(LC_ALL, "C.UTF-8");
  wchar_t wbuf[16];
  CHECK(mbstowcs(wbuf, "h\xc3\xa9llo", 16) == 5 && wbuf[1] == 0xe9);
  CHECK(wcslen(L"wide") == 4);
  char mb[8];
  CHECK(wcrtomb(mb, 0x20ac, 0) == 3); // the euro sign

  // checked arithmetic
  int res;
  CHECK(ckd_add(&res, INT_MAX, 1) && !ckd_mul(&res, 1000, 1000) && res == 1000000);

  // #embed
  CHECK(strstr(embedded, "CHECK(cond)") != 0);

  // function pointers in a table
  double (*fns[])(double) = {sin, cos, sqrt};
  CHECK(fns[2](16.0) == 4.0);

  return done("lang");
}
