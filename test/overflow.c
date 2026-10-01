// flags: -std=c23
#include "test.h"
#include <stdckdint.h>

#define LONG_MAX 0x7fffffffffffffffL
#define LONG_MIN (-LONG_MAX - 1)
#define ULONG_MAX 0xffffffffffffffffUL
#define INT_MAX 0x7fffffff
#define INT_MIN (-INT_MAX - 1)

// Every pair of 8-bit operands, signed and unsigned, into 8- and 16-bit
// results of both signs, against the exact result computed in long:
// returns the number of wrong answers.
static int check_small(void) {
  int wrong = 0;
  for (int i = -128; i < 256; i++) {
    for (int j = -128; j < 256; j++) {
      // a is signed char or unsigned char by its range, as is b.
      signed char sa = i, sb = j;
      unsigned char ua = i, ub = j;
      long a = i < 128 ? sa : ua;
      long b = j < 128 ? sb : ub;
      long exact[3] = {a + b, a - b, a * b};
      for (int op = 0; op < 3; op++) {
        signed char r1;
        unsigned char r2;
        short r3;
        bool o1, o2, o3;
        if (i < 128 && j < 128) {
          o1 = op == 0 ? ckd_add(&r1, sa, sb) : op == 1 ? ckd_sub(&r1, sa, sb) : ckd_mul(&r1, sa, sb);
          o2 = op == 0 ? ckd_add(&r2, sa, sb) : op == 1 ? ckd_sub(&r2, sa, sb) : ckd_mul(&r2, sa, sb);
          o3 = op == 0 ? ckd_add(&r3, sa, sb) : op == 1 ? ckd_sub(&r3, sa, sb) : ckd_mul(&r3, sa, sb);
        } else if (i < 128) {
          o1 = op == 0 ? ckd_add(&r1, sa, ub) : op == 1 ? ckd_sub(&r1, sa, ub) : ckd_mul(&r1, sa, ub);
          o2 = op == 0 ? ckd_add(&r2, sa, ub) : op == 1 ? ckd_sub(&r2, sa, ub) : ckd_mul(&r2, sa, ub);
          o3 = op == 0 ? ckd_add(&r3, sa, ub) : op == 1 ? ckd_sub(&r3, sa, ub) : ckd_mul(&r3, sa, ub);
        } else if (j < 128) {
          o1 = op == 0 ? ckd_add(&r1, ua, sb) : op == 1 ? ckd_sub(&r1, ua, sb) : ckd_mul(&r1, ua, sb);
          o2 = op == 0 ? ckd_add(&r2, ua, sb) : op == 1 ? ckd_sub(&r2, ua, sb) : ckd_mul(&r2, ua, sb);
          o3 = op == 0 ? ckd_add(&r3, ua, sb) : op == 1 ? ckd_sub(&r3, ua, sb) : ckd_mul(&r3, ua, sb);
        } else {
          o1 = op == 0 ? ckd_add(&r1, ua, ub) : op == 1 ? ckd_sub(&r1, ua, ub) : ckd_mul(&r1, ua, ub);
          o2 = op == 0 ? ckd_add(&r2, ua, ub) : op == 1 ? ckd_sub(&r2, ua, ub) : ckd_mul(&r2, ua, ub);
          o3 = op == 0 ? ckd_add(&r3, ua, ub) : op == 1 ? ckd_sub(&r3, ua, ub) : ckd_mul(&r3, ua, ub);
        }
        long e = exact[op];
        wrong += o1 != (e < -128 || e > 127) || r1 != (signed char)e;
        wrong += o2 != (e < 0 || e > 255) || r2 != (unsigned char)e;
        wrong += o3 != (e < -32768 || e > 32767) || r3 != (short)e;
      }
    }
  }
  return wrong;
}

