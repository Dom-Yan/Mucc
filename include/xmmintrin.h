// <xmmintrin.h>: SSE intrinsics, on __m128 (see mmintrin.h)
#ifndef __XMMINTRIN_H
#define __XMMINTRIN_H

#include <mmintrin.h>
#include <mm_malloc.h>

typedef float __m128 __attribute__((__vector_size__(16), __may_alias__));
typedef float __m128_u __attribute__((__vector_size__(16), __may_alias__));
typedef float __v4sf __attribute__((__vector_size__(16)));
typedef int __mucc_v4si __attribute__((__vector_size__(16)));
typedef unsigned __mucc_v4su __attribute__((__vector_size__(16)));

#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))

#define _MM_HINT_ET0 7
#define _MM_HINT_ET1 6
#define _MM_HINT_T0 3
#define _MM_HINT_T1 2
#define _MM_HINT_T2 1
#define _MM_HINT_NTA 0

// MXCSR's fields
#define _MM_EXCEPT_INVALID 0x0001
#define _MM_EXCEPT_DENORM 0x0002
#define _MM_EXCEPT_DIV_ZERO 0x0004
#define _MM_EXCEPT_OVERFLOW 0x0008
#define _MM_EXCEPT_UNDERFLOW 0x0010
#define _MM_EXCEPT_INEXACT 0x0020
#define _MM_EXCEPT_MASK 0x003f
#define _MM_MASK_INVALID 0x0080
#define _MM_MASK_DENORM 0x0100
#define _MM_MASK_DIV_ZERO 0x0200
#define _MM_MASK_OVERFLOW 0x0400
#define _MM_MASK_UNDERFLOW 0x0800
#define _MM_MASK_INEXACT 0x1000
#define _MM_MASK_MASK 0x1f80
#define _MM_ROUND_NEAREST 0x0000
#define _MM_ROUND_DOWN 0x2000
#define _MM_ROUND_UP 0x4000
#define _MM_ROUND_TOWARD_ZERO 0x6000
#define _MM_ROUND_MASK 0x6000
#define _MM_FLUSH_ZERO_MASK 0x8000
#define _MM_FLUSH_ZERO_ON 0x8000
#define _MM_FLUSH_ZERO_OFF 0x0000

#define _MM_GET_EXCEPTION_MASK() (_mm_getcsr() & _MM_MASK_MASK)
#define _MM_GET_EXCEPTION_STATE() (_mm_getcsr() & _MM_EXCEPT_MASK)
#define _MM_GET_FLUSH_ZERO_MODE() (_mm_getcsr() & _MM_FLUSH_ZERO_MASK)
#define _MM_GET_ROUNDING_MODE() (_mm_getcsr() & _MM_ROUND_MASK)
#define _MM_SET_EXCEPTION_MASK(m) _mm_setcsr((_mm_getcsr() & ~_MM_MASK_MASK) | (m))
#define _MM_SET_EXCEPTION_STATE(m) _mm_setcsr((_mm_getcsr() & ~_MM_EXCEPT_MASK) | (m))
#define _MM_SET_FLUSH_ZERO_MODE(m) _mm_setcsr((_mm_getcsr() & ~_MM_FLUSH_ZERO_MASK) | (m))
#define _MM_SET_ROUNDING_MODE(m) _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | (m))

// Arithmetic
__MUCC_BINARY(_mm_add_ss, "addss", __m128)
__MUCC_BINARY(_mm_sub_ss, "subss", __m128)
__MUCC_BINARY(_mm_mul_ss, "mulss", __m128)
__MUCC_BINARY(_mm_div_ss, "divss", __m128)
__MUCC_UNARY(_mm_sqrt_ss, "sqrtss", __m128)
__MUCC_UNARY(_mm_rcp_ss, "rcpss", __m128)
__MUCC_UNARY(_mm_rsqrt_ss, "rsqrtss", __m128)
__MUCC_BINARY(_mm_min_ss, "minss", __m128)
__MUCC_BINARY(_mm_max_ss, "maxss", __m128)
__MUCC_INLINE __m128 _mm_add_ps(__m128 __a, __m128 __b) { return __a + __b; }
__MUCC_INLINE __m128 _mm_sub_ps(__m128 __a, __m128 __b) { return __a - __b; }
__MUCC_INLINE __m128 _mm_mul_ps(__m128 __a, __m128 __b) { return __a * __b; }
__MUCC_INLINE __m128 _mm_div_ps(__m128 __a, __m128 __b) { return __a / __b; }
__MUCC_UNARY(_mm_sqrt_ps, "sqrtps", __m128)
__MUCC_UNARY(_mm_rcp_ps, "rcpps", __m128)
__MUCC_UNARY(_mm_rsqrt_ps, "rsqrtps", __m128)
__MUCC_BINARY(_mm_min_ps, "minps", __m128)
__MUCC_BINARY(_mm_max_ps, "maxps", __m128)

