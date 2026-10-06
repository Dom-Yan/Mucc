#include "test.h"
#include <float.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

// glibc declares regexec() with a parameter sized by an earlier one:
// regmatch_t pmatch[nmatch].
#include <regex.h>

// glibc's headers keep their attributes for mucc (include/sys/cdefs.h):
// struct epoll_event is packed, and <pthread.h> uses `aligned` and `weak`.
#include <pthread.h>
#include <sys/epoll.h>

// include/tgmath.h, also with glibc: each macro picks the function for
// its arguments' type, an integer's being double's.
#include <tgmath.h>

int main() {
  ASSERT(12, sizeof(struct epoll_event));
  ASSERT(4, offsetof(struct epoll_event, data));
#ifdef __GLIBC__
  ASSERT(16, _Alignof(__pthread_unwind_buf_t));
#endif
  ASSERT(0, ({ regex_t re; regmatch_t m[1]; regcomp(&re, "b+", REG_EXTENDED);
               int r = regexec(&re, "abbc", 1, m, 0); regfree(&re); r; }));
  ASSERT(1, ({ regex_t re; regmatch_t m[1]; regcomp(&re, "b+", REG_EXTENDED);
               regexec(&re, "abbc", 1, m, 0); regfree(&re); m[0].rm_so; }));

  ASSERT(1, ({ float f = 2; _Generic(sqrt(f), float: 1, default: 0); }));
  ASSERT(1, ({ double d = 2; _Generic(sqrt(d), double: 1, default: 0); }));
  ASSERT(1, ({ int i = 2; _Generic(sqrt(i), double: 1, default: 0); }));
  ASSERT(1, ({ long double l = 2; _Generic(sqrt(l), long double: 1, default: 0); }));
  ASSERT(1, ({ float f = 2; double d = 2; _Generic(pow(f, d), double: 1, default: 0); }));
  ASSERT(1, ({ float f = 2; _Generic(fma(f, f, f), float: 1, default: 0); }));
  ASSERT(1, ({ double complex z = 1; _Generic(exp(z), double complex: 1, default: 0); }));
  ASSERT(1, ({ float complex z = 1; _Generic(fabs(z), float: 1, default: 0); }));

  printf("OK\n");
  return 0;
}
