// <tmmintrin.h>: SSSE3 intrinsics (see mmintrin.h)
#ifndef __TMMINTRIN_H
#define __TMMINTRIN_H

#include <pmmintrin.h>

__MUCC_UNARY(_mm_abs_epi8, "pabsb", __m128i)
__MUCC_UNARY(_mm_abs_epi16, "pabsw", __m128i)
__MUCC_UNARY(_mm_abs_epi32, "pabsd", __m128i)
__MUCC_UNARY(_mm_abs_pi8, "pabsb", __m64)
__MUCC_UNARY(_mm_abs_pi16, "pabsw", __m64)
__MUCC_UNARY(_mm_abs_pi32, "pabsd", __m64)
__MUCC_BINARY(_mm_hadd_epi16, "phaddw", __m128i)
__MUCC_BINARY(_mm_hadd_epi32, "phaddd", __m128i)
__MUCC_BINARY(_mm_hadds_epi16, "phaddsw", __m128i)
__MUCC_BINARY(_mm_hsub_epi16, "phsubw", __m128i)
__MUCC_BINARY(_mm_hsub_epi32, "phsubd", __m128i)
__MUCC_BINARY(_mm_hsubs_epi16, "phsubsw", __m128i)
__MUCC_BINARY(_mm_maddubs_epi16, "pmaddubsw", __m128i)
__MUCC_BINARY(_mm_mulhrs_epi16, "pmulhrsw", __m128i)
__MUCC_BINARY(_mm_shuffle_epi8, "pshufb", __m128i)
__MUCC_BINARY(_mm_sign_epi8, "psignb", __m128i)
__MUCC_BINARY(_mm_sign_epi16, "psignw", __m128i)
__MUCC_BINARY(_mm_sign_epi32, "psignd", __m128i)
__MUCC_BINARY(_mm_maddubs_pi16, "pmaddubsw", __m64)
__MUCC_BINARY(_mm_mulhrs_pi16, "pmulhrsw", __m64)
__MUCC_BINARY(_mm_sign_pi8, "psignb", __m64)
__MUCC_BINARY(_mm_sign_pi16, "psignw", __m64)
__MUCC_BINARY(_mm_sign_pi32, "psignd", __m64)

// The horizontal ones on __m64 work on a and b side by side.
#define __MUCC_HORIZONTAL64(name, insn) \
  __MUCC_INLINE __m64 name(__m64 __a, __m64 __b) { \
    __asm__("punpcklqdq %1, %0; " insn " %0, %0" : "+x"(__a) : "x"(__b)); \
    return __a; \
  }
__MUCC_HORIZONTAL64(_mm_hadd_pi16, "phaddw")
__MUCC_HORIZONTAL64(_mm_hadd_pi32, "phaddd")
__MUCC_HORIZONTAL64(_mm_hadds_pi16, "phaddsw")
__MUCC_HORIZONTAL64(_mm_hsub_pi16, "phsubw")
__MUCC_HORIZONTAL64(_mm_hsub_pi32, "phsubd")
__MUCC_HORIZONTAL64(_mm_hsubs_pi16, "phsubsw")

// pshufb on __m64 indexes 8 bytes: only bits 7 and 0-2 of an index count.
__MUCC_INLINE __m64 _mm_shuffle_pi8(__m64 __a, __m64 __b) {
  __m128i __v = {(long long)__a, 0};
  __m128i __m = {(long long)__b & 0x8787878787878787LL, 0};
  __asm__("pshufb %1, %0" : "+x"(__v) : "x"(__m));
  return (__m64)__v[0];
}

// a and b side by side (b low), shifted right by `imm` bytes
#define _mm_alignr_epi8(a, b, imm) __extension__({ \
    __m128i __mucc_a = (a), __mucc_b = (b); \
    __asm__("palignr %2, %1, %0" : "+x"(__mucc_a) : "x"(__mucc_b), "i"(imm)); \
    __mucc_a; })
#define _mm_alignr_pi8(a, b, imm) __extension__({ \
    unsigned __int128 __mucc_t = (unsigned __int128)(unsigned long long)(__m64)(a) << 64 | \
                                 (unsigned long long)(__m64)(b); \
    (__m64)(long long)((imm) > 15 ? 0 : __mucc_t >> (imm) * 8); })

#endif