int main() {
  ASSERT(0, check_small());

  // int
  ASSERT(0, ({ int r; ckd_add(&r, 1, 2); }));
  ASSERT(3, ({ int r; ckd_add(&r, 1, 2); r; }));
  ASSERT(1, ({ int r; ckd_add(&r, INT_MAX, 1); }));
  ASSERT(INT_MIN, ({ int r; ckd_add(&r, INT_MAX, 1); r; }));
  ASSERT(1, ({ int r; ckd_sub(&r, INT_MIN, 1); }));
  ASSERT(0, ({ int r; ckd_sub(&r, 0u, 1u); }));
  ASSERT(-1, ({ int r; ckd_sub(&r, 0u, 1u); r; }));
  ASSERT(1, ({ unsigned r; ckd_sub(&r, 0, 1); }));
  ASSERT(1, ({ int r; ckd_mul(&r, INT_MIN, -1); }));
  ASSERT(0, ({ long r; ckd_mul(&r, INT_MIN, -1); }));
  ASSERT(0, ({ int r; ckd_mul(&r, 46340, 46340); }));
  ASSERT(1, ({ int r; ckd_mul(&r, 46341, 46341); }));

  // long and unsigned long, where the exact result needs 65 to 128 bits
  ASSERT(1, ({ long r; ckd_add(&r, LONG_MAX, 1); }));
  ASSERT(0, ({ unsigned long r; ckd_add(&r, LONG_MAX, 1); }));
  ASSERT(1, ({ unsigned long r; ckd_add(&r, ULONG_MAX, 1); }));
  ASSERT(0, ({ unsigned long r; ckd_add(&r, ULONG_MAX, 1); r; }));
  ASSERT(0, ({ long r; ckd_add(&r, ULONG_MAX, LONG_MIN); }));
  ASSERT(1, ({ long r; ckd_add(&r, ULONG_MAX, LONG_MIN); r == LONG_MAX; }));
  ASSERT(1, ({ long r; ckd_sub(&r, LONG_MIN, 1); }));
  ASSERT(0, ({ long r; ckd_sub(&r, -1, LONG_MAX); }));
  ASSERT(1, ({ long r; ckd_sub(&r, -2, LONG_MAX); }));
  ASSERT(1, ({ unsigned long r; ckd_sub(&r, 0UL, 1UL); }));
  ASSERT(0, ({ long r; ckd_sub(&r, 0UL, ULONG_MAX / 2 + 1); }));
  ASSERT(1, ({ long r; ckd_sub(&r, 0UL, ULONG_MAX / 2 + 1); r == LONG_MIN; }));
  ASSERT(1, ({ long r; ckd_sub(&r, 0UL, ULONG_MAX / 2 + 2); }));
  ASSERT(1, ({ long r; ckd_mul(&r, LONG_MIN, -1); }));
  ASSERT(0, ({ unsigned long r; ckd_mul(&r, LONG_MIN, -1); }));
  ASSERT(1, ({ unsigned long r; ckd_mul(&r, LONG_MIN, -1); r == 1UL << 63; }));
  ASSERT(1, ({ unsigned long r; ckd_mul(&r, ULONG_MAX, ULONG_MAX); }));
  ASSERT(1, ({ unsigned long r; ckd_mul(&r, ULONG_MAX, ULONG_MAX); r == 1; }));
  ASSERT(1, ({ long r; ckd_mul(&r, -1, ULONG_MAX); }));
  ASSERT(0, ({ long r; ckd_mul(&r, -1, ULONG_MAX / 2); }));
  ASSERT(1, ({ long r; ckd_mul(&r, -1, ULONG_MAX / 2); r == -LONG_MAX; }));
  ASSERT(1, ({ unsigned long r; ckd_mul(&r, -2, 3UL); }));
  ASSERT(0, ({ long r; ckd_mul(&r, 1L << 31, 1L << 31); }));
  ASSERT(1, ({ long r; ckd_mul(&r, 1L << 32, 1L << 31); }));
  ASSERT(0, ({ long r; ckd_mul(&r, -(1L << 32), 1L << 31); }));
  ASSERT(1, ({ long r; ckd_mul(&r, -(1L << 32), 1L << 31); r == LONG_MIN; }));
  ASSERT(0, ({ unsigned long r; ckd_mul(&r, 1UL << 32, (1UL << 32) - 1); }));
  ASSERT(1, ({ unsigned long r; ckd_mul(&r, 1UL << 32, 1UL << 32); }));

  // Operands of different widths, and side effects done once
  ASSERT(1, ({ short r; ckd_add(&r, 30000, 30000L); }));
  ASSERT(0, ({ int r; ckd_add(&r, (short)30000, (char)100); }));
  ASSERT(3, ({ int i = 0, r; ckd_add(&r, i++, i++); ckd_add(&r, i++, 0); i; }));

  // gcc's own names, and the typed forms
  ASSERT(1, ({ int r; __builtin_add_overflow(INT_MAX, 1, &r); }));
  ASSERT(1, ({ int r; __builtin_sadd_overflow(INT_MAX, 1, &r); }));
  ASSERT(0, ({ long r; __builtin_saddl_overflow(INT_MAX, 1, &r); }));
  ASSERT(1, ({ long long r; __builtin_smulll_overflow(LONG_MAX, 2, &r); }));
  ASSERT(1, ({ unsigned r; __builtin_usub_overflow(1, 2, &r); }));
  ASSERT(0, ({ unsigned long r; __builtin_umull_overflow(1UL << 31, 1UL << 32, &r); }));
  ASSERT(1, ({ unsigned long long r; __builtin_uaddll_overflow(ULONG_MAX, 1, &r); }));

  printf("OK\n");
  return 0;
}
