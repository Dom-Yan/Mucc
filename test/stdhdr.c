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

int main() {
  ASSERT(12, sizeof(struct epoll_event));
  ASSERT(4, offsetof(struct epoll_event, data));
  ASSERT(16, _Alignof(__pthread_unwind_buf_t));
  ASSERT(0, ({ regex_t re; regmatch_t m[1]; regcomp(&re, "b+", REG_EXTENDED);
               int r = regexec(&re, "abbc", 1, m, 0); regfree(&re); r; }));
  ASSERT(1, ({ regex_t re; regmatch_t m[1]; regcomp(&re, "b+", REG_EXTENDED);
               regexec(&re, "abbc", 1, m, 0); regfree(&re); m[0].rm_so; }));

  printf("OK\n");
  return 0;
}
