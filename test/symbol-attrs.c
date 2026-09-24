#include "test.h"

// Attributes that shape symbols: alias, section, visibility, gnu_inline.
// test/driver.sh checks the symbol tables; this checks that they work.

int target(void) { return 42; }
int alias_fn(void) __attribute__((alias("target")));
static int svar = 3;
extern int avar __attribute__((alias("svar")));
static int weak_target(void) { return 5; }
int weak_alias(void) __attribute__((weak, alias("weak_target")));
static inline int inl_target(void) { return 6; }
int inl_alias(void) __attribute__((alias("inl_target")));

// Linker sets: section("name") gathers variables, and the linker defines
// __start_name and __stop_name around them.
__attribute__((section("mucc_set"))) int set_a = 1;
__attribute__((section("mucc_set"))) int set_b = 2;
__attribute__((section("mucc_set"))) int set_c;
extern int __start_mucc_set[], __stop_mucc_set[];
__attribute__((section(".text.mucc"))) int in_text(void) { return 7; }

__attribute__((visibility("hidden"))) int hidden_fn(void) { return 8; }
[[gnu::visibility("protected")]] int prot_var = 9;

// gcc's extern inline: the body is only for inlining, and calls go to the
// real definition.
extern inline __attribute__((gnu_inline)) int gi_ext(void) { return 100; }
int gi_ext(void) { return 200; }
inline __attribute__((gnu_inline)) int gi_plain(void) { return 10; }

int main() {
  ASSERT(42, alias_fn());
  ASSERT(1, alias_fn == target);
  ASSERT(3, avar);
  ASSERT(5, weak_alias());
  ASSERT(6, inl_alias());

  ASSERT(3, __stop_mucc_set - __start_mucc_set);
  ASSERT(3, __start_mucc_set[0] + __start_mucc_set[1] + __start_mucc_set[2]);
  ASSERT(7, in_text());

  ASSERT(8, hidden_fn());
  ASSERT(9, prot_var);

  ASSERT(200, gi_ext());
  ASSERT(10, gi_plain());

  printf("OK\n");
  return 0;
}
