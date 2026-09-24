// flags: -std=c23
// C23 features, and C11's _Static_assert.
#include "test.h"
#include <stdarg.h>
#include <stddef.h>

#if __STDC_VERSION__ != 202311L
# error "expected C23"
#endif

// _Static_assert and static_assert, at file scope.
_Static_assert(sizeof(int) == 4, "int is 4 bytes");
static_assert(sizeof(long) == 8);

struct WithAssert {
  int x;
  static_assert(sizeof(int) == 4, "allowed among struct members");
  int y;
};

// [[attributes]] are accepted and ignored.
[[nodiscard]] static int twice(int x) { return x * 2; }
[[maybe_unused]] static int unused_global;
static int with_param_attr([[maybe_unused]] int a, int b) { return b; }

// __has_include
#if __has_include(<stddef.h>) && !__has_include(<no/such/header.h>)
static int has_include_works = 1;
#else
static int has_include_works = 0;
#endif

#if defined(__has_include)
static int has_include_defined = 1;
#else
static int has_include_defined = 0;
#endif

// #elifdef / #elifndef
#define DEFINED_MACRO
#if 0
static int elifdef = 0;
#elifdef DEFINED_MACRO
static int elifdef = 1;
#else
static int elifdef = 2;
#endif

#ifdef NOT_DEFINED
static int elifndef = 0;
#elifndef NOT_DEFINED
static int elifndef = 1;
#endif

// true in #if is 1.
#if true
static int if_true = 1;
#else
static int if_true = 0;
#endif

// constexpr at file scope: usable in constant expressions.
constexpr int N = 4;
constexpr double HALF = 0.5;
constexpr long BIG = N * 1000L;
static int by_n[N * 2];
_Static_assert(sizeof(by_n) == 32);

// auto at file scope
auto g_auto = 7;
auto g_str = "global";

// A constant converted to bool is 0 or 1, not the value's low byte.
static bool b2 = 2;
static bool bhalf = 0.5;
static bool b256 = 256;
static int by_bool[(bool)2 + 1];

// Unnamed parameters in a definition; variadic with no named parameter,
// and va_start with one argument.
static int unnamed(int, int b) { return b; }
static int sum3(...) {
  va_list ap;
  va_start(ap);
  int s = 0;
  for (int i = 0; i < 3; i++)
    s += va_arg(ap, int);
  va_end(ap);
  return s;
}

// #embed
static const unsigned char hello[] = {
#embed "embed.txt"
};
static const char hello_z[] = {
#embed "embed.txt" suffix(, 0)
};
static const unsigned char hello2[] = {
#embed "embed.txt" limit(2)
};
static const int empty_embed[] = {
#embed "embed-empty.bin" if_empty(-1)
};
#define EMBED_FILE "embed.txt"
static const unsigned char via_macro[] = {
#embed EMBED_FILE prefix(0,) __limit__(1 + 2)
};

#if __has_embed("embed.txt") == __STDC_EMBED_FOUND__ && \
    __has_embed("embed-empty.bin") == __STDC_EMBED_EMPTY__ && \
    __has_embed("embed.txt" limit(0)) == __STDC_EMBED_EMPTY__ && \
    __has_embed("no-such-file") == __STDC_EMBED_NOT_FOUND__ && \
    __has_embed("embed.txt" vendor::param) == __STDC_EMBED_NOT_FOUND__
static int has_embed_works = 1;
#else
static int has_embed_works = 0;
#endif

#if __has_c_attribute(nodiscard) == 202003 && __has_c_attribute(__fallthrough__) && \
    !__has_c_attribute(vendor::thing)
static int has_c_attribute_works = 1;
#else
static int has_c_attribute_works = 0;
#endif

// Labels before declarations, and at the end of a block.
static int label_decl(int n) {
  int sum = 0;
  switch (n) {
  case 1:
    int x = 10;
    sum += x;
    break;
  default:
  }
  if (n > 5)
    goto end;
  sum += 100;
end:
  return sum;
}

