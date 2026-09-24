#include "test.h"
#include "stddef.h"

// Attributes in every position gcc accepts them
__attribute__((unused)) static int attr_before;
static int __attribute__((unused)) attr_middle;
static int attr_after __attribute__((unused));
static int attr_after_init __attribute__((unused)) = 3;
int attr_fn(int a __attribute__((unused)), int b) __attribute__((nonnull, warn_unused_result));
int attr_fn(int a __attribute__((unused)), int b) { return b; }
__attribute__((noinline, cold)) static int attr_def(void) { return 5; }
int * __attribute__((unused)) attr_ptr;
typedef int attr_int __attribute__((may_alias));
struct attr_members { int a __attribute__((deprecated)); int b : 4 __attribute__((unused)); };
enum __attribute__((deprecated)) attr_enum {
  ATTR_A __attribute__((deprecated)) = 1, ATTR_B
} __attribute__((unused));
void (__attribute__((noreturn)) *attr_fp)(void);

// Both spellings of the keyword and of names; empty lists
static int __attribute((unused)) attr_short;
static int attr_under __attribute__((__unused__, __deprecated__));
static int attr_empty __attribute__(()) __attribute__((, unused,));

// Every attribute mucc ignores (ignored_attributes in src/parser.c): none
// may change what the program does.
static int __attribute__((
  unused, maybe_unused, format(printf, 1, 2), format_arg(1), nonnull(1),
  returns_nonnull, sentinel, warn_unused_result, nodiscard, deprecated("x"),
  unavailable, pure, const, malloc, alloc_size(1), alloc_align(1),
  assume_aligned(8), nothrow, leaf, noinline, noclone, noipa, always_inline,
  hot, cold, artificial, flatten, may_alias, fallthrough, no_sanitize("address"),
  no_sanitize_address, no_sanitize_thread, no_sanitize_undefined,
  no_address_safety_analysis, no_instrument_function,
  no_profile_instrument_function, no_stack_protector, no_split_stack,
  stack_protect, no_icf, no_reorder, noplt, optimize("O2"), returns_twice,
  access(read_only, 1), nonstring, designated_init, externally_visible,
  counted_by(n), error("e"), warning("w"), tls_model("local-exec"),
  warn_if_not_aligned(8), null_terminated_string_arg(1),
  nonnull_if_nonzero(1, 2), fd_arg(1), fd_arg_read(1), fd_arg_write(1),
  zero_call_used_regs("all"), patchable_function_entry(1, 0),
  strict_flex_array(1), expected_throw, flag_enum, uninitialized, assume(1),
  musttail))
attr_all(int x) { return x + 1; }

// C23 syntax
[[gnu::unused]] static int attr_c23;
[[__gnu__::__unused__]] static int attr_c23_under;
struct [[gnu::packed]] attr_c23_packed { char a; int b; };
[[maybe_unused, deprecated]] static int attr_std;
static int attr_std_after [[maybe_unused]];

// __has_attribute: 1 for what mucc implements or safely ignores, 0 for
// what it rejects or doesn't know. __has_builtin likewise.
#if defined(__has_attribute) && __has_attribute(packed) && \
    __has_attribute(__aligned__) && __has_attribute(cleanup) && \
    __has_attribute(format) && !__has_attribute(vector_size) && \
    !__has_attribute(bogus)
static int has_attribute_ok = 1;
#endif
#if defined(__has_builtin) && __has_builtin(__builtin_unreachable) && \
    !__has_builtin(__builtin_bogus) && !__has_builtin(printf)
static int has_builtin_ok = 1;
#endif
// Through a macro, as glibc's <sys/cdefs.h> does
#define my_has_attribute(x) __has_attribute(x)
#if my_has_attribute(packed) && !my_has_attribute(bogus)
static int has_attribute_macro_ok = 1;
#endif

int main() {
  ASSERT(1, has_attribute_ok);
  ASSERT(1, has_builtin_ok);
  ASSERT(1, has_attribute_macro_ok);
  ASSERT(8, attr_all(7));
  ASSERT(5, attr_def());
  ASSERT(2, attr_fn(1, 2));
  ASSERT(3, attr_after_init);
  ASSERT(2, ATTR_B);
  ASSERT(5, sizeof(struct attr_c23_packed));
  ASSERT(8, sizeof(struct attr_members));
  ASSERT(1, ({ int x = 1; switch (x) { case 1: x = 1; __attribute__((fallthrough)); case 2: break; } x; }));
  ASSERT(1, ({ int x = 1; switch (x) { case 1: x = 1; [[fallthrough]]; case 2: break; } x; }));
  ASSERT(3, ({ int x = 0; goto l; l: __attribute__((unused)) x = 3; x; }));
  ASSERT(4, ({ int __attribute__((unused)) y = 4; y; }));
  ASSERT(8, sizeof(int * __attribute__((unused))));

  ASSERT(5, ({ struct { char a; int b; } __attribute__((packed)) x; sizeof(x); }));
  ASSERT(0, offsetof(struct __attribute__((packed)) { char a; int b; }, a));
  ASSERT(1, offsetof(struct __attribute__((packed)) { char a; int b; }, b));

  ASSERT(5, ({ struct __attribute__((packed)) { char a; int b; } x; sizeof(x); }));
  ASSERT(0, offsetof(struct { char a; int b; } __attribute__((packed)), a));
  ASSERT(1, offsetof(struct { char a; int b; } __attribute__((packed)), b));

  ASSERT(9, ({ typedef struct { char a; int b[2]; } __attribute__((packed)) T; sizeof(T); }));
  ASSERT(9, ({ typedef struct __attribute__((packed)) { char a; int b[2]; } T; sizeof(T); }));

  ASSERT(1, offsetof(struct __attribute__((packed)) T { char a; int b[2]; }, b));
  ASSERT(1, _Alignof(struct __attribute__((packed)) { char a; int b[2]; }));

  ASSERT(8, ({ struct __attribute__((aligned(8))) { int a; } x; _Alignof(x); }));
  ASSERT(8, ({ struct { int a; } __attribute__((aligned(8))) x; _Alignof(x); }));

  ASSERT(8, ({ struct __attribute__((aligned(8), packed)) { char a; int b; } x; _Alignof(x); }));
  ASSERT(8, ({ struct { char a; int b; } __attribute__((aligned(8), packed)) x; _Alignof(x); }));
  ASSERT(1, offsetof(struct __attribute__((aligned(8), packed)) { char a; int b; }, b));
  ASSERT(1, offsetof(struct { char a; int b; } __attribute__((aligned(8), packed)), b));

  ASSERT(8, ({ struct __attribute__((aligned(8))) __attribute__((packed)) { char a; int b; } x; _Alignof(x); }));
  ASSERT(8, ({ struct { char a; int b; } __attribute__((aligned(8))) __attribute__((packed)) x; _Alignof(x); }));
  ASSERT(1, offsetof(struct __attribute__((aligned(8))) __attribute__((packed)) { char a; int b; }, b));
  ASSERT(1, offsetof(struct { char a; int b; } __attribute__((aligned(8))) __attribute__((packed)), b));

  ASSERT(8, ({ struct __attribute__((aligned(8))) { char a; int b; } __attribute__((packed)) x; _Alignof(x); }));
  ASSERT(1, offsetof(struct __attribute__((aligned(8))) { char a; int b; } __attribute__((packed)), b));

  ASSERT(16, ({ struct __attribute__((aligned(8+8))) { char a; int b; } x; _Alignof(x); }));

  printf("OK\n");
  return 0;
}