// Logic, on the bits
__MUCC_INLINE __m128 _mm_and_ps(__m128 __a, __m128 __b) {
  return (__m128)((__mucc_v4si)__a & (__mucc_v4si)__b);
}
__MUCC_INLINE __m128 _mm_andnot_ps(__m128 __a, __m128 __b) {
  return (__m128)(~(__mucc_v4si)__a & (__mucc_v4si)__b);
}
__MUCC_INLINE __m128 _mm_or_ps(__m128 __a, __m128 __b) {
  return (__m128)((__mucc_v4si)__a | (__mucc_v4si)__b);
}
__MUCC_INLINE __m128 _mm_xor_ps(__m128 __a, __m128 __b) {
  return (__m128)((__mucc_v4si)__a ^ (__mucc_v4si)__b);
}

// Comparisons: all ones where true. > and >= are < and <= the other way
// round; for one element, the others are a's.
#define __MUCC_CMP_SWAPPED(name, insn, T, move) \
  __MUCC_INLINE T name(T __a, T __b) { \
    T __r = __b; \
    __asm__(insn " %1, %0" : "+x"(__r) : "x"(__a)); \
    __asm__(move " %1, %0" : "+x"(__a) : "x"(__r)); \
    return __a; \
  }
#define __MUCC_CMP_SWAPPED_ALL(name, insn, T) \
  __MUCC_INLINE T name(T __a, T __b) { \
    __asm__(insn " %1, %0" : "+x"(__b) : "x"(__a)); \
    return __b; \
  }

__MUCC_BINARY(_mm_cmpeq_ss, "cmpeqss", __m128)
__MUCC_BINARY(_mm_cmplt_ss, "cmpltss", __m128)
__MUCC_BINARY(_mm_cmple_ss, "cmpless", __m128)
__MUCC_CMP_SWAPPED(_mm_cmpgt_ss, "cmpltss", __m128, "movss")
__MUCC_CMP_SWAPPED(_mm_cmpge_ss, "cmpless", __m128, "movss")
__MUCC_BINARY(_mm_cmpneq_ss, "cmpneqss", __m128)
__MUCC_BINARY(_mm_cmpnlt_ss, "cmpnltss", __m128)
__MUCC_BINARY(_mm_cmpnle_ss, "cmpnless", __m128)
__MUCC_CMP_SWAPPED(_mm_cmpngt_ss, "cmpnltss", __m128, "movss")
__MUCC_CMP_SWAPPED(_mm_cmpnge_ss, "cmpnless", __m128, "movss")
__MUCC_BINARY(_mm_cmpord_ss, "cmpordss", __m128)
__MUCC_BINARY(_mm_cmpunord_ss, "cmpunordss", __m128)
__MUCC_BINARY(_mm_cmpeq_ps, "cmpeqps", __m128)
__MUCC_BINARY(_mm_cmplt_ps, "cmpltps", __m128)
__MUCC_BINARY(_mm_cmple_ps, "cmpleps", __m128)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpgt_ps, "cmpltps", __m128)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpge_ps, "cmpleps", __m128)
__MUCC_BINARY(_mm_cmpneq_ps, "cmpneqps", __m128)
__MUCC_BINARY(_mm_cmpnlt_ps, "cmpnltps", __m128)
__MUCC_BINARY(_mm_cmpnle_ps, "cmpnleps", __m128)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpngt_ps, "cmpnltps", __m128)
__MUCC_CMP_SWAPPED_ALL(_mm_cmpnge_ps, "cmpnleps", __m128)
__MUCC_BINARY(_mm_cmpord_ps, "cmpordps", __m128)
__MUCC_BINARY(_mm_cmpunord_ps, "cmpunordps", __m128)

