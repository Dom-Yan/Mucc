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

int g1 __attribute__((aligned(64)));
__attribute__((aligned(32))) static char g2;
char g3 [[gnu::aligned(128)]];

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

  // Like every program in test/, which test/link.sh runs too.
  printf("OK\n");
  return 0;
}
