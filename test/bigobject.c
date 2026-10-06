#include "test.h"

// Objects of 2 GiB and more: sizes, offsets and indexes past 32 bits.
// They live in memory from mmap(), reserved but not committed, so only
// the pages touched are used. (Static data can't be this large: x86-64's
// small code model, gcc's default too, keeps it below 2 GiB.)

void *mmap(void *addr, long len, int prot, int flags, int fd, long off);

#define G (1L << 30)

typedef struct {
  char pad[2 * G + 4]; // x is past INT32_MAX
  int x;
  char tail[5];
} Big;

long off_x = __builtin_offsetof(Big, x);
long size_bigs = sizeof(Big[2]);
extern char ext[3 * G];

static long idx(long i) { return i; }

static int get_x(Big *p) { return p->x; }
static void set_x(Big *p, int v) { p->x = v; }

int main() {
  ASSERT(1, sizeof(char[3 * G]) == 3 * G);
  ASSERT(1, sizeof(int[G]) == 4 * G);
  ASSERT(1, sizeof(char[2][2 * G]) == 4 * G);
  ASSERT(1, sizeof(ext) == 3 * G);
  ASSERT(1, sizeof(Big) == 2 * G + 16);
  ASSERT(1, sizeof(Big[2]) == 4 * G + 32);
  ASSERT(1, size_bigs == 4 * G + 32);
  ASSERT(1, __builtin_offsetof(Big, x) == 2 * G + 4);
  ASSERT(1, __builtin_offsetof(Big, tail[4]) == 2 * G + 12);
  ASSERT(1, off_x == 2 * G + 4);
  ASSERT(1, _Alignof(Big) == 4);

  // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE
  char *mem = mmap(0, 4 * G + (1 << 20), 3, 0x4022, -1, 0);
  ASSERT(1, mem != (char *)-1);

  char *a = mem;
  a[3 * G - 1] = 7;
  ASSERT(7, a[3 * G - 1]);
  ASSERT(7, a[idx(3 * G - 1)]);
  ASSERT(7, *(a + 3 * G - 1));

  int *ints = (int *)mem;
  int *last_int = &ints[G - 1];
  ints[G - 1] = 9;
  ASSERT(9, ints[G - 1]);
  ASSERT(9, *last_int);
  ASSERT(1, last_int - ints == G - 1);
  ASSERT(1, (char *)last_int - (char *)ints == 4 * G - 4);

  Big *p = (Big *)mem;
  p->x = 42;
  p->tail[4] = 5;
  ASSERT(42, p->x);
  ASSERT(5, p->tail[4]);
  ASSERT(42, get_x(p));
  ASSERT(1, (char *)&p->x - (char *)p == 2 * G + 4);
  ASSERT(1, &p->tail[4] - (char *)p == 2 * G + 12);
  p->x++;
  ASSERT(43, p->x);
  p->x += 2;
  ASSERT(45, (*p).x);

  Big *bigs = (Big *)mem;
  set_x(&bigs[1], 11);
  ASSERT(11, bigs[1].x);
  ASSERT(1, (char *)&bigs[1] - (char *)&bigs[0] == 2 * G + 16);
  ASSERT(1, &bigs[1] - &bigs[0] == 1);
  Big *q = bigs;
  q++;
  ASSERT(11, q->x);
  ASSERT(1, q - bigs == 1);
  q--;
  ASSERT(1, q == bigs);
  q += 1;
  ASSERT(11, q->x);

  char (*row)[2 * G] = (void *)mem;
  row[1][5] = 3;
  ASSERT(3, a[2 * G + 5]);
  ASSERT(1, (char *)(row + 1) - a == 2 * G);
  ASSERT(1, row[1] - row[0] == 2 * G);

  long n = 3 * G;
  char (*vla)[n] = (void *)mem;
  ASSERT(1, sizeof(*vla) == 3 * G);
  ASSERT(1, (char *)(vla + 1) - a == 3 * G);
  ASSERT(1, sizeof(char[n][2]) == 6 * G);

  printf("OK\n");
  return 0;
}
