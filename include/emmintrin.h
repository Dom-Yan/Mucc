// <emmintrin.h>: SSE2 intrinsics, on __m128d and __m128i (see mmintrin.h)
#ifndef __EMMINTRIN_H
#define __EMMINTRIN_H

#include <xmmintrin.h>

typedef double __m128d __attribute__((__vector_size__(16), __may_alias__));
typedef long long __m128i __attribute__((__vector_size__(16), __may_alias__));
typedef double __m128d_u __attribute__((__vector_size__(16), __may_alias__));
typedef long long __m128i_u __attribute__((__vector_size__(16), __may_alias__));
typedef double __v2df __attribute__((__vector_size__(16)));
typedef long long __v2di __attribute__((__vector_size__(16)));
typedef unsigned long long __v2du __attribute__((__vector_size__(16)));
typedef int __v4si __attribute__((__vector_size__(16)));
typedef unsigned __v4su __attribute__((__vector_size__(16)));
typedef short __v8hi __attribute__((__vector_size__(16)));
typedef unsigned short __v8hu __attribute__((__vector_size__(16)));
typedef char __v16qi __attribute__((__vector_size__(16)));
typedef signed char __v16qs __attribute__((__vector_size__(16)));
typedef unsigned char __v16qu __attribute__((__vector_size__(16)));

#define _MM_SHUFFLE2(x, y) (((x) << 1) | (y))

// Doubles
__MUCC_BINARY(_mm_add_sd, "addsd", __m128d)
__MUCC_BINARY(_mm_sub_sd, "subsd", __m128d)
__MUCC_BINARY(_mm_mul_sd, "mulsd", __m128d)
__MUCC_BINARY(_mm_div_sd, "divsd", __m128d)
__MUCC_BINARY(_mm_min_sd, "minsd", __m128d)
__MUCC_BINARY(_mm_max_sd, "maxsd", __m128d)
__MUCC_INLINE __m128d _mm_add_pd(__m128d __a, __m128d __b) { return __a + __b; }
__MUCC_INLINE __m128d _mm_sub_pd(__m128d __a, __m128d __b) { return __a - __b; }
__MUCC_INLINE __m128d _mm_mul_pd(__m128d __a, __m128d __b) { return __a * __b; }
__MUCC_INLINE __m128d _mm_div_pd(__m128d __a, __m128d __b) { return __a / __b; }
__MUCC_BINARY(_mm_min_pd, "minpd", __m128d)
__MUCC_BINARY(_mm_max_pd, "maxpd", __m128d)
__MUCC_UNARY(_mm_sqrt_pd, "sqrtpd", __m128d)
// b's low element's root, a's high element
__MUCC_INLINE __m128d _mm_sqrt_sd(__m128d __a, __m128d __b) {
  __asm__("sqrtsd %1, %0" : "+x"(__a) : "x"(__b));
  return __a;
}

__MUCC_INLINE __m128d _mm_and_pd(__m128d __a, __m128d __b) {
  return (__m128d)((__v2di)__a & (__v2di)__b);
}
__MUCC_INLINE __m128d _mm_andnot_pd(__m128d __a, __m128d __b) {
  return (__m128d)(~(__v2di)__a & (__v2di)__b);
}
__MUCC_INLINE __m128d _mm_or_pd(__m128d __a, __m128d __b) {
  return (__m128d)((__v2di)__a | (__v2di)__b);
}
__MUCC_INLINE __m128d _mm_xor_pd(__m128d __a, __m128d __b) {
  return (__m128d)((__v2di)__a ^ (__v2di)__b);
}

