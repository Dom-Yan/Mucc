#include "test.h"

void _exit(int n);
int fflush(void *stream);

// Constructors run before main and destructors after it: lowest priority
// first for constructors, last for destructors, and those without a
// priority after (or before) them, in the order they're defined, as with
// gcc. Each one adds a letter to `ran`.
static char ran[16];
static int nran;
static void add(char c) { ran[nran++] = c; }

__attribute__((constructor)) static void c_first(void) { add('n'); }
__attribute__((constructor(200))) static void c200(void) { add('b'); }
[[gnu::constructor(101)]] static void c101(void) { add('a'); }

// On an earlier declaration
static void c_declared(void) __attribute__((constructor));
static void c_declared(void) { add('m'); }

__attribute__((constructor)) void c_global(void) { add('g'); }

// A static inline function nothing calls is left out, unless it's a
// constructor or `used`.
__attribute__((constructor)) static inline void c_inline(void) { add('i'); }
__attribute__((used)) static inline int kept(void) { return 7; }

__attribute__((destructor)) static void d_first(void) { add('z'); }
__attribute__((destructor(200))) static void d200(void) { add('y'); }

// The last destructor to run checks them all and prints OK, so a
// destructor that doesn't run fails too.
__attribute__((__destructor__(101))) static void d101(void) {
  add('x');
  if (strcmp(ran, "abnmgizyx")) {
    printf("constructors and destructors ran as %s\n", ran);
    fflush(0);
    _exit(1);
  }
  printf("OK\n");
}

int main() {
  ASSERT(0, strcmp(ran, "abnmgi"));
  ASSERT(6, nran);
  return 0;
}
