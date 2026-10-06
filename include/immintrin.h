// <immintrin.h>: every x86 intrinsic mucc has, SSE to SSE4.2, AES and
// carry-less multiplication (see mmintrin.h). It has no AVX: mucc's
// vectors are 16 bytes at most, and it doesn't define __AVX__, so code
// that checks for it uses SSE instead.
#ifndef __IMMINTRIN_H
#define __IMMINTRIN_H

#include <smmintrin.h>
#include <wmmintrin.h>

#endif
