// <smmintrin.h>: SSE4.1 and SSE4.2 intrinsics (see mmintrin.h)
#ifndef __SMMINTRIN_H
#define __SMMINTRIN_H

#include <tmmintrin.h>
#include <popcntintrin.h>

#define _MM_FROUND_TO_NEAREST_INT 0x00
#define _MM_FROUND_TO_NEG_INF 0x01
#define _MM_FROUND_TO_POS_INF 0x02
#define _MM_FROUND_TO_ZERO 0x03
#define _MM_FROUND_CUR_DIRECTION 0x04
#define _MM_FROUND_RAISE_EXC 0x00
#define _MM_FROUND_NO_EXC 0x08
#define _MM_FROUND_NINT (_MM_FROUND_TO_NEAREST_INT | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_FLOOR (_MM_FROUND_TO_NEG_INF | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_CEIL (_MM_FROUND_TO_POS_INF | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_TRUNC (_MM_FROUND_TO_ZERO | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_RINT (_MM_FROUND_CUR_DIRECTION | _MM_FROUND_RAISE_EXC)
#define _MM_FROUND_NEARBYINT (_MM_FROUND_CUR_DIRECTION | _MM_FROUND_NO_EXC)

// `insn $imm, %b, %a`, the result in a's register
#define __MUCC_IMM2(insn, T, a, b, imm) __extension__({ \
    T __mucc_a = (a), __mucc_b = (b); \
    __asm__(insn " %2, %1, %0" : "+x"(__mucc_a) : "x"(__mucc_b), "i"(imm)); \
    __mucc_a; })
// `insn $imm, %a, %a`
#define __MUCC_IMM1(insn, T, a, imm) __extension__({ \
    T __mucc_a = (a); \
    __asm__(insn " %1, %0, %0" : "+x"(__mucc_a) : "i"(imm)); \
    __mucc_a; })

// Blending: b's element where the bit of imm, or the mask element's top
// bit, is set
#define _mm_blend_epi16(a, b, imm) __MUCC_IMM2("pblendw", __m128i, a, b, imm)
#define _mm_blend_ps(a, b, imm) __MUCC_IMM2("blendps", __m128, a, b, imm)
#define _mm_blend_pd(a, b, imm) __MUCC_IMM2("blendpd", __m128d, a, b, imm)
__MUCC_INLINE __m128i _mm_blendv_epi8(__m128i __a, __m128i __b, __m128i __mask) {
  __v16qs __m = (__v16qs)__mask < 0;
  return (__m128i)(((__v16qs)__a & ~__m) | ((__v16qs)__b & __m));
}
__MUCC_INLINE __m128 _mm_blendv_ps(__m128 __a, __m128 __b, __m128 __mask) {
  __v4si __m = (__v4si)__mask < 0;
  return (__m128)(((__v4si)__a & ~__m) | ((__v4si)__b & __m));
}
__MUCC_INLINE __m128d _mm_blendv_pd(__m128d __a, __m128d __b, __m128d __mask) {
  __v2di __m = (__v2di)__mask < 0;
  return (__m128d)(((__v2di)__a & ~__m) | ((__v2di)__b & __m));
}

// Rounding
#define _mm_round_ps(a, mode) __MUCC_IMM1("roundps", __m128, a, mode)
#define _mm_round_pd(a, mode) __MUCC_IMM1("roundpd", __m128d, a, mode)
#define _mm_round_ss(a, b, mode) __MUCC_IMM2("roundss", __m128, a, b, mode)
#define _mm_round_sd(a, b, mode) __MUCC_IMM2("roundsd", __m128d, a, b, mode)
#define _mm_ceil_ps(a) _mm_round_ps(a, _MM_FROUND_CEIL)
#define _mm_ceil_pd(a) _mm_round_pd(a, _MM_FROUND_CEIL)
#define _mm_ceil_ss(a, b) _mm_round_ss(a, b, _MM_FROUND_CEIL)
#define _mm_ceil_sd(a, b) _mm_round_sd(a, b, _MM_FROUND_CEIL)
#define _mm_floor_ps(a) _mm_round_ps(a, _MM_FROUND_FLOOR)
#define _mm_floor_pd(a) _mm_round_pd(a, _MM_FROUND_FLOOR)
#define _mm_floor_ss(a, b) _mm_round_ss(a, b, _MM_FROUND_FLOOR)
#define _mm_floor_sd(a, b) _mm_round_sd(a, b, _MM_FROUND_FLOOR)

