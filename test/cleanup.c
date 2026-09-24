#include "test.h"

// __attribute__((cleanup(fn))) calls fn(&var) whenever var goes out of
// scope: at the end of its block, and on break, continue, return and goto.
// Each cleanup here adds a character to `trail`.
static char trail[32];
static int ntrail;

static void note(int *p) { trail[ntrail++] = '0' + *p; }
static void zero(int *p) { *p = 0; }
static inline void inl(int *p) { trail[ntrail++] = 'i'; }
static void any(void *p) { trail[ntrail++] = 'v'; }

// Returns what has been noted since the last call.
static char *take(void) {
  trail[ntrail] = '\0';
  ntrail = 0;
  return trail;
}

static void block_end(void) {
  {
    int a __attribute__((cleanup(note))) = 1;
    int b __attribute__((cleanup(note))) = 2;
  }
  [[gnu::cleanup(note)]] int c = 3;
}

// The value is computed before the cleanups run.
static int ret_value(void) {
  int a __attribute__((cleanup(zero))) = 5;
  return a;
}

static int ret_nested(void) {
  int a __attribute__((cleanup(note))) = 1;
  {
    int b __attribute__((cleanup(note))) = 2;
    return 7;
  }
}

static void ret_void(void) {
  int a __attribute__((cleanup(note))) = 4;
  if (a)
    return;
  a = 9;
}

static void loops(void) {
  for (int i = 0; i < 3; i++) {
    int c __attribute__((cleanup(note))) = i;
    if (i == 1)
      continue;
    if (i == 2)
      break;
  }
  int j = 0;
  while (1) {
    int w __attribute__((cleanup(note))) = j;
    if (++j == 2)
      break;
  }
  do {
    int d __attribute__((cleanup(note))) = 8;
  } while (0);
}

static void for_init(void) {
  for (int i __attribute__((cleanup(note))) = 7; i < 9; i++)
    ;
}

static void gotos(void) {
  {
    int g __attribute__((cleanup(note))) = 5;
    goto out;
  }
out:;
  int k = 0;
again:;
  {
    int h __attribute__((cleanup(note))) = k;
    if (++k < 3)
      goto again;
  }
}

static int in_switch(int x) {
  switch (x) {
  case 1: {
    int s __attribute__((cleanup(note))) = 6;
    break;
  }
  case 2:
    return 2;
  }
  return 0;
}

static void kinds(void) {
  int a __attribute__((cleanup(inl))) = 0;
  char *p __attribute__((cleanup(any))) = 0;
}

int main() {
  block_end();
  ASSERT(0, strcmp(take(), "213"));
  ASSERT(5, ret_value());
  ASSERT(7, ret_nested());
  ASSERT(0, strcmp(take(), "21"));
  ret_void();
  ASSERT(0, strcmp(take(), "4"));
  loops();
  ASSERT(0, strcmp(take(), "012018"));
  for_init();
  ASSERT(0, strcmp(take(), "9"));
  gotos();
  ASSERT(0, strcmp(take(), "5012"));
  ASSERT(0, in_switch(1));
  ASSERT(0, strcmp(take(), "6"));
  kinds();
  ASSERT(0, strcmp(take(), "vi"));

  printf("OK\n");
  return 0;
}
