// <popcntintrin.h>: the number of one bits, without the popcnt
// instruction, which older x86-64 CPUs lack (see ND_POPCOUNT in cgen.c)
#ifndef __POPCNTINTRIN_H
#define __POPCNTINTRIN_H

static __inline__ __attribute__((__unused__)) int _mm_popcnt_u32(unsigned __x) {
  return __builtin_popcount(__x);
}

static __inline__ __attribute__((__unused__)) long long _mm_popcnt_u64(unsigned long long __x) {
  return __builtin_popcountll(__x);
}

#endif
