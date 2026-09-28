#ifndef __STDARG_H
#define __STDARG_H

// mucc implements these as gcc does, as builtins (see va_builtin() in
// src/parser.c), so a va_list can be passed to and from code other
// compilers built.
typedef __builtin_va_list va_list;

// In C23 the second argument is optional.
#define va_start(ap, ...) __builtin_va_start(ap, 0)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, ty) __builtin_va_arg(ap, ty)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif
