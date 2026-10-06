// <mmintrin.h>: MMX intrinsics, on __m64.
//
// mucc's x86 intrinsics headers (this one, xmmintrin.h for SSE up to
// wmmintrin.h and immintrin.h) are plain C on mucc's vector types (see
// "Vectors" in src/cgen.c), or one instruction in an asm statement where C
// has no operator for it. mucc holds an __m64 in the low half of an XMM
// register, as the psABI passes one, so the MMX registers and emms are
// never needed: the MMX operations are their SSE2 forms.
#ifndef __MMINTRIN_H
#define __MMINTRIN_H

typedef int __m64 __attribute__((__vector_size__(8), __may_alias__));
typedef int __v2si __attribute__((__vector_size__(8)));
typedef short __v4hi __attribute__((__vector_size__(8)));
typedef char __v8qi __attribute__((__vector_size__(8)));
typedef long long __v1di __attribute__((__vector_size__(8)));

#define __MUCC_INLINE static __inline__ __attribute__((__unused__))

// `insn %b, %a` on two vectors of type T, the result in a's register
#define __MUCC_BINARY(name, insn, T) \
  __MUCC_INLINE T name(T __a, T __b) { \
    __asm__(insn " %1, %0" : "+x"(__a) : "x"(__b)); \
    return __a; \
  }

// `insn %a, %a`: on a vector, or on the low element only, keeping the rest
#define __MUCC_UNARY(name, insn, T) \
  __MUCC_INLINE T name(T __a) { \
    __asm__(insn " %0, %0" : "+x"(__a)); \
    return __a; \
  }

// `insn %count, %a`: a shift by the count in an XMM register, whose
// instructions shift every element out at a count of the width or more
#define __MUCC_SHIFT(name, insn, T) \
  __MUCC_INLINE T name(T __a, int __n) { \
    long long __c __attribute__((__vector_size__(16))) = {(unsigned)__n, 0}; \
    __asm__(insn " %1, %0" : "+x"(__a) : "x"(__c)); \
    return __a; \
  }

__MUCC_INLINE void _mm_empty(void) {}

__MUCC_INLINE __m64 _mm_cvtsi32_si64(int __i) { return (__m64)(__v2si){__i, 0}; }
__MUCC_INLINE int _mm_cvtsi64_si32(__m64 __m) { return ((__v2si)__m)[0]; }
__MUCC_INLINE __m64 _mm_cvtsi64_m64(long long __i) { return (__m64)__i; }
__MUCC_INLINE long long _mm_cvtm64_si64(__m64 __m) { return (long long)__m; }
__MUCC_INLINE __m64 _mm_cvtsi64x_si64(long long __i) { return (__m64)__i; }
__MUCC_INLINE __m64 _mm_set_pi64x(long long __i) { return (__m64)__i; }
__MUCC_INLINE long long _mm_cvtsi64_si64x(__m64 __m) { return (long long)__m; }

// a and b side by side in one register, packed into its low half
#define __MUCC_PACK64(name, insn) \
  __MUCC_INLINE __m64 name(__m64 __a, __m64 __b) { \
    __asm__("punpcklqdq %1, %0; " insn " %0, %0" : "+x"(__a) : "x"(__b)); \
    return __a; \
  }

__MUCC_PACK64(_mm_packs_pi16, "packsswb")
__MUCC_PACK64(_mm_packs_pi32, "packssdw")
__MUCC_PACK64(_mm_packs_pu16, "packuswb")
__MUCC_BINARY(_mm_unpacklo_pi8, "punpcklbw", __m64)
__MUCC_BINARY(_mm_unpacklo_pi16, "punpcklwd", __m64)
__MUCC_BINARY(_mm_unpacklo_pi32, "punpckldq", __m64)

__MUCC_INLINE __m64 _mm_unpackhi_pi8(__m64 __a, __m64 __b) {
  __asm__("punpcklbw %1, %0; pshufd $0xee, %0, %0" : "+x"(__a) : "x"(__b));
  return __a;
}

__MUCC_INLINE __m64 _mm_unpackhi_pi16(__m64 __a, __m64 __b) {
  __asm__("punpcklwd %1, %0; pshufd $0xee, %0, %0" : "+x"(__a) : "x"(__b));
  return __a;
}

__MUCC_INLINE __m64 _mm_unpackhi_pi32(__m64 __a, __m64 __b) {
  return (__m64)(__v2si){((__v2si)__a)[1], ((__v2si)__b)[1]};
}