// The low elements compared, as 0 or 1 (with a NaN, only != is 1)
__MUCC_INLINE int _mm_comieq_ss(__m128 __a, __m128 __b) { return __a[0] == __b[0]; }
__MUCC_INLINE int _mm_comilt_ss(__m128 __a, __m128 __b) { return __a[0] < __b[0]; }
__MUCC_INLINE int _mm_comile_ss(__m128 __a, __m128 __b) { return __a[0] <= __b[0]; }
__MUCC_INLINE int _mm_comigt_ss(__m128 __a, __m128 __b) { return __a[0] > __b[0]; }
__MUCC_INLINE int _mm_comige_ss(__m128 __a, __m128 __b) { return __a[0] >= __b[0]; }
__MUCC_INLINE int _mm_comineq_ss(__m128 __a, __m128 __b) { return __a[0] != __b[0]; }
#define _mm_ucomieq_ss _mm_comieq_ss
#define _mm_ucomilt_ss _mm_comilt_ss
#define _mm_ucomile_ss _mm_comile_ss
#define _mm_ucomigt_ss _mm_comigt_ss
#define _mm_ucomige_ss _mm_comige_ss
#define _mm_ucomineq_ss _mm_comineq_ss

// Conversions. cvt rounds as MXCSR says (to nearest, normally); cvtt
// truncates, as C does.
__MUCC_INLINE int _mm_cvtss_si32(__m128 __a) {
  int __r;
  __asm__("cvtss2si %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}
__MUCC_INLINE long long _mm_cvtss_si64(__m128 __a) {
  long long __r;
  __asm__("cvtss2si %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}
__MUCC_INLINE int _mm_cvttss_si32(__m128 __a) { return (int)__a[0]; }
__MUCC_INLINE long long _mm_cvttss_si64(__m128 __a) { return (long long)__a[0]; }
__MUCC_INLINE __m128 _mm_cvtsi32_ss(__m128 __a, int __b) {
  __a[0] = __b;
  return __a;
}
__MUCC_INLINE __m128 _mm_cvtsi64_ss(__m128 __a, long long __b) {
  __a[0] = __b;
  return __a;
}
__MUCC_INLINE float _mm_cvtss_f32(__m128 __a) { return __a[0]; }
#define _mm_cvt_ss2si _mm_cvtss_si32
#define _mm_cvtt_ss2si _mm_cvttss_si32
#define _mm_cvt_si2ss _mm_cvtsi32_ss
#define _mm_cvtss_si64x _mm_cvtss_si64
#define _mm_cvttss_si64x _mm_cvttss_si64
#define _mm_cvtsi64x_ss _mm_cvtsi64_ss

__MUCC_INLINE __m64 _mm_cvtps_pi32(__m128 __a) {
  __asm__("cvtps2dq %0, %0" : "+x"(__a));
  return (__m64)(__v2si){((__mucc_v4si)__a)[0], ((__mucc_v4si)__a)[1]};
}
__MUCC_INLINE __m64 _mm_cvttps_pi32(__m128 __a) { return (__m64){(int)__a[0], (int)__a[1]}; }
__MUCC_INLINE __m128 _mm_cvtpi32_ps(__m128 __a, __m64 __b) {
  __a[0] = __b[0];
  __a[1] = __b[1];
  return __a;
}
__MUCC_INLINE __m128 _mm_cvtpi16_ps(__m64 __a) {
  __v4hi __v = (__v4hi)__a;
  return (__m128){__v[0], __v[1], __v[2], __v[3]};
}
__MUCC_INLINE __m128 _mm_cvtpu16_ps(__m64 __a) {
  __v4hi __v = (__v4hi)__a;
  return (__m128){(unsigned short)__v[0], (unsigned short)__v[1], (unsigned short)__v[2],
                  (unsigned short)__v[3]};
}
__MUCC_INLINE __m128 _mm_cvtpi8_ps(__m64 __a) {
  __v8qi __v = (__v8qi)__a;
  return (__m128){(signed char)__v[0], (signed char)__v[1], (signed char)__v[2],
                  (signed char)__v[3]};
}
__MUCC_INLINE __m128 _mm_cvtpu8_ps(__m64 __a) {
  __v8qi __v = (__v8qi)__a;
  return (__m128){(unsigned char)__v[0], (unsigned char)__v[1], (unsigned char)__v[2],
                  (unsigned char)__v[3]};
}
__MUCC_INLINE __m128 _mm_cvtpi32x2_ps(__m64 __a, __m64 __b) {
  return (__m128){__a[0], __a[1], __b[0], __b[1]};
}
__MUCC_INLINE __m64 _mm_cvtps_pi16(__m128 __a) {
  __asm__("cvtps2dq %0, %0; packssdw %0, %0" : "+x"(__a));
  return (__m64)(__v2si){((__mucc_v4si)__a)[0], ((__mucc_v4si)__a)[1]};
}
__MUCC_INLINE __m64 _mm_cvtps_pi8(__m128 __a) {
  __asm__("cvtps2dq %0, %0; packssdw %0, %0; packsswb %0, %0" : "+x"(__a));
  return (__m64)(__v2si){((__mucc_v4si)__a)[0], 0};
}
#define _mm_cvt_ps2pi _mm_cvtps_pi32
#define _mm_cvtt_ps2pi _mm_cvttps_pi32
#define _mm_cvt_pi2ps _mm_cvtpi32_ps

// Setting, loading and storing. mucc's vector loads and stores take any
// address, so the aligned ones are the unaligned ones.
__MUCC_INLINE __m128 _mm_set_ss(float __w) { return (__m128){__w, 0, 0, 0}; }
__MUCC_INLINE __m128 _mm_set1_ps(float __w) { return (__m128){__w, __w, __w, __w}; }
__MUCC_INLINE __m128 _mm_set_ps1(float __w) { return (__m128){__w, __w, __w, __w}; }
__MUCC_INLINE __m128 _mm_set_ps(float __z, float __y, float __x, float __w) {
  return (__m128){__w, __x, __y, __z};
}
__MUCC_INLINE __m128 _mm_setr_ps(float __z, float __y, float __x, float __w) {
  return (__m128){__z, __y, __x, __w};
}
__MUCC_INLINE __m128 _mm_setzero_ps(void) { return (__m128){0, 0, 0, 0}; }
__MUCC_INLINE __m128 _mm_undefined_ps(void) { return (__m128){0, 0, 0, 0}; }
__MUCC_INLINE __m128 _mm_load_ss(float const *__p) { return (__m128){*__p, 0, 0, 0}; }
__MUCC_INLINE __m128 _mm_load1_ps(float const *__p) { return _mm_set1_ps(*__p); }
__MUCC_INLINE __m128 _mm_load_ps1(float const *__p) { return _mm_set1_ps(*__p); }
__MUCC_INLINE __m128 _mm_load_ps(float const *__p) { return *(__m128 *)__p; }
__MUCC_INLINE __m128 _mm_loadu_ps(float const *__p) { return *(__m128_u *)__p; }
__MUCC_INLINE __m128 _mm_loadr_ps(float const *__p) {
  return (__m128){__p[3], __p[2], __p[1], __p[0]};
}
__MUCC_INLINE __m128 _mm_loadh_pi(__m128 __a, __m64 const *__p) {
  __a[2] = ((float *)__p)[0];
  __a[3] = ((float *)__p)[1];
  return __a;
}
__MUCC_INLINE __m128 _mm_loadl_pi(__m128 __a, __m64 const *__p) {
  __a[0] = ((float *)__p)[0];
  __a[1] = ((float *)__p)[1];
  return __a;
}
__MUCC_INLINE void _mm_store_ss(float *__p, __m128 __a) { *__p = __a[0]; }
__MUCC_INLINE void _mm_store1_ps(float *__p, __m128 __a) {
  *(__m128 *)__p = (__m128){__a[0], __a[0], __a[0], __a[0]};
}
__MUCC_INLINE void _mm_store_ps1(float *__p, __m128 __a) { _mm_store1_ps(__p, __a); }
__MUCC_INLINE void _mm_store_ps(float *__p, __m128 __a) { *(__m128 *)__p = __a; }
__MUCC_INLINE void _mm_storeu_ps(float *__p, __m128 __a) { *(__m128_u *)__p = __a; }
__MUCC_INLINE void _mm_storer_ps(float *__p, __m128 __a) {
  *(__m128 *)__p = (__m128){__a[3], __a[2], __a[1], __a[0]};
}
__MUCC_INLINE void _mm_storeh_pi(__m64 *__p, __m128 __a) {
  ((float *)__p)[0] = __a[2];
  ((float *)__p)[1] = __a[3];
}
__MUCC_INLINE void _mm_storel_pi(__m64 *__p, __m128 __a) {
  ((float *)__p)[0] = __a[0];
  ((float *)__p)[1] = __a[1];
}
__MUCC_INLINE void _mm_stream_ps(float *__p, __m128 __a) {
  __asm__("movntps %1, %0" : "=m"(*(__m128 *)__p) : "x"(__a));
}
__MUCC_INLINE void _mm_stream_pi(__m64 *__p, __m64 __a) { *__p = __a; }

// Moving and shuffling
__MUCC_BINARY(_mm_move_ss, "movss", __m128)
__MUCC_BINARY(_mm_unpackhi_ps, "unpckhps", __m128)
__MUCC_BINARY(_mm_unpacklo_ps, "unpcklps", __m128)
__MUCC_BINARY(_mm_movehl_ps, "movhlps", __m128)
__MUCC_BINARY(_mm_movelh_ps, "movlhps", __m128)
#define _mm_shuffle_ps(a, b, imm) __extension__({ \
    __m128 __mucc_a = (a), __mucc_b = (b); \
    __asm__("shufps %2, %1, %0" : "+x"(__mucc_a) : "x"(__mucc_b), "i"(imm)); \
    __mucc_a; })

__MUCC_INLINE int _mm_movemask_ps(__m128 __a) {
  int __r;
  __asm__("movmskps %1, %0" : "=r"(__r) : "x"(__a));
  return __r;
}

// Integer operations on __m64
__MUCC_BINARY(_mm_max_pi16, "pmaxsw", __m64)
__MUCC_BINARY(_mm_max_pu8, "pmaxub", __m64)
__MUCC_BINARY(_mm_min_pi16, "pminsw", __m64)
__MUCC_BINARY(_mm_min_pu8, "pminub", __m64)
__MUCC_BINARY(_mm_mulhi_pu16, "pmulhuw", __m64)
__MUCC_BINARY(_mm_avg_pu8, "pavgb", __m64)
__MUCC_BINARY(_mm_avg_pu16, "pavgw", __m64)
__MUCC_BINARY(_mm_sad_pu8, "psadbw", __m64)
__MUCC_INLINE int _mm_movemask_pi8(__m64 __a) {
  int __r;
  __asm__("pmovmskb %1, %0" : "=r"(__r) : "x"(__a));
  return __r & 0xff;
}
#define _mm_extract_pi16(a, n) ((int)(unsigned short)((__v4hi)(__m64)(a))[(n) & 3])
#define _mm_insert_pi16(a, d, n) __extension__({ \
    __v4hi __mucc_v = (__v4hi)(__m64)(a); \
    __mucc_v[(n) & 3] = (d); \
    (__m64)__mucc_v; })