// Integers
__MUCC_INLINE __m128i _mm_cmpeq_epi64(__m128i __a, __m128i __b) {
  return (__m128i)(__a == __b);
}
__MUCC_CONVERT(_mm_cvtepi8_epi16, "pmovsxbw", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepi8_epi32, "pmovsxbd", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepi8_epi64, "pmovsxbq", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepi16_epi32, "pmovsxwd", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepi16_epi64, "pmovsxwq", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepi32_epi64, "pmovsxdq", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepu8_epi16, "pmovzxbw", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepu8_epi32, "pmovzxbd", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepu8_epi64, "pmovzxbq", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepu16_epi32, "pmovzxwd", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepu16_epi64, "pmovzxwq", __m128i, __m128i)
__MUCC_CONVERT(_mm_cvtepu32_epi64, "pmovzxdq", __m128i, __m128i)
__MUCC_BINARY(_mm_max_epi8, "pmaxsb", __m128i)
__MUCC_BINARY(_mm_max_epi32, "pmaxsd", __m128i)
__MUCC_BINARY(_mm_max_epu16, "pmaxuw", __m128i)
__MUCC_BINARY(_mm_max_epu32, "pmaxud", __m128i)
__MUCC_BINARY(_mm_min_epi8, "pminsb", __m128i)
__MUCC_BINARY(_mm_min_epi32, "pminsd", __m128i)
__MUCC_BINARY(_mm_min_epu16, "pminuw", __m128i)
__MUCC_BINARY(_mm_min_epu32, "pminud", __m128i)
__MUCC_CONVERT(_mm_minpos_epu16, "phminposuw", __m128i, __m128i)
__MUCC_BINARY(_mm_mul_epi32, "pmuldq", __m128i)
__MUCC_BINARY(_mm_mullo_epi32, "pmulld", __m128i)
__MUCC_BINARY(_mm_packus_epi32, "packusdw", __m128i)
#define _mm_mpsadbw_epu8(a, b, imm) __MUCC_IMM2("mpsadbw", __m128i, a, b, imm)

// Dot products, inserting and extracting
#define _mm_dp_ps(a, b, imm) __MUCC_IMM2("dpps", __m128, a, b, imm)
#define _mm_dp_pd(a, b, imm) __MUCC_IMM2("dppd", __m128d, a, b, imm)
#define _mm_insert_ps(a, b, imm) __MUCC_IMM2("insertps", __m128, a, b, imm)
#define _mm_extract_epi8(a, imm) ((int)(unsigned char)((__v16qi)(__m128i)(a))[(imm) & 15])
#define _mm_extract_epi32(a, imm) (((__v4si)(__m128i)(a))[(imm) & 3])
#define _mm_extract_epi64(a, imm) (((__v2di)(__m128i)(a))[(imm) & 1])
#define _mm_extract_ps(a, imm) (((__v4si)(__m128)(a))[(imm) & 3])
#define __MUCC_INSERT(V, a, d, imm, n) __extension__({ \
    V __mucc_v = (V)(__m128i)(a); \
    __mucc_v[(imm) & (n)] = (d); \
    (__m128i)__mucc_v; })
#define _mm_insert_epi8(a, d, imm) __MUCC_INSERT(__v16qi, a, d, imm, 15)
#define _mm_insert_epi32(a, d, imm) __MUCC_INSERT(__v4si, a, d, imm, 3)
#define _mm_insert_epi64(a, d, imm) __MUCC_INSERT(__v2di, a, d, imm, 1)
#define _MM_EXTRACT_FLOAT(d, a, imm) \
  ((d) = ((__v4sf)(__m128)(a))[(imm) & 3])
#define _MM_MK_INSERTPS_NDX(src, dst, zero) (((src) << 6) | ((dst) << 4) | (zero))
#define _MM_PICK_OUT_PS(a, n) _mm_insert_ps(_mm_setzero_ps(), (a), _MM_MK_INSERTPS_NDX((n), 0, 0x0e))

__MUCC_INLINE __m128i _mm_stream_load_si128(__m128i *__p) { return *__p; }

// Tests: testz is whether a & b is 0, testc whether ~a & b is
__MUCC_INLINE int _mm_testz_si128(__m128i __a, __m128i __b) {
  __m128i __t = __a & __b;
  return (__t[0] | __t[1]) == 0;
}
__MUCC_INLINE int _mm_testc_si128(__m128i __a, __m128i __b) {
  __m128i __t = ~__a & __b;
  return (__t[0] | __t[1]) == 0;
}
__MUCC_INLINE int _mm_testnzc_si128(__m128i __a, __m128i __b) {
  return !_mm_testz_si128(__a, __b) && !_mm_testc_si128(__a, __b);
}
#define _mm_test_all_zeros(a, mask) _mm_testz_si128(a, mask)
#define _mm_test_mix_ones_zeros(a, mask) _mm_testnzc_si128(a, mask)
__MUCC_INLINE int _mm_test_all_ones(__m128i __a) { return (__a[0] & __a[1]) == -1; }

// SSE4.2
__MUCC_INLINE __m128i _mm_cmpgt_epi64(__m128i __a, __m128i __b) {
  return (__m128i)(__a > __b);
}

__MUCC_INLINE unsigned _mm_crc32_u8(unsigned __c, unsigned char __v) {
  __asm__("crc32b %1, %0" : "+r"(__c) : "r"(__v));
  return __c;
}
__MUCC_INLINE unsigned _mm_crc32_u16(unsigned __c, unsigned short __v) {
  __asm__("crc32w %1, %0" : "+r"(__c) : "r"(__v));
  return __c;
}
__MUCC_INLINE unsigned _mm_crc32_u32(unsigned __c, unsigned __v) {
  __asm__("crc32l %1, %0" : "+r"(__c) : "r"(__v));
  return __c;
}
__MUCC_INLINE unsigned long long _mm_crc32_u64(unsigned long long __c, unsigned long long __v) {
  __asm__("crc32q %1, %0" : "+r"(__c) : "r"(__v));
  return __c;
}