__MUCC_BINARY(_mm_cmpeq_sd, "cmpeqsd", __m128d)
__MUCC_BINARY(_mm_cmplt_sd, "cmpltsd", __m128d)
__MUCC_BINARY(_mm_cmple_sd, "cmplesd", __m128d)
__MUCC_CMP_SWAPPED(_mm_cmpgt_sd, "cmpltsd", __m128d, "movsd")
__MUCC_CMP_SWAPPED(_mm_cmpge_sd, "cmplesd", __m128d, "movsd")
__MUCC_BINARY(_mm_cmpneq_sd, "cmpneqsd", __m128d)
__MUCC_BINARY(_mm_cmpnlt_sd, "cmpnltsd", __m128d)
__MUCC_BINARY(_mm_cmpnle_sd, "cmpnlesd", __m128d)
__MUCC_CMP_SWAPPED(_mm_cmpngt_sd, "cmpnltsd", __m128d, "movsd")
__MUCC_CMP_SWAPPED(_mm_cmpnge_sd, "cmpnlesd", __m128d, "movsd")
__MUCC_BINARY(_mm_cmpord_sd, "cmpordsd", __m128d)
__MUCC_BINARY(_mm_cmpunord_sd, "cmpunordsd", __m128d)
__MUCC_BINARY(_mm_cmpeq_pd, "cmpeqpd", __m128d)
__MUCC_BINARY(_mm_cmplt_pd, "cmpltpd", __m128d)
__MUCC_BINARY(_mm_cmple_pd, "cmplepd", __m128d)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpgt_pd, "cmpltpd", __m128d)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpge_pd, "cmplepd", __m128d)
__MUCC_BINARY(_mm_cmpneq_pd, "cmpneqpd", __m128d)
__MUCC_BINARY(_mm_cmpnlt_pd, "cmpnltpd", __m128d)
__MUCC_BINARY(_mm_cmpnle_pd, "cmpnlepd", __m128d)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpngt_pd, "cmpnltpd", __m128d)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpnge_pd, "cmpnlepd", __m128d)
__MUCC_BINARY(_mm_cmpord_pd, "cmpordpd", __m128d)
__MUCC_BINARY(_mm_cmpunord_pd, "cmpunordpd", __m128d)

__MUCC_INLINE int _mm_comieq_sd(__m128d __a, __m128d __b) { return __a[0] == __b[0]; }
__MUCC_INLINE int _mm_comilt_sd(__m128d __a, __m128d __b) { return __a[0] < __b[0]; }
__MUCC_INLINE int _mm_comile_sd(__m128d __a, __m128d __b) { return __a[0] <= __b[0]; }
__MUCC_INLINE int _mm_comigt_sd(__m128d __a, __m128d __b) { return __a[0] > __b[0]; }
__MUCC_INLINE int _mm_comige_sd(__m128d __a, __m128d __b) { return __a[0] >= __b[0]; }
__MUCC_INLINE int _mm_comineq_sd(__m128d __a, __m128d __b) { return __a[0] != __b[0]; }
#define _mm_ucomieq_sd _mm_comieq_sd
#define _mm_ucomilt_sd _mm_comilt_sd
#define _mm_ucomile_sd _mm_comile_sd
#define _mm_ucomigt_sd _mm_comigt_sd
#define _mm_ucomige_sd _mm_comige_sd
#define _mm_ucomineq_sd _mm_comineq_sd

// Conversions
#define __MUCC_CONVERT(name, insn, To, From) \
  __MUCC_INLINE To name(From __a) { \
    To __r; \
    __asm__(insn " %1, %0" : "=x"(__r) : "x"(__a)); \
    return __r; \
  }
