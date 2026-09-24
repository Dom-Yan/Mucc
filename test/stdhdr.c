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

int main() {
  ASSERT(0, ({ regex_t re; regmatch_t m[1]; regcomp(&re, "b+", REG_EXTENDED);
               int r = regexec(&re, "abbc", 1, m, 0); regfree(&re); r; }));
  ASSERT(1, ({ regex_t re; regmatch_t m[1]; regcomp(&re, "b+", REG_EXTENDED);
               regexec(&re, "abbc", 1, m, 0); regfree(&re); m[0].rm_so; }));

  printf("OK\n");
  return 0;
}