__MUCC_INLINE __m64 _mm_add_pi8(__m64 __a, __m64 __b) { return (__m64)((__v8qi)__a + (__v8qi)__b); }
__MUCC_INLINE __m64 _mm_add_pi16(__m64 __a, __m64 __b) { return (__m64)((__v4hi)__a + (__v4hi)__b); }
__MUCC_INLINE __m64 _mm_add_pi32(__m64 __a, __m64 __b) { return __a + __b; }
__MUCC_INLINE __m64 _mm_sub_pi8(__m64 __a, __m64 __b) { return (__m64)((__v8qi)__a - (__v8qi)__b); }
__MUCC_INLINE __m64 _mm_sub_pi16(__m64 __a, __m64 __b) { return (__m64)((__v4hi)__a - (__v4hi)__b); }
__MUCC_INLINE __m64 _mm_sub_pi32(__m64 __a, __m64 __b) { return __a - __b; }
__MUCC_BINARY(_mm_adds_pi8, "paddsb", __m64)
__MUCC_BINARY(_mm_adds_pi16, "paddsw", __m64)
__MUCC_BINARY(_mm_adds_pu8, "paddusb", __m64)
__MUCC_BINARY(_mm_adds_pu16, "paddusw", __m64)
__MUCC_BINARY(_mm_subs_pi8, "psubsb", __m64)
__MUCC_BINARY(_mm_subs_pi16, "psubsw", __m64)
__MUCC_BINARY(_mm_subs_pu8, "psubusb", __m64)
__MUCC_BINARY(_mm_subs_pu16, "psubusw", __m64)
__MUCC_BINARY(_mm_madd_pi16, "pmaddwd", __m64)
__MUCC_BINARY(_mm_mulhi_pi16, "pmulhw", __m64)
__MUCC_INLINE __m64 _mm_mullo_pi16(__m64 __a, __m64 __b) { return (__m64)((__v4hi)__a * (__v4hi)__b); }

__MUCC_SHIFT(_mm_slli_pi16, "psllw", __m64)
__MUCC_SHIFT(_mm_slli_pi32, "pslld", __m64)
__MUCC_SHIFT(_mm_slli_si64, "psllq", __m64)
__MUCC_SHIFT(_mm_srai_pi16, "psraw", __m64)
__MUCC_SHIFT(_mm_srai_pi32, "psrad", __m64)
__MUCC_SHIFT(_mm_srli_pi16, "psrlw", __m64)
__MUCC_SHIFT(_mm_srli_pi32, "psrld", __m64)
__MUCC_SHIFT(_mm_srli_si64, "psrlq", __m64)
__MUCC_BINARY(_mm_sll_pi16, "psllw", __m64)
__MUCC_BINARY(_mm_sll_pi32, "pslld", __m64)
__MUCC_BINARY(_mm_sll_si64, "psllq", __m64)
__MUCC_BINARY(_mm_sra_pi16, "psraw", __m64)
__MUCC_BINARY(_mm_sra_pi32, "psrad", __m64)
__MUCC_BINARY(_mm_srl_pi16, "psrlw", __m64)
__MUCC_BINARY(_mm_srl_pi32, "psrld", __m64)
__MUCC_BINARY(_mm_srl_si64, "psrlq", __m64)

__MUCC_INLINE __m64 _mm_and_si64(__m64 __a, __m64 __b) { return __a & __b; }
__MUCC_INLINE __m64 _mm_andnot_si64(__m64 __a, __m64 __b) { return ~__a & __b; }
__MUCC_INLINE __m64 _mm_or_si64(__m64 __a, __m64 __b) { return __a | __b; }
__MUCC_INLINE __m64 _mm_xor_si64(__m64 __a, __m64 __b) { return __a ^ __b; }

__MUCC_INLINE __m64 _mm_cmpeq_pi8(__m64 __a, __m64 __b) { return (__m64)((__v8qi)__a == (__v8qi)__b); }
__MUCC_INLINE __m64 _mm_cmpeq_pi16(__m64 __a, __m64 __b) { return (__m64)((__v4hi)__a == (__v4hi)__b); }
__MUCC_INLINE __m64 _mm_cmpeq_pi32(__m64 __a, __m64 __b) { return (__m64)(__a == __b); }
__MUCC_BINARY(_mm_cmpgt_pi8, "pcmpgtb", __m64)
__MUCC_BINARY(_mm_cmpgt_pi16, "pcmpgtw", __m64)
__MUCC_BINARY(_mm_cmpgt_pi32, "pcmpgtd", __m64)