int main() {
  // bool, true, false
  bool b = true;
  ASSERT(1, b);
  ASSERT(0, false);
  ASSERT(1, sizeof(bool));
  ASSERT(1, sizeof(true));
  ASSERT(1, _Generic(true, bool: 1, int: 2));
  ASSERT(1, ({ bool c = 5; c; }));

  // nullptr
  int *p = nullptr;
  ASSERT(1, p == 0);
  ASSERT(1, !nullptr);
  ASSERT(8, sizeof(nullptr));
  ASSERT(8, sizeof(nullptr_t));
  ASSERT(1, ({ nullptr_t n = nullptr; n == nullptr; }));

  // Digit separators
  ASSERT(1000000, 1'000'000);
  ASSERT(255, 0xF'F);
  ASSERT(5, 0b1'01);
  ASSERT(1, 1'000.5 == 1000.5);

  // C23 spellings
  alignas(16) int aligned = 1;
  ASSERT(0, (long)&aligned % 16);
  ASSERT(8, alignof(long));
  ASSERT(4, ({ typeof_unqual(const int) t = 4; t; }));
  thread_local static int tl = 3;
  ASSERT(3, tl);

  // static_assert in a block, and __asm__
  static_assert(1 + 1 == 2, "math works");
  __asm__("nop");

  ASSERT(4, twice(2));
  ASSERT(7, with_param_attr(1, 7));
  ASSERT(8, sizeof(struct WithAssert));
  ASSERT(1, has_include_works);
  ASSERT(1, has_include_defined);
  ASSERT(1, elifdef);
  ASSERT(1, elifndef);
  ASSERT(1, if_true);
  ASSERT(110, label_decl(1));
  ASSERT(100, label_decl(2));
  ASSERT(0, label_decl(9));

  // [[fallthrough]] in a switch
  int f = 0;
  switch (1) {
  case 1:
    f += 1;
    [[fallthrough]];
  case 2:
    f += 2;
  }
  ASSERT(3, f);

  // Empty initializer
  int zeros[3] = {};
  ASSERT(0, zeros[0] + zeros[1] + zeros[2]);

  // auto: the type comes from the initializer.
  auto a_int = 1;
  auto a_dbl = 2.5;
  auto a_str = "hi";
  int arr[3] = {1, 2, 3};
  auto a_ptr = arr;
  auto a_fn = unnamed;
  auto a_uns = 3000000000u;
  ASSERT(4, sizeof(a_int));
  ASSERT(8, sizeof(a_dbl));
  ASSERT(1, a_dbl == 2.5);
  ASSERT(8, sizeof(a_str));
  ASSERT('i', a_str[1]);
  ASSERT(8, sizeof(a_ptr));
  ASSERT(3, a_ptr[2]);
  ASSERT(9, a_fn(0, 9));
  ASSERT(1, a_uns > 0);
  ASSERT(7, g_auto);
  ASSERT('g', g_str[0]);

  // constexpr in a block, static, and with auto
  constexpr int M = N + 1;
  int by_m[M];
  static constexpr unsigned char UC = 200;
  constexpr auto K = 12;
  static_assert(M == 5);
  ASSERT(20, sizeof(by_m));
  ASSERT(200, UC);
  ASSERT(12, K);
  ASSERT(4000, BIG);
  ASSERT(1, HALF == 0.5);
  ASSERT(4, *&N);
  ASSERT(1, ({ int r = 0; switch (3) { case N - 1: r = 1; } r; }));

  ASSERT(1, b2);
  ASSERT(1, bhalf);
  ASSERT(1, b256);
  ASSERT(8, sizeof(by_bool));

  // u8 character literals
  ASSERT('A', u8'A');
  ASSERT(1, sizeof(u8'A'));

  ASSERT(9, unnamed(1, 9));
  ASSERT(6, sum3(1, 2, 3));

  // #embed and __has_embed
  ASSERT(5, sizeof(hello));
  ASSERT('H', hello[0]);
  ASSERT('o', hello[4]);
  ASSERT(0, strcmp(hello_z, "Hello"));
  ASSERT(2, sizeof(hello2));
  ASSERT('e', hello2[1]);
  ASSERT(-1, empty_embed[0]);
  ASSERT(4, sizeof(via_macro));
  ASSERT(0, via_macro[0]);
  ASSERT('l', via_macro[3]);
  ASSERT(1, has_embed_works);
  ASSERT(1, has_c_attribute_works);

  printf("OK\n");
  return 0;
}