__MUCC_CONVERT(_mm_cvtepi32_pd, "cvtdq2pd", __m128d, __m128i)
__MUCC_CONVERT(_mm_cvtepi32_ps, "cvtdq2ps", __m128, __m128i)
__MUCC_CONVERT(_mm_cvtpd_epi32, "cvtpd2dq", __m128i, __m128d)
__MUCC_CONVERT(_mm_cvttpd_epi32, "cvttpd2dq", __m128i, __m128d)
__MUCC_CONVERT(_mm_cvtpd_ps, "cvtpd2ps", __m128, __m128d)
__MUCC_CONVERT(_mm_cvtps_epi32, "cvtps2dq", __m128i, __m128)
__MUCC_CONVERT(_mm_cvttps_epi32, "cvttps2dq", __m128i, __m128)
__MUCC_CONVERT(_mm_cvtps_pd, "cvtps2pd", __m128d, __m128)
__MUCC_INLINE __m64 _mm_cvtpd_pi32(__m128d __a) {
  __m128i __r = _mm_cvtpd_epi32(__a);
  return (__m64)__r[0];
}
__MUCC_INLINE __m64 _mm_cvttpd_pi32(__m128d __a) {
  return (__m64){(int)__a[0], (int)__a[1]};
}
__MUCC_INLINE __m128d _mm_cvtpi32_pd(__m64 __a) {
  return (__m128d){__a[0], __a[1]};
}
__MUCC_INLINE double _mm_cvtsd_f64(__m128d __a) { return __a[0]; }
__MUCC_INLINE int _mm_cvtsd_si32(__m128d __a) {
  int __r;
  __asm__("cvtsd2si %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}
__MUCC_INLINE long long _mm_cvtsd_si64(__m128d __a) {
  long long __r;
  __asm__("cvtsd2si %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}
__MUCC_INLINE int _mm_cvttsd_si32(__m128d __a) { return (int)__a[0]; }
__MUCC_INLINE long long _mm_cvttsd_si64(__m128d __a) { return (long long)__a[0]; }
__MUCC_INLINE __m128 _mm_cvtsd_ss(__m128 __a, __m128d __b) {
  __a[0] = (float)__b[0];
  return __a;
}
__MUCC_INLINE __m128d _mm_cvtsi32_sd(__m128d __a, int __b) {
  __a[0] = __b;
  return __a;
}
__MUCC_INLINE __m128d _mm_cvtsi64_sd(__m128d __a, long long __b) {
  __a[0] = __b;
  return __a;
}
__MUCC_INLINE __m128d _mm_cvtss_sd(__m128d __a, __m128 __b) {
  __a[0] = __b[0];
  return __a;
}
__MUCC_INLINE int _mm_cvtsi128_si32(__m128i __a) { return ((__v4si)__a)[0]; }
__MUCC_INLINE long long _mm_cvtsi128_si64(__m128i __a) { return __a[0]; }
__MUCC_INLINE __m128i _mm_cvtsi32_si128(int __a) { return (__m128i)(__v4si){__a, 0, 0, 0}; }
__MUCC_INLINE __m128i _mm_cvtsi64_si128(long long __a) { return (__m128i){__a, 0}; }
#define _mm_cvtsd_si64x _mm_cvtsd_si64
#define _mm_cvttsd_si64x _mm_cvttsd_si64
#define _mm_cvtsi64x_sd _mm_cvtsi64_sd
#define _mm_cvtsi128_si64x _mm_cvtsi128_si64
#define _mm_cvtsi64x_si128 _mm_cvtsi64_si128

// Setting, loading and storing doubles
__MUCC_INLINE __m128d _mm_set_sd(double __w) { return (__m128d){__w, 0}; }
__MUCC_INLINE __m128d _mm_set1_pd(double __w) { return (__m128d){__w, __w}; }
__MUCC_INLINE __m128d _mm_set_pd1(double __w) { return (__m128d){__w, __w}; }
__MUCC_INLINE __m128d _mm_set_pd(double __x, double __w) { return (__m128d){__w, __x}; }
__MUCC_INLINE __m128d _mm_setr_pd(double __w, double __x) { return (__m128d){__w, __x}; }
__MUCC_INLINE __m128d _mm_setzero_pd(void) { return (__m128d){0, 0}; }
__MUCC_INLINE __m128d _mm_undefined_pd(void) { return (__m128d){0, 0}; }
__MUCC_INLINE __m128d _mm_load_pd(double const *__p) { return *(__m128d *)__p; }
__MUCC_INLINE __m128d _mm_loadu_pd(double const *__p) { return *(__m128d_u *)__p; }
__MUCC_INLINE __m128d _mm_load1_pd(double const *__p) { return (__m128d){*__p, *__p}; }
__MUCC_INLINE __m128d _mm_load_pd1(double const *__p) { return (__m128d){*__p, *__p}; }
__MUCC_INLINE __m128d _mm_loadr_pd(double const *__p) { return (__m128d){__p[1], __p[0]}; }
__MUCC_INLINE __m128d _mm_load_sd(double const *__p) { return (__m128d){*__p, 0}; }
__MUCC_INLINE __m128d _mm_loadh_pd(__m128d __a, double const *__p) {
  __a[1] = *__p;
  return __a;
}
__MUCC_INLINE __m128d _mm_loadl_pd(__m128d __a, double const *__p) {
  __a[0] = *__p;
  return __a;
}
__MUCC_INLINE void _mm_store_sd(double *__p, __m128d __a) { *__p = __a[0]; }
__MUCC_INLINE void _mm_store_pd(double *__p, __m128d __a) { *(__m128d *)__p = __a; }
__MUCC_INLINE void _mm_storeu_pd(double *__p, __m128d __a) { *(__m128d_u *)__p = __a; }
__MUCC_INLINE void _mm_store1_pd(double *__p, __m128d __a) {
  *(__m128d *)__p = (__m128d){__a[0], __a[0]};
}
__MUCC_INLINE void _mm_store_pd1(double *__p, __m128d __a) { _mm_store1_pd(__p, __a); }
__MUCC_INLINE void _mm_storer_pd(double *__p, __m128d __a) {
  *(__m128d *)__p = (__m128d){__a[1], __a[0]};
}
__MUCC_INLINE void _mm_storeh_pd(double *__p, __m128d __a) { *__p = __a[1]; }
__MUCC_INLINE void _mm_storel_pd(double *__p, __m128d __a) { *__p = __a[0]; }
__MUCC_INLINE void _mm_stream_pd(double *__p, __m128d __a) {
  __asm__("movntpd %1, %0" : "=m"(*(__m128d *)__p) : "x"(__a));
}

__MUCC_BINARY(_mm_move_sd, "movsd", __m128d)
__MUCC_BINARY(_mm_unpackhi_pd, "unpckhpd", __m128d)
__MUCC_BINARY(_mm_unpacklo_pd, "unpcklpd", __m128d)
#define _mm_shuffle_pd(a, b, imm) __extension__({ \
    __m128d __mucc_a = (a), __mucc_b = (b); \
    __asm__("shufpd %2, %1, %0" : "+x"(__mucc_a) : "x"(__mucc_b), "i"(imm)); \
    __mucc_a; })
__MUCC_INLINE int _mm_movemask_pd(__m128d __a) {
  int __r;
  __asm__("movmskpd %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}

// Integers
__MUCC_INLINE __m128i _mm_add_epi8(__m128i __a, __m128i __b) {
  return (__m128i)((__v16qu)__a + (__v16qu)__b);
}
__MUCC_INLINE __m128i _mm_add_epi16(__m128i __a, __m128i __b) {
  return (__m128i)((__v8hu)__a + (__v8hu)__b);
}
__MUCC_INLINE __m128i _mm_add_epi32(__m128i __a, __m128i __b) {
  return (__m128i)((__v4su)__a + (__v4su)__b);
}
__MUCC_INLINE __m128i _mm_add_epi64(__m128i __a, __m128i __b) {
  return (__m128i)((__v2du)__a + (__v2du)__b);
}
__MUCC_INLINE __m128i _mm_sub_epi8(__m128i __a, __m128i __b) {
  return (__m128i)((__v16qu)__a - (__v16qu)__b);
}
__MUCC_INLINE __m128i _mm_sub_epi16(__m128i __a, __m128i __b) {
  return (__m128i)((__v8hu)__a - (__v8hu)__b);
}
__MUCC_INLINE __m128i _mm_sub_epi32(__m128i __a, __m128i __b) {
  return (__m128i)((__v4su)__a - (__v4su)__b);
}
__MUCC_INLINE __m128i _mm_sub_epi64(__m128i __a, __m128i __b) {
  return (__m128i)((__v2du)__a - (__v2du)__b);
}
__MUCC_INLINE __m64 _mm_add_si64(__m64 __a, __m64 __b) {
  return (__m64)((__v1di)__a + (__v1di)__b);
}
__MUCC_INLINE __m64 _mm_sub_si64(__m64 __a, __m64 __b) {
  return (__m64)((__v1di)__a - (__v1di)__b);
}
__MUCC_BINARY(_mm_adds_epi8, "paddsb", __m128i)
__MUCC_BINARY(_mm_adds_epi16, "paddsw", __m128i)
__MUCC_BINARY(_mm_adds_epu8, "paddusb", __m128i)
__MUCC_BINARY(_mm_adds_epu16, "paddusw", __m128i)
__MUCC_BINARY(_mm_subs_epi8, "psubsb", __m128i)
__MUCC_BINARY(_mm_subs_epi16, "psubsw", __m128i)
__MUCC_BINARY(_mm_subs_epu8, "psubusb", __m128i)
__MUCC_BINARY(_mm_subs_epu16, "psubusw", __m128i)
__MUCC_BINARY(_mm_avg_epu8, "pavgb", __m128i)
__MUCC_BINARY(_mm_avg_epu16, "pavgw", __m128i)
__MUCC_BINARY(_mm_madd_epi16, "pmaddwd", __m128i)
__MUCC_BINARY(_mm_max_epi16, "pmaxsw", __m128i)
__MUCC_BINARY(_mm_max_epu8, "pmaxub", __m128i)
__MUCC_BINARY(_mm_min_epi16, "pminsw", __m128i)
__MUCC_BINARY(_mm_min_epu8, "pminub", __m128i)
__MUCC_BINARY(_mm_mulhi_epi16, "pmulhw", __m128i)
__MUCC_BINARY(_mm_mulhi_epu16, "pmulhuw", __m128i)
__MUCC_BINARY(_mm_mul_epu32, "pmuludq", __m128i)
__MUCC_BINARY(_mm_sad_epu8, "psadbw", __m128i)
__MUCC_INLINE __m128i _mm_mullo_epi16(__m128i __a, __m128i __b) {
  return (__m128i)((__v8hu)__a * (__v8hu)__b);
}
__MUCC_INLINE __m64 _mm_mul_su32(__m64 __a, __m64 __b) {
  return (__m64)((unsigned long long)(unsigned)__a[0] * (unsigned)__b[0]);
}

// Shifts: by a count in an int or in a vector's low 64 bits, or of the
// whole vector by a constant number of bytes
__MUCC_SHIFT(_mm_slli_epi16, "psllw", __m128i)
__MUCC_SHIFT(_mm_slli_epi32, "pslld", __m128i)
__MUCC_SHIFT(_mm_slli_epi64, "psllq", __m128i)
__MUCC_SHIFT(_mm_srai_epi16, "psraw", __m128i)
__MUCC_SHIFT(_mm_srai_epi32, "psrad", __m128i)
__MUCC_SHIFT(_mm_srli_epi16, "psrlw", __m128i)
__MUCC_SHIFT(_mm_srli_epi32, "psrld", __m128i)
__MUCC_SHIFT(_mm_srli_epi64, "psrlq", __m128i)
__MUCC_BINARY(_mm_sll_epi16, "psllw", __m128i)
__MUCC_BINARY(_mm_sll_epi32, "pslld", __m128i)
__MUCC_BINARY(_mm_sll_epi64, "psllq", __m128i)
__MUCC_BINARY(_mm_sra_epi16, "psraw", __m128i)
__MUCC_BINARY(_mm_sra_epi32, "psrad", __m128i)
__MUCC_BINARY(_mm_srl_epi16, "psrlw", __m128i)
__MUCC_BINARY(_mm_srl_epi32, "psrld", __m128i)
__MUCC_BINARY(_mm_srl_epi64, "psrlq", __m128i)
#define _mm_slli_si128(a, imm) __extension__({ \
    __m128i __mucc_a = (a); \
    __asm__("pslldq %1, %0" : "+x"(__mucc_a) : "i"((imm) > 15 ? 16 : (imm))); \
    __mucc_a; })
#define _mm_srli_si128(a, imm) __extension__({ \
    __m128i __mucc_a = (a); \
    __asm__("psrldq %1, %0" : "+x"(__mucc_a) : "i"((imm) > 15 ? 16 : (imm))); \
    __mucc_a; })
