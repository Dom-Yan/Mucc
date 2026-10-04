// Prints sizes, alignments and offsets of structs laid out with GNU
// attributes. test/attribute-layout.sh compiles it with mucc and with the
// system's gcc and requires the same output.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct s1 { char c; int x __attribute__((aligned(8))); };
struct s2 { char c; int x __attribute__((packed)); };
struct s3 { char c; int x __attribute__((packed, aligned(2))); };
struct __attribute__((packed)) s4 { char c; int x __attribute__((aligned(4))); };
struct __attribute__((packed)) s5 { char c; _Alignas(8) int x; };
typedef int ai16 __attribute__((aligned(16)));
struct s6 { char c; ai16 x; };
typedef int ai1 __attribute__((aligned(1)));
struct s7 { char c; ai1 x; };
struct s8 { char c; struct { int a; } __attribute__((aligned(16))) s; };
typedef struct { int a; } __attribute__((aligned)) s9;
struct s10 { char c; __attribute__((aligned(8))) int x; };
struct s11 { char c; int x __attribute__((aligned(8))), y; };
struct s12 { int a; char b; } __attribute__((packed, aligned(4)));
struct s13 { char c; int x[3] __attribute__((aligned(16))); };
struct s14 { char c; long x __attribute__((aligned(2))); };
struct s15 { char c; struct s2 in; char d; };
struct __attribute__((packed)) s16 { char c; struct s1 in; };
union u1 { char c; int x __attribute__((aligned(8))); };
struct s18 { char c; [[gnu::aligned(8)]] int x; int y [[gnu::packed]]; };

enum __attribute__((packed)) pe1 { PA1, PB1 = 200 };
enum pe2 { PA2, PB2 = 300 } __attribute__((packed));
enum __attribute__((packed)) pe3 { PA3 = -1, PB3 = 100 };
enum __attribute__((packed)) pe4 { PA4 = 70000 };
enum __attribute__((packed)) pe5 { PA5 = -200 };
struct s19 { char c; enum pe1 e; enum pe2 f; };

// #pragma pack caps members' alignment, an explicit one too, and lets
// bit-fields straddle units of their type.
#pragma pack(push, 2)
struct p1 { char a; int b; double c; };
struct p2 { char a; int b __attribute__((aligned(8))); };
struct p3 { char a; int b:4; int c:30; char d; };
union pu1 { char a[5]; int b; };
#pragma pack(1)
struct p4 { char a; short b:9; char c; };
struct p5 { char a; long b:40; char c; };
#pragma pack(pop)
struct p6 { char a; int b; };
_Pragma("pack(push, 4)") struct p7 { char a; double b; long double c; }; _Pragma("pack(pop)")
#pragma pack(push, 1)
#pragma pack(push, 8)
struct p8 { char a; int b:20; int c:20; };
#pragma pack(pop)
struct p9 { char a; struct p6 in; };
#pragma pack(pop)
#pragma pack(16)
union __attribute__((packed)) pu2 { long a:11; char b; };
#pragma pack()

// Zero-width bit-fields leave a struct's alignment alone; packed unions
// and packed structs' bit-fields are laid out as gcc does.
struct z1 { char a; int :0; char b; };
struct z2 { char a; int b:4; long :0; char c; };
union z3 { char a; int :0; };
union __attribute__((packed)) z4 { char a[5]; int b; };
struct __attribute__((packed)) z5 { char a; int b:20; int c:20; };
struct __attribute__((packed)) z6 { char a; long b:60; char c; };

// Values stored in bit-fields that straddle units, as bytes.
static void show_bytes(char *name, void *p, size_t n) {
  printf("%s", name);
  for (size_t i = 0; i < n; i++)
    printf(" %02x", ((unsigned char *)p)[i]);
  printf("\n");
}

static void packed_bitfields(void) {
  struct p3 x = {1, 5, -3, 9};
  x.c += 7;
  show_bytes("p3", &x, sizeof x);
  printf("p3 %d %d %d %d\n", x.a, x.b, x.c, x.d);
  static struct p5 y = {1, -2, 3};
  y.b -= 0x123456789;
  show_bytes("p5", &y, sizeof y);
  printf("p5 %d %ld %d\n", y.a, (long)y.b, y.c);
  struct z6 z = {0};
  z.b = 0x0fedcba987654321;
  z.c = 4;
  show_bytes("z6", &z, sizeof z);
  printf("z6 %lx\n", (long)z.b);
}

