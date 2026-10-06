// <pmmintrin.h>: SSE3 intrinsics (see mmintrin.h)
#ifndef __PMMINTRIN_H
#define __PMMINTRIN_H

#include <emmintrin.h>

#define _MM_DENORMALS_ZERO_MASK 0x0040
#define _MM_DENORMALS_ZERO_ON 0x0040
#define _MM_DENORMALS_ZERO_OFF 0x0000
#define _MM_GET_DENORMALS_ZERO_MODE() (_mm_getcsr() & _MM_DENORMALS_ZERO_MASK)
#define _MM_SET_DENORMALS_ZERO_MODE(m) \
  _mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | (m))

__MUCC_BINARY(_mm_addsub_ps, "addsubps", __m128)
__MUCC_BINARY(_mm_addsub_pd, "addsubpd", __m128d)
__MUCC_BINARY(_mm_hadd_ps, "haddps", __m128)
__MUCC_BINARY(_mm_hadd_pd, "haddpd", __m128d)
__MUCC_BINARY(_mm_hsub_ps, "hsubps", __m128)
__MUCC_BINARY(_mm_hsub_pd, "hsubpd", __m128d)
__MUCC_UNARY(_mm_movehdup_ps, "movshdup", __m128)
__MUCC_UNARY(_mm_moveldup_ps, "movsldup", __m128)
__MUCC_UNARY(_mm_movedup_pd, "movddup", __m128d)
__MUCC_INLINE __m128i _mm_lddqu_si128(__m128i_u const *__p) { return *__p; }
__MUCC_INLINE __m128d _mm_loaddup_pd(double const *__p) { return (__m128d){*__p, *__p}; }

#endif