#define _mm_bslli_si128 _mm_slli_si128
#define _mm_bsrli_si128 _mm_srli_si128

// Logic and comparisons
__MUCC_INLINE __m128i _mm_and_si128(__m128i __a, __m128i __b) { return __a & __b; }
__MUCC_INLINE __m128i _mm_andnot_si128(__m128i __a, __m128i __b) { return ~__a & __b; }
__MUCC_INLINE __m128i _mm_or_si128(__m128i __a, __m128i __b) { return __a | __b; }
__MUCC_INLINE __m128i _mm_xor_si128(__m128i __a, __m128i __b) { return __a ^ __b; }
__MUCC_INLINE __m128i _mm_cmpeq_epi8(__m128i __a, __m128i __b) {
  return (__m128i)((__v16qs)__a == (__v16qs)__b);
}
__MUCC_INLINE __m128i _mm_cmpeq_epi16(__m128i __a, __m128i __b) {
  return (__m128i)((__v8hi)__a == (__v8hi)__b);
}
__MUCC_INLINE __m128i _mm_cmpeq_epi32(__m128i __a, __m128i __b) {
  return (__m128i)((__v4si)__a == (__v4si)__b);
}
__MUCC_INLINE __m128i _mm_cmpgt_epi8(__m128i __a, __m128i __b) {
  return (__m128i)((__v16qs)__a > (__v16qs)__b);
}
__MUCC_INLINE __m128i _mm_cmpgt_epi16(__m128i __a, __m128i __b) {
  return (__m128i)((__v8hi)__a > (__v8hi)__b);
}
__MUCC_INLINE __m128i _mm_cmpgt_epi32(__m128i __a, __m128i __b) {
  return (__m128i)((__v4si)__a > (__v4si)__b);
}
__MUCC_INLINE __m128i _mm_cmplt_epi8(__m128i __a, __m128i __b) {
  return (__m128i)((__v16qs)__a < (__v16qs)__b);
}
__MUCC_INLINE __m128i _mm_cmplt_epi16(__m128i __a, __m128i __b) {
  return (__m128i)((__v8hi)__a < (__v8hi)__b);
}
__MUCC_INLINE __m128i _mm_cmplt_epi32(__m128i __a, __m128i __b) {
  return (__m128i)((__v4si)__a < (__v4si)__b);
}

