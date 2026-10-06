// <cpuid.h>: the cpuid instruction, as gcc's header has it, for choosing
// code by what the CPU has at run time
#ifndef __CPUID_H
#define __CPUID_H

// Leaf 1, %ecx
#define bit_SSE3 (1 << 0)
#define bit_PCLMUL (1 << 1)
#define bit_LZCNT (1 << 5)
#define bit_SSSE3 (1 << 9)
#define bit_FMA (1 << 12)
#define bit_CMPXCHG16B (1 << 13)
#define bit_SSE4_1 (1 << 19)
#define bit_SSE4_2 (1 << 20)
#define bit_MOVBE (1 << 22)
#define bit_POPCNT (1 << 23)
#define bit_AES (1 << 25)
#define bit_XSAVE (1 << 26)
#define bit_OSXSAVE (1 << 27)
#define bit_AVX (1 << 28)
#define bit_F16C (1 << 29)
#define bit_RDRND (1 << 30)

// Leaf 1, %edx
#define bit_CMPXCHG8B (1 << 8)
#define bit_CMOV (1 << 15)
#define bit_MMX (1 << 23)
#define bit_FXSAVE (1 << 24)
#define bit_SSE (1 << 25)
#define bit_SSE2 (1 << 26)

// Leaf 0x80000001, %ecx and %edx
#define bit_LAHF_LM (1 << 0)
#define bit_ABM (1 << 5)
#define bit_SSE4a (1 << 6)
#define bit_PRFCHW (1 << 8)
#define bit_XOP (1 << 11)
#define bit_LWP (1 << 15)
#define bit_FMA4 (1 << 16)
#define bit_TBM (1 << 21)
#define bit_MWAITX (1 << 29)
#define bit_MMXEXT (1 << 22)
#define bit_LM (1 << 29)
#define bit_3DNOWP (1 << 30)
#define bit_3DNOW (1u << 31)

// Leaf 7, subleaf 0, %ebx and %ecx
#define bit_FSGSBASE (1 << 0)
#define bit_SGX (1 << 2)
#define bit_BMI (1 << 3)
#define bit_HLE (1 << 4)
#define bit_AVX2 (1 << 5)
#define bit_BMI2 (1 << 8)
#define bit_RTM (1 << 11)
#define bit_AVX512F (1 << 16)
#define bit_AVX512DQ (1 << 17)
#define bit_RDSEED (1 << 18)
#define bit_ADX (1 << 19)
#define bit_AVX512IFMA (1 << 21)
#define bit_CLFLUSHOPT (1 << 23)
#define bit_CLWB (1 << 24)
#define bit_AVX512CD (1 << 28)
#define bit_SHA (1 << 29)
#define bit_AVX512BW (1 << 30)
#define bit_AVX512VL (1u << 31)
#define bit_PREFETCHWT1 (1 << 0)
#define bit_AVX512VBMI (1 << 1)
#define bit_PKU (1 << 3)
#define bit_OSPKE (1 << 4)
#define bit_GFNI (1 << 8)
#define bit_VAES (1 << 9)
#define bit_VPCLMULQDQ (1 << 10)
#define bit_AVX512VNNI (1 << 11)
#define bit_AVX512BITALG (1 << 12)
#define bit_AVX512VPOPCNTDQ (1 << 14)
#define bit_RDPID (1 << 22)

#define __cpuid(level, a, b, c, d) \
  __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(level))

#define __cpuid_count(level, count, a, b, c, d) \
  __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(level), "2"(count))

// The highest leaf of the basic (ext 0) or extended (0x80000000) range,
// and the vendor's signature's first word in *sig
static __inline__ __attribute__((__unused__)) unsigned
__get_cpuid_max(unsigned __ext, unsigned *__sig) {
  unsigned __a, __b, __c, __d;
  __cpuid(__ext, __a, __b, __c, __d);
  if (__sig)
    *__sig = __b;
  return __a;
}

static __inline__ __attribute__((__unused__)) int
__get_cpuid(unsigned __leaf, unsigned *__a, unsigned *__b, unsigned *__c, unsigned *__d) {
  unsigned __ext = __leaf & 0x80000000;
  if (__get_cpuid_max(__ext, 0) < __leaf)
    return 0;
  __cpuid(__leaf, *__a, *__b, *__c, *__d);
  return 1;
}

static __inline__ __attribute__((__unused__)) int
__get_cpuid_count(unsigned __leaf, unsigned __subleaf, unsigned *__a, unsigned *__b,
                  unsigned *__c, unsigned *__d) {
  unsigned __ext = __leaf & 0x80000000;
  if (__get_cpuid_max(__ext, 0) < __leaf)
    return 0;
  __cpuid_count(__leaf, __subleaf, *__a, *__b, *__c, *__d);
  return 1;
}

#endif
