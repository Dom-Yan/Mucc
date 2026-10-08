// [GNU] Vector types: attribute vector_size
#include "test.h"
#include <stdarg.h>

typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));
typedef unsigned v4su __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef unsigned short v8hu __attribute__((vector_size(16)));
typedef signed char v16qi __attribute__((vector_size(16)));
typedef unsigned char v16qu __attribute__((vector_size(16)));
typedef long long v2di __attribute__((vector_size(16)));
typedef unsigned long long v2du __attribute__((vector_size(16)));
typedef int v2si __attribute__((vector_size(8)));
typedef float v2sf __attribute__((vector_size(8)));
typedef char v4qi __attribute__((vector_size(4)));
int __attribute__((vector_size(16))) declared, *pointer;

v4si g1 = {1, 2, 3, 4};
v4sf g2 = {1.5, 2.5};
v2di g3[2] = {{1, 2}, {3}};
struct { int a; v4si v; } g4 = {7, {5, 6, 7, 8}};

// Every element of a and b, both `n` long, the same
#define SAME(a, b, n) same_bytes(&(a), &(b), sizeof(a))
static int same_bytes(void *a, void *b, long n) {
  return !memcmp(a, b, n);
}

// The operator `op` on each element, checked against the scalar operator
#define CHECK_OP(T, E, n, a, op, b)                                  \
  do {                                                               \
    T res_ = (a)op(b);                                               \
    for (int i = 0; i < n; i++)                                      \
      if (res_[i] != (E)((a)[i] op(b)[i])) {                         \
        printf("%s: element %d\n", #a " " #op " " #b, i);             \
        exit(1);                                                     \
      }                                                              \
  } while (0)

// A comparison's elements are -1 or 0
#define CHECK_CMP(T, n, a, op, b)                                    \
  do {                                                               \
    __typeof__((a)op(b)) res_ = (a)op(b);                            \
    for (int i = 0; i < n; i++)                                      \
      if (res_[i] != ((a)[i] op(b)[i] ? -1 : 0)) {                   \
        printf("%s: element %d\n", #a " " #op " " #b, i);             \
        exit(1);                                                     \
      }                                                              \
  } while (0)

static v4si add4(v4si a, v4si b) { return a + b; }

// Nine vectors: the ninth goes on the stack, as do a and b's 16 bytes
static v4sf sum9(v4sf a, v4sf b, v4sf c, v4sf d, v4sf e, v4sf f, v4sf g, v4sf h,
                 double x, v4sf i) {
  return a + b + c + d + e + f + g + h + i + (float)x;
}

static v2si mix(int a, v2si b, double c, v2si d) { return b * a + d + (int)c; }

typedef struct { v4sf v; } wrap;
static wrap twice(wrap w) {
  w.v *= 2;
  return w;
}

static v4si va_sum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  v4si s = {};
  for (int i = 0; i < n; i++)
    s += va_arg(ap, v4si);
  va_end(ap);
  return s;
}

static v4si make(int x) { return (v4si){x, x + 1, x + 2, x + 3}; }

// In test/common, which gcc compiles for the tests with glibc
typedef struct { v4sf v; } Tvwrap;
v4sf abi_vec1(v4sf a, double b, v2si c, v4sf d, v4sf e, v4sf f, v4sf g, v4sf h, int k,
              v4sf i, v2si j, Tvwrap w);
Tvwrap abi_vec2(Tvwrap w, v2si s);
v4sf abi_vec_va(int n, ...);
v4sf abi_vec_call(v4sf (*fn)(v4sf, double, v2si, v4sf, v4sf, v4sf, v4sf, v4sf, int, v4sf,
                             v2si, Tvwrap),
                  v4sf (*va)(int, ...));

static v4sf my_vec1(v4sf a, double b, v2si c, v4sf d, v4sf e, v4sf f, v4sf g, v4sf h,
                    int k, v4sf i, v2si j, Tvwrap w) {
  return a + (float)b + (float)c[1] + d + e + f + g + h + (float)k + i + (float)j[0] + w.v;
}

static v4sf my_vec_va(int n, ...) {
  va_list ap;
  va_start(ap, n);
  v4sf s = {};
  for (int i = 0; i < n; i++)
    s += va_arg(ap, v4sf);
  va_end(ap);
  return s;
}

int main() {
  ASSERT(16, sizeof(v4sf));
  ASSERT(16, _Alignof(v4sf));
  ASSERT(8, sizeof(v2si));
  ASSERT(8, _Alignof(v2si));
  ASSERT(4, sizeof(v4qi));
  ASSERT(16, sizeof(declared));
  ASSERT(4, sizeof(declared[0]));
  ASSERT(4, sizeof(g2[1]));
  ASSERT(32, sizeof(g3));

  // Initializers, globals and locals
  ASSERT(3, g1[2]);
  ASSERT(5, (int)(g2[0] + g2[1] + g2[2] + g2[3] + 1));
  ASSERT(3, g3[1][0]);
  ASSERT(0, g3[1][1]);
  ASSERT(14, g4.a + g4.v[0] + g4.v[1] + g4.v[2] + g4.v[3] - 19);
  {
    v4si a = {1, 2};
    ASSERT(3, a[0] + a[1] + a[2] + a[3]);
    v4si b = {};
    ASSERT(0, b[0] | b[1] | b[2] | b[3]);
    v4si c = a;
    ASSERT(2, c[1]);
    v4si d = {9, 4,};
    ASSERT(13, d[0] + d[1] + d[2] + d[3]);
    v4sf e = (v4sf){1, 2, 3, 4};
    ASSERT(10, (int)(e[0] + e[1] + e[2] + e[3]));
  }

  // Elements as lvalues, and of rvalues
  {
    v4si a = {1, 2, 3, 4};
    a[1] = 20;
    a[2] += 5;
    a[3]++;
    ASSERT(1 + 20 + 8 + 5, a[0] + a[1] + a[2] + a[3]);
    int *p = (int *)&a;
    ASSERT(20, p[1]);
    ASSERT(12, make(10)[2]);
    ASSERT(7, add4(a, a)[0] + 5);
    struct { char c; v8hi v; } s = {1, {1, 2, 3, 4, 5, 6, 7, 8}};
    ASSERT(16, (int)((char *)&s.v - (char *)&s));
    ASSERT(8, s.v[7]);
    s.v[7] = -1;
    ASSERT(-1, s.v[7]);
  }

  // Operators, against the scalar ones
  {
    v4si a = {1, -2, 300000, -7}, b = {3, 5, -7, 2};
    CHECK_OP(v4si, int, 4, a, +, b);
    CHECK_OP(v4si, int, 4, a, -, b);
    CHECK_OP(v4si, int, 4, a, *, b);
    CHECK_OP(v4si, int, 4, a, /, b);
    CHECK_OP(v4si, int, 4, a, %, b);
    CHECK_OP(v4si, int, 4, a, &, b);
    CHECK_OP(v4si, int, 4, a, |, b);
    CHECK_OP(v4si, int, 4, a, ^, b);
    v4si c = {1, 2, 3, 31};
    CHECK_OP(v4si, int, 4, a, <<, c);
    CHECK_OP(v4si, int, 4, a, >>, c);
    CHECK_CMP(v4si, 4, a, ==, b);
    CHECK_CMP(v4si, 4, a, !=, b);
    CHECK_CMP(v4si, 4, a, <, b);
    CHECK_CMP(v4si, 4, a, <=, b);
    CHECK_CMP(v4si, 4, a, >, b);
    CHECK_CMP(v4si, 4, a, >=, b);
    a[1] = b[1];
    CHECK_CMP(v4si, 4, a, ==, b);
    CHECK_CMP(v4si, 4, a, <=, b);

    v4su ua = {1, 0xfffffffe, 300000, 7}, ub = {3, 5, 0x80000000, 2};
    CHECK_OP(v4su, unsigned, 4, ua, /, ub);
    CHECK_OP(v4su, unsigned, 4, ua, %, ub);
    CHECK_OP(v4su, unsigned, 4, ua, >>, c);
    CHECK_CMP(v4su, 4, ua, <, ub);
    CHECK_CMP(v4su, 4, ua, >=, ub);

    v8hi h = {1, -2, 3, -4, 30000, -30000, 7, 8}, k = {2, 2, -3, 3, 2, 2, 0, 9};
    CHECK_OP(v8hi, short, 8, h, +, k);
    CHECK_OP(v8hi, short, 8, h, *, k);
    CHECK_OP(v8hi, short, 8, h, -, k);
    CHECK_CMP(v8hi, 8, h, <, k);
    CHECK_CMP(v8hi, 8, h, >, k);
    v8hu uh = {1, 65535, 3, 4, 30000, 40000, 7, 8}, uk = {2, 2, 3, 3, 2, 2, 0, 9};
    CHECK_CMP(v8hu, 8, uh, <, uk);
    CHECK_OP(v8hu, unsigned short, 8, uh, *, uk);

    v16qi q = {1, -2, 3, -4, 5, 6, 127, -128, 9, 10, 11, 12, 13, 14, 15, 16};
    v16qi r = {2, 2, 2, 2, 2, 2, 2, 2, -1, -1, 3, 3, 3, 3, 3, 3};
    CHECK_OP(v16qi, signed char, 16, q, +, r);
    CHECK_OP(v16qi, signed char, 16, q, *, r);
    CHECK_OP(v16qi, signed char, 16, q, /, r);
    CHECK_CMP(v16qi, 16, q, <, r);
    CHECK_CMP(v16qi, 16, q, ==, r);
    v16qu uq = (v16qu)q, ur = (v16qu)r;
    CHECK_CMP(v16qu, 16, uq, <, ur);
    CHECK_OP(v16qu, unsigned char, 16, uq, >>, ((v16qu){1, 2, 3, 4, 5, 6, 7, 1}));

    v2di l = {-5, 1LL << 40}, m = {3, -(1LL << 41)};
    CHECK_OP(v2di, long long, 2, l, +, m);
    CHECK_OP(v2di, long long, 2, l, *, m);
    CHECK_OP(v2di, long long, 2, l, /, m);
    CHECK_CMP(v2di, 2, l, <, m);
    CHECK_CMP(v2di, 2, l, ==, m);
    CHECK_CMP(v2di, 2, l, !=, l);
    v2du ul = (v2du)l, um = (v2du)m;
    CHECK_CMP(v2du, 2, ul, <, um);

    v4sf f = {1.5, -2, 1e30, 0.25}, g = {0.5, 4, -1e30, 0.25};
    CHECK_OP(v4sf, float, 4, f, +, g);
    CHECK_OP(v4sf, float, 4, f, -, g);
    CHECK_OP(v4sf, float, 4, f, *, g);
    CHECK_OP(v4sf, float, 4, f, /, g);
    CHECK_CMP(v4sf, 4, f, ==, g);
    CHECK_CMP(v4sf, 4, f, !=, g);
    CHECK_CMP(v4sf, 4, f, <, g);
    CHECK_CMP(v4sf, 4, f, <=, g);
    CHECK_CMP(v4sf, 4, f, >, g);
    CHECK_CMP(v4sf, 4, f, >=, g);
    v2df d = {1.5, -2}, e = {1.5, 3};
    CHECK_OP(v2df, double, 2, d, /, e);
    CHECK_CMP(v2df, 2, d, ==, e);
    CHECK_CMP(v2df, 2, d, <, e);

    v2si s = {7, -9}, t = {2, 4};
    CHECK_OP(v2si, int, 2, s, +, t);
    CHECK_OP(v2si, int, 2, s, *, t);
    CHECK_CMP(v2si, 2, s, <, t);
    v4qi cq = {1, 2, 3, 4};
    CHECK_OP(v4qi, char, 4, cq, +, cq);
  }

  // A number with a vector: each element is it
  {
    v4si a = {1, 2, 3, 4};
    v4si b = a * 3 + 1;
    ASSERT(4, b[0]);
    ASSERT(13, b[3]);
    b = 10 - a;
    ASSERT(6, b[3]);
    b = a << 2;
    ASSERT(16, b[3]);
    v4sf f = {1, 2, 3, 4};
    f = f * 2.5f + 1.0;
    ASSERT(11, (int)f[3]);
    f = 1 / f;
    ASSERT(1, f[0] == 1 / 3.5f);
    v4si c = a == 2;
    ASSERT(-1, c[1]);
    ASSERT(0, c[0]);
  }

  // Unary operators
  {
    v4si a = {1, -2, 0, 4};
    v4si b = -a;
    ASSERT(-1, b[0]);
    ASSERT(2, b[1]);
    b = ~a;
    ASSERT(-2, b[0]);
    b = +a;
    ASSERT(-2, b[1]);
    v4sf f = {1, -2, 0, 4};
    v4sf g = -f;
    ASSERT(1, __builtin_signbit(g[2]) != 0);
    ASSERT(2, (int)g[1]);
    v2df d = -(v2df){1, -0.0};
    ASSERT(-1, (int)d[0]);
    ASSERT(0, __builtin_signbit(d[1]));
  }

  // Compound assignment
  {
    v4si a = {1, 2, 3, 4};
    a += (v4si){1, 1, 1, 1};
    a *= 2;
    a -= 1;
    a <<= 1;
    ASSERT(6, a[0]);
    ASSERT(18, a[3]);
    struct { v4si v; } s = {{1, 2, 3, 4}};
    s.v += a;
    ASSERT(22, s.v[3]);
    v4si *p = &a;
    *p /= 2;
    ASSERT(9, a[3]);
  }

  // Casts keep the bits
  {
    v4sf f = {1, 2, 3, 4};
    v4si i = (v4si)f;
    ASSERT(0x3f800000, i[0]);
    v2di l = (v2di)(v4si){1, 2, 3, 4};
    ASSERT(1, l[0] == (2LL << 32 | 1));
    long x = (long)(v2si){5, 6};
    ASSERT(1, x == (6L << 32 | 5));
    v2si y = (v2si)x;
    ASSERT(6, y[1]);
    v2sf z = (v2sf)(long)(v2sf){1, 2};
    ASSERT(2, (int)z[1]);
    unsigned __int128 big = (unsigned __int128)(v4si){1, 2, 3, 4};
    ASSERT(4, (int)(big >> 96));
    v4si back = (v4si)big;
    ASSERT(3, back[2]);
    v4su u = (v4su)(v4si){-1, 0, 0, 0};
    ASSERT(1, u[0] == 0xffffffff);
    v4si fromu = (v4si)u;
    ASSERT(-1, fromu[0]);
  }

  // ?:, comma and statement expressions
  {
    v4si a = {1, 2, 3, 4}, b = {5, 6, 7, 8};
    int k = 1;
    v4si c = k ? a : b;
    ASSERT(1, c[0]);
    c = !k ? a : b;
    ASSERT(5, c[0]);
    c = (k++, b);
    ASSERT(8, c[3]);
    c = ({ v4si t = a + b; t; });
    ASSERT(12, c[3]);
  }

  // Calls: in XMM registers, then on the stack
  {
    v4si r = add4((v4si){1, 2, 3, 4}, (v4si){10, 20, 30, 40});
    ASSERT(44, r[3]);
    v4sf one = {1, 1, 1, 1};
    v4sf s = sum9(one, one, one, one, one, one, one, one, 0.5, (v4sf){1, 2, 3, 4});
    ASSERT(12, (int)s[3]);
    ASSERT(9, (int)s[0]);
    v2si m = mix(3, (v2si){1, 2}, 4.5, (v2si){10, 20});
    ASSERT(17, m[0]);
    ASSERT(30, m[1]);
    wrap w = twice((wrap){{1, 2, 3, 4}});
    ASSERT(8, (int)w.v[3]);
    v4si v = va_sum(3, (v4si){1, 2, 3, 4}, (v4si){1, 1, 1, 1}, (v4si){0, 0, 0, 100});
    ASSERT(105, v[3]);
    v4si (*fp)(v4si, v4si) = add4;
    ASSERT(8, fp(make(1), make(1))[3]);
  }

  // Calls to and from code another compiler built
  {
    v4sf one = {1, 1, 1, 1};
    v4sf r = abi_vec1(one, 2, (v2si){0, 3}, one, one, one, one, one, 4, (v4sf){1, 2, 3, 4},
                      (v2si){5, 0}, (Tvwrap){{10, 20, 30, 40}});
    ASSERT(31, (int)r[0]);
    ASSERT(64, (int)r[3]);
    Tvwrap w = abi_vec2((Tvwrap){{1, 2, 3, 4}}, (v2si){3, 1});
    ASSERT(13, (int)w.v[3]);
    r = abi_vec_va(3, one, one, (v4sf){0, 0, 0, 100});
    ASSERT(102, (int)r[3]);
    r = abi_vec_call(my_vec1, my_vec_va);
    ASSERT(33, (int)r[0]);
    ASSERT(166, (int)r[3]);
  }

  // An asm operand in an SSE register
  {
    v4si a = {1, 2, 3, 4}, b = {10, 20, 30, 40};
    __asm__("paddd %1, %0" : "+x"(a) : "x"(b));
    ASSERT(44, a[3]);
    v4sf f = {4, 9, 16, 25};
    v4sf r;
    __asm__("sqrtps %1, %0" : "=x"(r) : "x"(f));
    ASSERT(5, (int)r[3]);
  }

  // Through a pointer, and _Generic
  {
    v4si a[2] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    v4si *p = a;
    ASSERT(8, p[1][3]);
    ASSERT(6, (*++p)[1]);
    pointer = &declared;
    ASSERT(1, _Generic(declared, v4si: 1, default: 2));
    ASSERT(2, _Generic(declared, v4su: 1, default: 2));
  }

  // __builtin_shufflevector and __builtin_convertvector
  {
    v4si a = {1, 2, 3, 4}, b = {5, 6, 7, 8};
    v4si r = __builtin_shufflevector(a, b, 7, 0, 5, 2);
    v4si e1 = {8, 1, 6, 3};
    ASSERT(1, SAME(r, e1, 4));
    v2si lo = __builtin_shufflevector(a, a, 1, 0);
    ASSERT(2, lo[0]);
    ASSERT(1, lo[1]);
    ASSERT(8, sizeof(__builtin_shufflevector(a, a, 3, 2)));
    v8hi wide = {1, 2, 3, 4, 5, 6, 7, 8};
    v8hi rev = __builtin_shufflevector(wide, wide, 7, 6, 5, 4, 3, 2, 1, 0);
    ASSERT(8, rev[0]);
    ASSERT(1, rev[7]);
    v4si any = __builtin_shufflevector(a, b, -1, 4, -1, 3);
    ASSERT(5, any[1]);
    ASSERT(4, any[3]);
    v2df d = {1.5, -2.5};
    v2df ds = __builtin_shufflevector(d, d, 1, 1);
    ASSERT(1, ds[0] == -2.5 && ds[1] == -2.5);
    v4si calls = __builtin_shufflevector(r + 1, (v4si){0}, 0, 4, 1, 4);
    v4si e2 = {9, 0, 2, 0};
    ASSERT(1, SAME(calls, e2, 4));

    v4sf f = __builtin_convertvector(a, v4sf);
    ASSERT(1, f[0] == 1.0f && f[3] == 4.0f);
    v4sf g = {1.9f, -2.9f, 3.5f, 1e9f};
    v4si gi = __builtin_convertvector(g, v4si);
    v4si e3 = {1, -2, 3, 1000000000};
    ASSERT(1, SAME(gi, e3, 4));
    v2df dd = __builtin_convertvector(lo, v2df);
    ASSERT(1, dd[0] == 2.0 && dd[1] == 1.0);
    v4qi q = __builtin_convertvector((v4si){300, -1, 65, 2}, v4qi);
    ASSERT(44, q[0]);
    ASSERT(-1, q[1]);
    ASSERT(65, q[2]);
    v4su u = __builtin_convertvector(a, v4su);
    ASSERT(1, _Generic(u, v4su: 1, default: 0));
  }
#if !__has_builtin(__builtin_shufflevector) || !__has_builtin(__builtin_convertvector)
  ASSERT(0, 1);
#endif

  // vector_size before the type, among a declaration's specifiers
  ASSERT(16, ({ __attribute__((vector_size(16))) char v; sizeof(v); }));
  ASSERT(8, ({ __attribute__((vector_size(8))) short v = {1, 2, 3, 4}; sizeof(v) + v[3] - 4; }));

  // A vector literal cast to an integer, as a condition
  ASSERT(1, ({ typedef int v2si __attribute__((vector_size(8))); int r = 0; if ((long long)(v2si){2, 2}) r = 1; r; }));

  printf("OK\n");
  return 0;
}