// Packing, unpacking, moving and shuffling
__MUCC_BINARY(_mm_packs_epi16, "packsswb", __m128i)
__MUCC_BINARY(_mm_packs_epi32, "packssdw", __m128i)
__MUCC_BINARY(_mm_packus_epi16, "packuswb", __m128i)
__MUCC_BINARY(_mm_unpackhi_epi8, "punpckhbw", __m128i)
__MUCC_BINARY(_mm_unpackhi_epi16, "punpckhwd", __m128i)
__MUCC_BINARY(_mm_unpackhi_epi32, "punpckhdq", __m128i)
__MUCC_BINARY(_mm_unpackhi_epi64, "punpckhqdq", __m128i)
__MUCC_BINARY(_mm_unpacklo_epi8, "punpcklbw", __m128i)
__MUCC_BINARY(_mm_unpacklo_epi16, "punpcklwd", __m128i)
__MUCC_BINARY(_mm_unpacklo_epi32, "punpckldq", __m128i)
__MUCC_BINARY(_mm_unpacklo_epi64, "punpcklqdq", __m128i)
__MUCC_INLINE int _mm_movemask_epi8(__m128i __a) {
  int __r;
  __asm__("pmovmskb %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}
__MUCC_INLINE __m128i _mm_move_epi64(__m128i __a) { return (__m128i){__a[0], 0}; }
__MUCC_INLINE __m64 _mm_movepi64_pi64(__m128i __a) { return (__m64)__a[0]; }
__MUCC_INLINE __m128i _mm_movpi64_epi64(__m64 __a) { return (__m128i){(long long)__a, 0}; }
#define _mm_extract_epi16(a, imm) \
  ((int)(unsigned short)((__v8hi)(__m128i)(a))[(imm) & 7])
#define _mm_insert_epi16(a, d, imm) __extension__({ \
    __v8hi __mucc_v = (__v8hi)(__m128i)(a); \
    __mucc_v[(imm) & 7] = (d); \
    (__m128i)__mucc_v; })
