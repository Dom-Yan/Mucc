#include "test.h"

struct {
  char a;
  int b : 5;
  int c : 10;
} g45 = {1, 2, 3}, g46={};

// Unnamed bit-fields are padding: initializers skip them, and a global's
// members after a bit-field with no initializer keep their values. musl's
// struct timespec pads this way.
struct ts { long sec; int :0; long nsec; int :0; };
struct ts g47 = {5, 6};
struct { int a:3; int :5; int b:4; int :0; int c; } g48 = {1, 2, 3}, g49 = {.c = 9};

// A global's bit-field gets its value converted to its type: 2 in a
// _Bool bit-field is 1, and 2.7 in an int one is 2.
struct { _Bool b : 1; _Bool c : 1; int i : 5; unsigned u : 3; _Bool d : 1; } bool_bf = {2, 0.5, 2.7, 9, 256};
struct { long l : 40; unsigned long ul : 50; } long_bf = {-1.5, 1e15};

// A packed long bit-field at an odd bit spans 9 bytes.
#pragma pack(1)
struct pk9 { char c; int a : 4; long b : 61; char d; };
#pragma pack()
struct pk9 g_pk9 = {1, -2, -3, 4};

int main() {
  ASSERT(4, sizeof(struct {int x:1; }));
  ASSERT(8, sizeof(struct {long x:1; }));

  struct bit1 {
    short a;
    char b;
    int c : 2;
    int d : 3;
    int e : 3;
  };

  ASSERT(4, sizeof(struct bit1));
  ASSERT(1, ({ struct bit1 x; x.a=1; x.b=2; x.c=3; x.d=4; x.e=5; x.a; }));
  ASSERT(1, ({ struct bit1 x={1,2,3,4,5}; x.a; }));
  ASSERT(2, ({ struct bit1 x={1,2,3,4,5}; x.b; }));
  ASSERT(-1, ({ struct bit1 x={1,2,3,4,5}; x.c; }));
  ASSERT(-4, ({ struct bit1 x={1,2,3,4,5}; x.d; }));
  ASSERT(-3, ({ struct bit1 x={1,2,3,4,5}; x.e; }));

  ASSERT(1, g45.a);
  ASSERT(2, g45.b);
  ASSERT(3, g45.c);

  ASSERT(0, g46.a);
  ASSERT(0, g46.b);
  ASSERT(0, g46.c);

  typedef struct {
    int a : 10;
    int b : 10;
    int c : 10;
  } T3;

  ASSERT(1, ({ T3 x={1,2,3}; x.a++; }));
  ASSERT(2, ({ T3 x={1,2,3}; x.b++; }));
  ASSERT(3, ({ T3 x={1,2,3}; x.c++; }));

  ASSERT(2, ({ T3 x={1,2,3}; ++x.a; }));
  ASSERT(3, ({ T3 x={1,2,3}; ++x.b; }));
  ASSERT(4, ({ T3 x={1,2,3}; ++x.c; }));

  ASSERT(4, sizeof(struct {int a:3; int c:1; int c:5;}));
  ASSERT(8, sizeof(struct {int a:3; int:0; int c:5;}));
  ASSERT(4, sizeof(struct {int a:3; int:0;}));

  // A narrow unsigned bit-field promotes to int, so these compare signed.
  ASSERT(1, ({ struct {unsigned x:5;} s = {3}; s.x > -1; }));
  ASSERT(1, ({ struct {unsigned x:5;} s = {3}; s.x - 10 < 0; }));
  ASSERT(-7, ({ struct {unsigned x:5;} s = {3}; s.x - 10; }));
  ASSERT(0, ({ struct {unsigned x:32;} s = {3}; s.x > -1; }));
  ASSERT(31, ({ struct {unsigned x:5;} s = {31}; s.x; }));

  // So does a long one narrower than 32 bits, and one of 32 bits is
  // (unsigned) int, as with gcc and clang.
  ASSERT(1, ({ struct {unsigned long x:5;} s = {0}; s.x - 1 < 0; }));
  ASSERT(1, ({ struct {unsigned long x:32;} s = {0}; s.x - 1 == 0xffffffff; }));
  ASSERT(-1, ({ struct {long x:32;} s = {-1}; s.x; }));

  // A field in the upper half of a long long's unit, loaded and stored
  // as the whole unit.
  ASSERT(1, ({ struct {unsigned long long a:8, b:32;} s = {12}; s.b = 0xcdef1234; s.b == 0xcdef1234; }));
  ASSERT(12, ({ struct {unsigned long long a:8, b:32;} s = {12}; s.b = 0xcdef1234; s.a; }));
  ASSERT(-3, ({ struct {long long a:40; long long b:5;} s = {0, 0}; s.b = -3; s.b; }));

  // A _Bool bit-field holds 0 or 1.
  ASSERT(1, ({ struct {_Bool b:1;} s = {1}; s.b; }));
  ASSERT(1, ({ struct {_Bool b:1;} s; s.b = 1; }));

  // An assignment's value is what the field holds after it.
  ASSERT(-967, ({ struct {int m:11;} s; s.m = 1081; }));
  ASSERT(1, ({ struct {unsigned m:3;} s; s.m = 9; }));

  // x++ and x-- give the old value, also when the new one wraps.
  ASSERT(1, ({ struct {unsigned a:1;} s = {1}; s.a++; }));
  ASSERT(0, ({ struct {unsigned a:1;} s = {1}; s.a++; s.a; }));
  ASSERT(3, ({ struct {int a:3;} s = {3}; s.a++; }));
  ASSERT(-4, ({ struct {int a:3;} s = {3}; s.a++; s.a; }));
  ASSERT(0, ({ struct {unsigned a:1;} s = {0}; s.a--; }));
  ASSERT(1, ({ int b = 0; struct {unsigned a:1;} s = {1}; while (s.a-- > 0) b++; b; }));
  ASSERT(1, ({ struct {unsigned a:3;} s[2] = {{1}, {5}}; int i = 0; s[i++].a++; i; }));

  ASSERT(6, g47.nsec);
  ASSERT(3, g48.b + g48.a);
  ASSERT(3, g48.c);
  ASSERT(9, g49.c);
  ASSERT(4, ({ struct ts t = {3, 4}; t.nsec; }));
  ASSERT(2, ({ struct ts t = {.sec = 1, .nsec = 2}; t.nsec; }));
  ASSERT(12, ({ struct ts t = {.nsec = 2}; t.nsec += 10; t.nsec; }));
  ASSERT(6, ({ struct { int a:3; int :5; int b:4; int :0; int c; } s = {.b = 5, 6}; s.c; }));
  ASSERT(8, ({ struct { int x; union { int u; float f; }; int y; } s = {.u = 7, 8}; s.y; }));

  ASSERT(1, bool_bf.b);
  ASSERT(1, bool_bf.c);
  ASSERT(2, bool_bf.i);
  ASSERT(1, bool_bf.u);
  ASSERT(1, bool_bf.d);
  ASSERT(-1, long_bf.l);
  ASSERT(1, long_bf.ul == 1000000000000000);

  ASSERT(11, sizeof(struct pk9));
  ASSERT(-3, g_pk9.b);
  ASSERT(1, ({ struct pk9 s = g_pk9; s.b = (1L << 60) - 1; s.b == (1L << 60) - 1 && s.a == -2 && s.c == 1 && s.d == 4; }));
  ASSERT(1, ({ struct pk9 s = g_pk9; s.b += 1L << 59; s.b == (1L << 59) - 3 && s.d == 4; }));
  ASSERT(1, ({ struct pk9 s = g_pk9; (s.b = 1L << 60) == -(1L << 60); }));

  printf("OK\n");
  return 0;
}
