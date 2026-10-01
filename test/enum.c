#include "test.h"

// As with gcc, an enum with no negative values is unsigned, so a bit-field
// of it holds values up to 2^width - 1.
typedef enum { K0, K1, K2, K3, K4, K5, K6, K7 } Kind;
struct node { Kind kind : 3; int x : 5; };

// [GNU] Values beyond int's range widen the enum.
enum u32 { U32A = 0x80000000u, U32B };
enum s64 { S64A = -1, S64B = 0x80000000u };
enum u64 { U64A = 0x100000000 };
enum after_max { AFTER_A = 0x7fffffff, AFTER_B };

// [GNU] `enum E` before E's list, as gcc and clang allow
enum fwd;
enum fwd *fwd_ptr;
enum fwd { FWD_A, FWD_B = 7 };
enum fwd fwd_var = FWD_B;

int main() {
  ASSERT(0, ({ enum { zero, one, two }; zero; }));
  ASSERT(1, ({ enum { zero, one, two }; one; }));
  ASSERT(2, ({ enum { zero, one, two }; two; }));
  ASSERT(5, ({ enum { five=5, six, seven }; five; }));
  ASSERT(6, ({ enum { five=5, six, seven }; six; }));
  ASSERT(0, ({ enum { zero, five=5, three=3, four }; zero; }));
  ASSERT(5, ({ enum { zero, five=5, three=3, four }; five; }));
  ASSERT(3, ({ enum { zero, five=5, three=3, four }; three; }));
  ASSERT(4, ({ enum { zero, five=5, three=3, four }; four; }));
  ASSERT(4, ({ enum { zero, one, two } x; sizeof(x); }));
  ASSERT(4, ({ enum t { zero, one, two }; enum t y; sizeof(y); }));

  ASSERT(6, ({ struct node n = {K6, -3}; n.kind; }));
  ASSERT(-3, ({ struct node n = {K6, -3}; n.x; }));
  ASSERT(7, ({ struct node n; n.kind = K7; n.kind; }));
  ASSERT(1, (Kind)-1 > 0);
  ASSERT(1, ({ enum e { A = -1, B } x = -1; x < 0; }));
  ASSERT(1, __builtin_types_compatible_p(__typeof__(K1), int));

  ASSERT(4, sizeof(enum u32));
  ASSERT(1, U32A > 0);
  ASSERT(1, U32B == 0x80000001u);
  ASSERT(4, sizeof(U32A));
  ASSERT(8, sizeof(enum s64));
  ASSERT(1, S64B == 0x80000000L);
  ASSERT(1, (enum s64)-1 < 0);
  ASSERT(8, sizeof(enum u64));
  ASSERT(1, U64A == 0x100000000);
  ASSERT(1, AFTER_B == 0x80000000u);
  ASSERT(4, sizeof(enum after_max));

  ASSERT(7, fwd_var);
  ASSERT(1, ({ fwd_ptr = &fwd_var; *fwd_ptr == FWD_B; }));

  printf("OK\n");
  return 0;
}
