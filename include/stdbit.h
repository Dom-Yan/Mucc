#ifndef __STDBIT_H
#define __STDBIT_H

#define __STDC_VERSION_STDBIT_H__ 202311L

// C23's bit utilities, for musl, which lacks them, and glibc alike: a
// function for each unsigned type (_uc, _us, _ui, _ul, _ull) on gcc's bit
// builtins, and a type-generic macro over them.

#define __STDC_ENDIAN_LITTLE__ 1234
#define __STDC_ENDIAN_BIG__ 4321
#define __STDC_ENDIAN_NATIVE__ __STDC_ENDIAN_LITTLE__

// T is the type, N its width, U the type the builtins work on (Z its
// builtins' suffix: "" for unsigned int, l or ll) and W U's width.
#define __STDBIT(S, T, N, U, Z, W)                                                \
  static inline unsigned int stdc_leading_zeros##S(T x) {                       \
    return x ? __builtin_clz##Z((U)x) - (W - N) : N;                            \
  }                                                                             \
  static inline unsigned int stdc_leading_ones##S(T x) {                        \
    return stdc_leading_zeros##S((T)~x);                                        \
  }                                                                             \
  static inline unsigned int stdc_trailing_zeros##S(T x) {                      \
    return x ? __builtin_ctz##Z((U)x) : N;                                      \
  }                                                                             \
  static inline unsigned int stdc_trailing_ones##S(T x) {                       \
    return stdc_trailing_zeros##S((T)~x);                                       \
  }                                                                             \
  static inline unsigned int stdc_first_leading_zero##S(T x) {                  \
    return (T)~x ? stdc_leading_ones##S(x) + 1 : 0;                             \
  }                                                                             \
  static inline unsigned int stdc_first_leading_one##S(T x) {                   \
    return x ? stdc_leading_zeros##S(x) + 1 : 0;                                \
  }                                                                             \
  static inline unsigned int stdc_first_trailing_zero##S(T x) {                 \
    return (T)~x ? stdc_trailing_ones##S(x) + 1 : 0;                            \
  }                                                                             \
  static inline unsigned int stdc_first_trailing_one##S(T x) {                  \
    return x ? stdc_trailing_zeros##S(x) + 1 : 0;                               \
  }                                                                             \
  static inline unsigned int stdc_count_ones##S(T x) {                          \
    return __builtin_popcount##Z((U)x);                                         \
  }                                                                             \
  static inline unsigned int stdc_count_zeros##S(T x) {                         \
    return N - stdc_count_ones##S(x);                                           \
  }                                                                             \
  static inline _Bool stdc_has_single_bit##S(T x) {                             \
    return x && !(x & (x - 1));                                                 \
  }                                                                             \
  static inline unsigned int stdc_bit_width##S(T x) {                           \
    return N - stdc_leading_zeros##S(x);                                        \
  }                                                                             \
  static inline T stdc_bit_floor##S(T x) {                                      \
    return x ? (T)((T)1 << (stdc_bit_width##S(x) - 1)) : 0;                     \
  }                                                                             \
  /* 0 when the result doesn't fit in T */                                      \
  static inline T stdc_bit_ceil##S(T x) {                                       \
    if (x <= 1)                                                                 \
      return 1;                                                                 \
    unsigned int w = stdc_bit_width##S((T)(x - 1));                             \
    return w < N ? (T)((T)1 << w) : 0;                                          \
  }

__STDBIT(_uc, unsigned char, 8, unsigned int, , 32)
__STDBIT(_us, unsigned short, 16, unsigned int, , 32)
__STDBIT(_ui, unsigned int, 32, unsigned int, , 32)
__STDBIT(_ul, unsigned long, 64, unsigned long, l, 64)
__STDBIT(_ull, unsigned long long, 64, unsigned long long, ll, 64)
#undef __STDBIT

#define __STDBIT_GENERIC(f, x)                                                  \
  _Generic((x), unsigned char: f##_uc, unsigned short: f##_us,                  \
           unsigned int: f##_ui, unsigned long: f##_ul,                         \
           unsigned long long: f##_ull)(x)

#define stdc_leading_zeros(x) __STDBIT_GENERIC(stdc_leading_zeros, x)
#define stdc_leading_ones(x) __STDBIT_GENERIC(stdc_leading_ones, x)
#define stdc_trailing_zeros(x) __STDBIT_GENERIC(stdc_trailing_zeros, x)
#define stdc_trailing_ones(x) __STDBIT_GENERIC(stdc_trailing_ones, x)
#define stdc_first_leading_zero(x) __STDBIT_GENERIC(stdc_first_leading_zero, x)
#define stdc_first_leading_one(x) __STDBIT_GENERIC(stdc_first_leading_one, x)
#define stdc_first_trailing_zero(x) __STDBIT_GENERIC(stdc_first_trailing_zero, x)
#define stdc_first_trailing_one(x) __STDBIT_GENERIC(stdc_first_trailing_one, x)
#define stdc_count_zeros(x) __STDBIT_GENERIC(stdc_count_zeros, x)
#define stdc_count_ones(x) __STDBIT_GENERIC(stdc_count_ones, x)
#define stdc_has_single_bit(x) __STDBIT_GENERIC(stdc_has_single_bit, x)
#define stdc_bit_width(x) __STDBIT_GENERIC(stdc_bit_width, x)
#define stdc_bit_floor(x) __STDBIT_GENERIC(stdc_bit_floor, x)
#define stdc_bit_ceil(x) __STDBIT_GENERIC(stdc_bit_ceil, x)

#endif
