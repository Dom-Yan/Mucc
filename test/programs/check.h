// Shared by the suite: CHECK(cond) counts and reports failures.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int checks, failures;

#define CHECK(cond) do {                                                  \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      printf("FAIL %s:%d: %s (errno %d: %s)\n", __FILE__, __LINE__, #cond, \
             errno, strerror(errno));                                     \
    }                                                                     \
  } while (0)

static int done(const char *name) {
  printf("%s: %d checks, %d failed\n", name, checks, failures);
  return failures != 0;
}
