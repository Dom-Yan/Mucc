// <wmmintrin.h>: AES and carry-less multiplication intrinsics (see
// mmintrin.h)
#ifndef __WMMINTRIN_H
#define __WMMINTRIN_H

#include <emmintrin.h>

__MUCC_BINARY(_mm_aesenc_si128, "aesenc", __m128i)
__MUCC_BINARY(_mm_aesenclast_si128, "aesenclast", __m128i)
__MUCC_BINARY(_mm_aesdec_si128, "aesdec", __m128i)
__MUCC_BINARY(_mm_aesdeclast_si128, "aesdeclast", __m128i)
__MUCC_UNARY(_mm_aesimc_si128, "aesimc", __m128i)
#define _mm_aeskeygenassist_si128(a, imm) __extension__({ \
    __m128i __mucc_a = (a); \
    __asm__("aeskeygenassist %1, %0, %0" : "+x"(__mucc_a) : "i"(imm)); \
    __mucc_a; })
#define _mm_clmulepi64_si128(a, b, imm) __extension__({ \
    __m128i __mucc_a = (a), __mucc_b = (b); \
    __asm__("pclmulqdq %2, %1, %0" : "+x"(__mucc_a) : "x"(__mucc_b), "i"(imm)); \
    __mucc_a; })

#endif
