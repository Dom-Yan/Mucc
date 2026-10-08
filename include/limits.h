// The C standard's limits, as gcc's own <limits.h> gives them. With
// __GNUC__ defined, glibc's <limits.h> leaves these to the compiler's and
// includes it with #include_next unless _GCC_LIMITS_H_ is defined, so
// this defines that first and then includes the C library's, for POSIX's
// limits. musl's defines these too; then they're left as they are.
#ifndef _GCC_LIMITS_H_
#define _GCC_LIMITS_H_
#endif

#if __has_include_next(<limits.h>)
#include_next <limits.h>
#endif

#ifndef _MUCC_LIMITS_H
#define _MUCC_LIMITS_H

#ifndef CHAR_BIT
#define CHAR_BIT __CHAR_BIT__
#endif
#ifndef MB_LEN_MAX
#define MB_LEN_MAX 16
#endif

#ifndef SCHAR_MAX
#define SCHAR_MIN (-__SCHAR_MAX__ - 1)
#define SCHAR_MAX __SCHAR_MAX__
#define UCHAR_MAX (__SCHAR_MAX__ * 2 + 1)
#endif
#ifndef CHAR_MAX
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#endif
#endif

#ifndef SHRT_MAX
#define SHRT_MIN (-__SHRT_MAX__ - 1)
#define SHRT_MAX __SHRT_MAX__
#define USHRT_MAX (__SHRT_MAX__ * 2 + 1)
#endif

#ifndef INT_MAX
#define INT_MIN (-__INT_MAX__ - 1)
#define INT_MAX __INT_MAX__
#define UINT_MAX (__INT_MAX__ * 2U + 1U)
#endif

#ifndef LONG_MAX
#define LONG_MIN (-__LONG_MAX__ - 1L)
#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)
#endif

#ifndef LLONG_MAX
#define LLONG_MIN (-__LONG_LONG_MAX__ - 1LL)
#define LLONG_MAX __LONG_LONG_MAX__
#define ULLONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1ULL)
#endif

#endif