#define _SIDD_UBYTE_OPS 0x00
#define _SIDD_UWORD_OPS 0x01
#define _SIDD_SBYTE_OPS 0x02
#define _SIDD_SWORD_OPS 0x03
#define _SIDD_CMP_EQUAL_ANY 0x00
#define _SIDD_CMP_RANGES 0x04
#define _SIDD_CMP_EQUAL_EACH 0x08
#define _SIDD_CMP_EQUAL_ORDERED 0x0c
#define _SIDD_POSITIVE_POLARITY 0x00
#define _SIDD_NEGATIVE_POLARITY 0x10
#define _SIDD_MASKED_POSITIVE_POLARITY 0x20
#define _SIDD_MASKED_NEGATIVE_POLARITY 0x30
#define _SIDD_LEAST_SIGNIFICANT 0x00
#define _SIDD_MOST_SIGNIFICANT 0x40
#define _SIDD_BIT_MASK 0x00
#define _SIDD_UNIT_MASK 0x40

// String comparisons: the index comes back in %ecx, the mask in %xmm0, and
// the rest in the flags, which setCC reads. Explicit lengths are in %eax
// and %edx.
#define _mm_cmpistri(a, b, imm) __extension__({ \
    int __mucc_r; \
    __asm__("pcmpistri %3, %2, %1" : "=c"(__mucc_r) : "x"((__m128i)(a)), "x"((__m128i)(b)), \
            "i"(imm)); \
    __mucc_r; })
#define _mm_cmpistrm(a, b, imm) __extension__({ \
    __m128i __mucc_r; \
    __asm__("pcmpistrm %3, %2, %1; movdqa %%xmm0, %0" : "=x"(__mucc_r) \
            : "x"((__m128i)(a)), "x"((__m128i)(b)), "i"(imm) : "xmm0"); \
    __mucc_r; })
#define __MUCC_CMPISTR(cc, a, b, imm) __extension__({ \
    unsigned char __mucc_r; \
    __asm__("pcmpistri %3, %2, %1; set" cc " %0" : "=r"(__mucc_r) \
            : "x"((__m128i)(a)), "x"((__m128i)(b)), "i"(imm) : "rcx"); \
    (int)__mucc_r; })
#define _mm_cmpistra(a, b, imm) __MUCC_CMPISTR("a", a, b, imm)
#define _mm_cmpistrc(a, b, imm) __MUCC_CMPISTR("c", a, b, imm)
#define _mm_cmpistro(a, b, imm) __MUCC_CMPISTR("o", a, b, imm)
#define _mm_cmpistrs(a, b, imm) __MUCC_CMPISTR("s", a, b, imm)
#define _mm_cmpistrz(a, b, imm) __MUCC_CMPISTR("z", a, b, imm)
#define _mm_cmpestri(a, la, b, lb, imm) __extension__({ \
    int __mucc_r; \
    __asm__("pcmpestri %5, %4, %3" : "=c"(__mucc_r) : "a"((int)(la)), "d"((int)(lb)), \
            "x"((__m128i)(a)), "x"((__m128i)(b)), "i"(imm)); \
    __mucc_r; })
#define _mm_cmpestrm(a, la, b, lb, imm) __extension__({ \
    __m128i __mucc_r; \
    __asm__("pcmpestrm %5, %4, %3; movdqa %%xmm0, %0" : "=x"(__mucc_r) \
            : "a"((int)(la)), "d"((int)(lb)), "x"((__m128i)(a)), "x"((__m128i)(b)), \
              "i"(imm) : "xmm0"); \
    __mucc_r; })
#define __MUCC_CMPESTR(cc, a, la, b, lb, imm) __extension__({ \
    unsigned char __mucc_r; \
    __asm__("pcmpestri %5, %4, %3; set" cc " %0" : "=r"(__mucc_r) \
            : "a"((int)(la)), "d"((int)(lb)), "x"((__m128i)(a)), "x"((__m128i)(b)), \
              "i"(imm) : "rcx"); \
    (int)__mucc_r; })
#define _mm_cmpestra(a, la, b, lb, imm) __MUCC_CMPESTR("a", a, la, b, lb, imm)
#define _mm_cmpestrc(a, la, b, lb, imm) __MUCC_CMPESTR("c", a, la, b, lb, imm)
#define _mm_cmpestro(a, la, b, lb, imm) __MUCC_CMPESTR("o", a, la, b, lb, imm)
#define _mm_cmpestrs(a, la, b, lb, imm) __MUCC_CMPESTR("s", a, la, b, lb, imm)
#define _mm_cmpestrz(a, la, b, lb, imm) __MUCC_CMPESTR("z", a, la, b, lb, imm)

#endif