__MUCC_INLINE __m64 _mm_setzero_si64(void) { return (__m64){0, 0}; }
__MUCC_INLINE __m64 _mm_set_pi32(int __i1, int __i0) { return (__m64){__i0, __i1}; }
__MUCC_INLINE __m64 _mm_set_pi16(short __s3, short __s2, short __s1, short __s0) {
  return (__m64)(__v4hi){__s0, __s1, __s2, __s3};
}
__MUCC_INLINE __m64 _mm_set_pi8(char __b7, char __b6, char __b5, char __b4, char __b3,
                                char __b2, char __b1, char __b0) {
  return (__m64)(__v8qi){__b0, __b1, __b2, __b3, __b4, __b5, __b6, __b7};
}
__MUCC_INLINE __m64 _mm_setr_pi32(int __i0, int __i1) { return (__m64){__i0, __i1}; }
__MUCC_INLINE __m64 _mm_setr_pi16(short __w0, short __w1, short __w2, short __w3) {
  return (__m64)(__v4hi){__w0, __w1, __w2, __w3};
}
__MUCC_INLINE __m64 _mm_setr_pi8(char __b0, char __b1, char __b2, char __b3, char __b4,
                                 char __b5, char __b6, char __b7) {
  return (__m64)(__v8qi){__b0, __b1, __b2, __b3, __b4, __b5, __b6, __b7};
}
__MUCC_INLINE __m64 _mm_set1_pi32(int __i) { return (__m64){__i, __i}; }
__MUCC_INLINE __m64 _mm_set1_pi16(short __w) { return (__m64)(__v4hi){__w, __w, __w, __w}; }
__MUCC_INLINE __m64 _mm_set1_pi8(char __b) {
  return (__m64)(__v8qi){__b, __b, __b, __b, __b, __b, __b, __b};
}

// The old names
#define _m_empty _mm_empty
#define _m_from_int _mm_cvtsi32_si64
#define _m_to_int _mm_cvtsi64_si32
#define _m_from_int64 _mm_cvtsi64_m64
#define _m_to_int64 _mm_cvtm64_si64
#define _m_packsswb _mm_packs_pi16
#define _m_packssdw _mm_packs_pi32
#define _m_packuswb _mm_packs_pu16
#define _m_punpckhbw _mm_unpackhi_pi8
#define _m_punpckhwd _mm_unpackhi_pi16
#define _m_punpckhdq _mm_unpackhi_pi32
#define _m_punpcklbw _mm_unpacklo_pi8
#define _m_punpcklwd _mm_unpacklo_pi16
#define _m_punpckldq _mm_unpacklo_pi32
#define _m_paddb _mm_add_pi8
#define _m_paddw _mm_add_pi16
#define _m_paddd _mm_add_pi32
#define _m_paddsb _mm_adds_pi8
#define _m_paddsw _mm_adds_pi16
#define _m_paddusb _mm_adds_pu8
#define _m_paddusw _mm_adds_pu16
#define _m_psubb _mm_sub_pi8
#define _m_psubw _mm_sub_pi16
#define _m_psubd _mm_sub_pi32
#define _m_psubsb _mm_subs_pi8
#define _m_psubsw _mm_subs_pi16
#define _m_psubusb _mm_subs_pu8
#define _m_psubusw _mm_subs_pu16
#define _m_pmaddwd _mm_madd_pi16
#define _m_pmulhw _mm_mulhi_pi16
#define _m_pmullw _mm_mullo_pi16
#define _m_psllw _mm_sll_pi16
#define _m_psllwi _mm_slli_pi16
#define _m_pslld _mm_sll_pi32
#define _m_pslldi _mm_slli_pi32
#define _m_psllq _mm_sll_si64
#define _m_psllqi _mm_slli_si64
#define _m_psraw _mm_sra_pi16
#define _m_psrawi _mm_srai_pi16
#define _m_psrad _mm_sra_pi32
#define _m_psradi _mm_srai_pi32
#define _m_psrlw _mm_srl_pi16
#define _m_psrlwi _mm_srli_pi16
#define _m_psrld _mm_srl_pi32
#define _m_psrldi _mm_srli_pi32
#define _m_psrlq _mm_srl_si64
#define _m_psrlqi _mm_srli_si64
#define _m_pand _mm_and_si64
#define _m_pandn _mm_andnot_si64
#define _m_por _mm_or_si64
#define _m_pxor _mm_xor_si64
#define _m_pcmpeqb _mm_cmpeq_pi8
#define _m_pcmpeqw _mm_cmpeq_pi16
#define _m_pcmpeqd _mm_cmpeq_pi32
#define _m_pcmpgtb _mm_cmpgt_pi8
#define _m_pcmpgtw _mm_cmpgt_pi16
#define _m_pcmpgtd _mm_cmpgt_pi32

#endif
