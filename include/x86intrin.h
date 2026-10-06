// <x86intrin.h>: <immintrin.h>, and the x86 intrinsics on integers
#ifndef __X86INTRIN_H
#define __X86INTRIN_H

#include <immintrin.h>

static __inline__ __attribute__((__unused__)) unsigned long long __rdtsc(void) {
  unsigned __lo, __hi;
  __asm__ __volatile__("rdtsc" : "=a"(__lo), "=d"(__hi));
  return (unsigned long long)__hi << 32 | __lo;
}

static __inline__ __attribute__((__unused__)) unsigned long long __rdtscp(unsigned *__aux) {
  unsigned __lo, __hi, __c;
  __asm__ __volatile__("rdtscp" : "=a"(__lo), "=d"(__hi), "=c"(__c));
  *__aux = __c;
  return (unsigned long long)__hi << 32 | __lo;
}

#define _rdtsc() __rdtsc()
#define __bsfd(x) __builtin_ctz(x)
#define __bsrd(x) (__builtin_clz(x) ^ 31)
#define __bsfq(x) __builtin_ctzll(x)
#define __bsrq(x) (__builtin_clzll(x) ^ 63)
#define _bit_scan_forward(x) __bsfd(x)
#define _bit_scan_reverse(x) __bsrd(x)
#define __bswapd(x) __builtin_bswap32(x)
#define __bswapq(x) __builtin_bswap64(x)
#define _bswap(x) __builtin_bswap32(x)
#define _bswap64(x) __builtin_bswap64(x)
#define __popcntd(x) __builtin_popcount(x)
#define __popcntq(x) __builtin_popcountll(x)
#define _popcnt32(x) __builtin_popcount(x)
#define _popcnt64(x) __builtin_popcountll(x)

static __inline__ __attribute__((__unused__)) unsigned __rold(unsigned __x, int __n) {
  __n &= 31;
  return __n ? __x << __n | __x >> (32 - __n) : __x;
}

static __inline__ __attribute__((__unused__)) unsigned __rord(unsigned __x, int __n) {
  __n &= 31;
  return __n ? __x >> __n | __x << (32 - __n) : __x;
}

static __inline__ __attribute__((__unused__)) unsigned long long
__rolq(unsigned long long __x, int __n) {
  __n &= 63;
  return __n ? __x << __n | __x >> (64 - __n) : __x;
}

static __inline__ __attribute__((__unused__)) unsigned long long
__rorq(unsigned long long __x, int __n) {
  __n &= 63;
  return __n ? __x >> __n | __x << (64 - __n) : __x;
}

#define _rotl(x, n) __rold(x, n)
#define _rotr(x, n) __rord(x, n)
#define _lrotl(x, n) __rolq(x, n)
#define _lrotr(x, n) __rorq(x, n)

#endif