#define _mm_shuffle_epi32(a, imm) __extension__({ \
    __m128i __mucc_a = (a); \
    __asm__("pshufd %1, %0, %0" : "+x"(__mucc_a) : "i"(imm)); \
    __mucc_a; })
#define _mm_shufflehi_epi16(a, imm) __extension__({ \
    __m128i __mucc_a = (a); \
    __asm__("pshufhw %1, %0, %0" : "+x"(__mucc_a) : "i"(imm)); \
    __mucc_a; })
#define _mm_shufflelo_epi16(a, imm) __extension__({ \
    __m128i __mucc_a = (a); \
    __asm__("pshuflw %1, %0, %0" : "+x"(__mucc_a) : "i"(imm)); \
    __mucc_a; })

// Setting, loading and storing integers
__MUCC_INLINE __m128i _mm_set_epi64x(long long __q1, long long __q0) {
  return (__m128i){__q0, __q1};
}
__MUCC_INLINE __m128i _mm_set_epi64(__m64 __q1, __m64 __q0) {
  return (__m128i){(long long)__q0, (long long)__q1};
}
__MUCC_INLINE __m128i _mm_set_epi32(int __i3, int __i2, int __i1, int __i0) {
  return (__m128i)(__v4si){__i0, __i1, __i2, __i3};
}
__MUCC_INLINE __m128i _mm_set_epi16(short __w7, short __w6, short __w5, short __w4,
                                    short __w3, short __w2, short __w1, short __w0) {
  return (__m128i)(__v8hi){__w0, __w1, __w2, __w3, __w4, __w5, __w6, __w7};
}
__MUCC_INLINE __m128i _mm_set_epi8(char __b15, char __b14, char __b13, char __b12,
                                   char __b11, char __b10, char __b9, char __b8,
                                   char __b7, char __b6, char __b5, char __b4,
                                   char __b3, char __b2, char __b1, char __b0) {
  return (__m128i)(__v16qi){__b0, __b1, __b2, __b3, __b4, __b5, __b6, __b7,
                            __b8, __b9, __b10, __b11, __b12, __b13, __b14, __b15};
}
__MUCC_INLINE __m128i _mm_set1_epi64x(long long __q) { return (__m128i){__q, __q}; }
__MUCC_INLINE __m128i _mm_set1_epi64(__m64 __q) {
  return (__m128i){(long long)__q, (long long)__q};
}
__MUCC_INLINE __m128i _mm_set1_epi32(int __i) { return (__m128i)(__v4si){__i, __i, __i, __i}; }
__MUCC_INLINE __m128i _mm_set1_epi16(short __w) {
  return (__m128i)(__v8hi){__w, __w, __w, __w, __w, __w, __w, __w};
}
__MUCC_INLINE __m128i _mm_set1_epi8(char __b) {
  return (__m128i)(__v16qi){__b, __b, __b, __b, __b, __b, __b, __b,
                            __b, __b, __b, __b, __b, __b, __b, __b};
}
__MUCC_INLINE __m128i _mm_setr_epi64(__m64 __q0, __m64 __q1) {
  return (__m128i){(long long)__q0, (long long)__q1};
}
__MUCC_INLINE __m128i _mm_setr_epi32(int __i0, int __i1, int __i2, int __i3) {
  return (__m128i)(__v4si){__i0, __i1, __i2, __i3};
}
__MUCC_INLINE __m128i _mm_setr_epi16(short __w0, short __w1, short __w2, short __w3,
                                     short __w4, short __w5, short __w6, short __w7) {
  return (__m128i)(__v8hi){__w0, __w1, __w2, __w3, __w4, __w5, __w6, __w7};
}
__MUCC_INLINE __m128i _mm_setr_epi8(char __b0, char __b1, char __b2, char __b3,
                                    char __b4, char __b5, char __b6, char __b7,
                                    char __b8, char __b9, char __b10, char __b11,
                                    char __b12, char __b13, char __b14, char __b15) {
  return (__m128i)(__v16qi){__b0, __b1, __b2, __b3, __b4, __b5, __b6, __b7,
                            __b8, __b9, __b10, __b11, __b12, __b13, __b14, __b15};
}
__MUCC_INLINE __m128i _mm_setzero_si128(void) { return (__m128i){0, 0}; }
__MUCC_INLINE __m128i _mm_undefined_si128(void) { return (__m128i){0, 0}; }
__MUCC_INLINE __m128i _mm_load_si128(__m128i const *__p) { return *__p; }
__MUCC_INLINE __m128i _mm_loadu_si128(__m128i_u const *__p) { return *__p; }
__MUCC_INLINE __m128i _mm_loadl_epi64(__m128i_u const *__p) {
  return (__m128i){*(long long *)__p, 0};
}
__MUCC_INLINE __m128i _mm_loadu_si64(void const *__p) { return (__m128i){*(long long *)__p, 0}; }
__MUCC_INLINE __m128i _mm_loadu_si32(void const *__p) {
  return (__m128i)(__v4si){*(int *)__p, 0, 0, 0};
}
__MUCC_INLINE __m128i _mm_loadu_si16(void const *__p) {
  return (__m128i)(__v8hi){*(short *)__p, 0, 0, 0, 0, 0, 0, 0};
}
__MUCC_INLINE void _mm_store_si128(__m128i *__p, __m128i __a) { *__p = __a; }
__MUCC_INLINE void _mm_storeu_si128(__m128i_u *__p, __m128i __a) { *__p = __a; }
__MUCC_INLINE void _mm_storel_epi64(__m128i_u *__p, __m128i __a) { *(long long *)__p = __a[0]; }
__MUCC_INLINE void _mm_storeu_si64(void *__p, __m128i __a) { *(long long *)__p = __a[0]; }
__MUCC_INLINE void _mm_storeu_si32(void *__p, __m128i __a) { *(int *)__p = ((__v4si)__a)[0]; }
__MUCC_INLINE void _mm_storeu_si16(void *__p, __m128i __a) {
  *(short *)__p = ((__v8hi)__a)[0];
}
__MUCC_INLINE void _mm_maskmoveu_si128(__m128i __a, __m128i __mask, char *__p) {
  for (int __i = 0; __i < 16; __i++)
    if (((__v16qs)__mask)[__i] < 0)
      __p[__i] = ((__v16qi)__a)[__i];
}
__MUCC_INLINE void _mm_stream_si128(__m128i *__p, __m128i __a) {
  __asm__("movntdq %1, %0" : "=m"(*__p) : "x"(__a));
}
__MUCC_INLINE void _mm_stream_si32(int *__p, int __a) { *__p = __a; }
__MUCC_INLINE void _mm_stream_si64(long long *__p, long long __a) { *__p = __a; }

// Casts keep the bits
__MUCC_INLINE __m128 _mm_castpd_ps(__m128d __a) { return (__m128)__a; }
__MUCC_INLINE __m128i _mm_castpd_si128(__m128d __a) { return (__m128i)__a; }
__MUCC_INLINE __m128d _mm_castps_pd(__m128 __a) { return (__m128d)__a; }
__MUCC_INLINE __m128i _mm_castps_si128(__m128 __a) { return (__m128i)__a; }
__MUCC_INLINE __m128d _mm_castsi128_pd(__m128i __a) { return (__m128d)__a; }
__MUCC_INLINE __m128 _mm_castsi128_ps(__m128i __a) { return (__m128)__a; }

// Cache control and fences
__MUCC_INLINE void _mm_clflush(void const *__p) {
  __asm__ __volatile__("clflush %0" : : "m"(*(char *)__p));
}
__MUCC_INLINE void _mm_lfence(void) { __asm__ __volatile__("lfence" ::: "memory"); }
__MUCC_INLINE void _mm_mfence(void) { __asm__ __volatile__("mfence" ::: "memory"); }
__MUCC_INLINE void _mm_pause(void) { __asm__ __volatile__("pause"); }

#endif
