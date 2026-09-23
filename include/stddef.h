#ifndef __STDDEF_H
#define __STDDEF_H

#define NULL ((void *)0)

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef unsigned int wchar_t;
typedef long max_align_t;
typedef typeof(nullptr) nullptr_t;

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif

// C23's unreachable() comes only with a direct #include <stddef.h>. C
// library headers include this file asking for just size_t or NULL (by
// defining __need_size_t and so on), and a program that never included
// <stddef.h> itself may use the name `unreachable` for its own things.
// Reaching unreachable() traps (mucc emits ud2).
#if !defined(__need_size_t) && !defined(__need_NULL) && \
    !defined(__need_wchar_t) && !defined(__need_ptrdiff_t) && \
    !defined(__need_wint_t)
# ifndef unreachable
#  define unreachable() __builtin_unreachable()
# endif
# define __STDC_VERSION_STDDEF_H__ 202311L
#endif

#undef __need_size_t
#undef __need_NULL
#undef __need_wchar_t
#undef __need_ptrdiff_t
#undef __need_wint_t