int g1 __attribute__((aligned(64)));
__attribute__((aligned(32))) static char g2;
char g3 [[gnu::aligned(128)]];

// Locals aligned above 16, of each kind, in a recursive function: the
// result counts misaligned addresses and wrong values, so it must be 0.
struct __attribute__((aligned(32))) over32 { int x, y; };
typedef int ai64 __attribute__((aligned(64)));
static struct over32 make_over(int x) { struct over32 s = {x, x + 1}; return s; }
static int overaligned_locals(int depth) {
  char pad = 1;
  _Alignas(32) int a;
  __attribute__((aligned(64))) char b[3];
  struct over32 s = {0};
  ai64 c = 5;
  struct over32 r = make_over(depth);
  int *pa = &a;
  *pa = depth;
  s.x = 7;
  int bad = (uintptr_t)&a % 32 + (uintptr_t)b % 64 + (uintptr_t)&s % 32 +
            (uintptr_t)&c % 64 + (uintptr_t)&r % 32;
  if (depth > 0)
    bad += overaligned_locals(depth - 1);
  return bad + (a != depth) + (s.x != 7) + (s.y != 0) + (c != 5) +
         (r.y != depth + 1) + (pad != 1);
}

#define SHOW(t, m) \
  printf("%-10s size=%zu align=%zu offset(%s)=%zu\n", #t, sizeof(t), \
         _Alignof(t), #m, offsetof(t, m))

int main(void) {
  SHOW(struct s1, x);
  SHOW(struct s2, x);
  SHOW(struct s3, x);
  SHOW(struct s4, x);
  SHOW(struct s5, x);
  SHOW(struct s6, x);
  SHOW(struct s7, x);
  SHOW(struct s8, s);
  SHOW(s9, a);
  SHOW(struct s10, x);
  SHOW(struct s11, y);
  SHOW(struct s12, b);
  SHOW(struct s13, x);
  SHOW(struct s14, x);
  SHOW(struct s15, d);
  SHOW(struct s16, in);
  SHOW(union u1, x);
  SHOW(struct s18, y);
  SHOW(struct s19, f);
  SHOW(struct p1, c);
  SHOW(struct p2, b);
  SHOW(struct p3, d);
  SHOW(union pu1, b);
  SHOW(struct p4, c);
  SHOW(struct p5, c);
  SHOW(struct p6, b);
  SHOW(struct p7, c);
  SHOW(struct p8, a);
  SHOW(struct p9, in);
  SHOW(union pu2, b);
  SHOW(struct z1, b);
  SHOW(struct z2, c);
  SHOW(union z3, a);
  SHOW(union z4, b);
  SHOW(struct z5, a);
  SHOW(struct z6, c);
  packed_bitfields();
  printf("ai16 align=%zu ai1 align=%zu\n", _Alignof(ai16), _Alignof(ai1));
  printf("packed enums %zu %zu %zu %zu %zu, signed %d %d %d\n",
         sizeof(enum pe1), sizeof(enum pe2), sizeof(enum pe3),
         sizeof(enum pe4), sizeof(enum pe5), (enum pe1)-1 < 0,
         (enum pe3)-1 < 0, (enum pe5)-1 < 0);

  static int sl __attribute__((aligned(64)));
  int l16 __attribute__((aligned(16)));
  char l8 __attribute__((aligned(8)));
  printf("g1 %% 64 = %d, g2 %% 32 = %d, g3 %% 128 = %d\n", (int)((uintptr_t)&g1 % 64),
         (int)((uintptr_t)&g2 % 32), (int)((uintptr_t)&g3 % 128));
  printf("sl %% 64 = %d, l16 %% 16 = %d, l8 %% 8 = %d\n", (int)((uintptr_t)&sl % 64),
         (int)((uintptr_t)&l16 % 16), (int)((uintptr_t)&l8 % 8));

  printf("overaligned locals: %d\n", overaligned_locals(3));

  // Like every program in test/, which test/link.sh runs too.
  printf("OK\n");
  return 0;
}