#define _mm_shuffle_pi16(a, n) __extension__({ \
    __v4hi __mucc_v = (__v4hi)(__m64)(a); \
    (__m64)(__v4hi){__mucc_v[(n) & 3], __mucc_v[(n) >> 2 & 3], __mucc_v[(n) >> 4 & 3], \
                    __mucc_v[(n) >> 6 & 3]}; })
__MUCC_INLINE void _mm_maskmove_si64(__m64 __a, __m64 __mask, char *__p) {
  for (int __i = 0; __i < 8; __i++)
    if (((__v8qi)__mask)[__i] & 0x80)
      __p[__i] = ((__v8qi)__a)[__i];
}
#define _m_pmaxsw _mm_max_pi16
#define _m_pmaxub _mm_max_pu8
#define _m_pminsw _mm_min_pi16
#define _m_pminub _mm_min_pu8
#define _m_pmovmskb _mm_movemask_pi8
#define _m_pmulhuw _mm_mulhi_pu16
#define _m_pavgb _mm_avg_pu8
#define _m_pavgw _mm_avg_pu16
#define _m_psadbw _mm_sad_pu8
#define _m_pextrw _mm_extract_pi16
#define _m_pinsrw _mm_insert_pi16
#define _m_pshufw _mm_shuffle_pi16
#define _m_maskmovq _mm_maskmove_si64

// The control and status register, fences and prefetches
__MUCC_INLINE unsigned _mm_getcsr(void) {
  unsigned __r;
  __asm__ __volatile__("stmxcsr %0" : "=m"(__r));
  return __r;
}
__MUCC_INLINE void _mm_setcsr(unsigned __r) {
  __asm__ __volatile__("ldmxcsr %0" : : "m"(__r));
}
__MUCC_INLINE void _mm_sfence(void) { __asm__ __volatile__("sfence" ::: "memory"); }
#define _mm_prefetch(p, hint) ((void)(p), (void)(hint))

#define _MM_TRANSPOSE4_PS(r0, r1, r2, r3) do { \
    __m128 __mucc_t0 = _mm_unpacklo_ps((r0), (r1)); \
    __m128 __mucc_t1 = _mm_unpacklo_ps((r2), (r3)); \
    __m128 __mucc_t2 = _mm_unpackhi_ps((r0), (r1)); \
    __m128 __mucc_t3 = _mm_unpackhi_ps((r2), (r3)); \
    (r0) = _mm_movelh_ps(__mucc_t0, __mucc_t1); \
    (r1) = _mm_movehl_ps(__mucc_t1, __mucc_t0); \
    (r2) = _mm_movelh_ps(__mucc_t2, __mucc_t3); \
    (r3) = _mm_movehl_ps(__mucc_t3, __mucc_t2); \
  } while (0)

#endif
