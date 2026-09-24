// glibc's <sys/cdefs.h> defines __attribute__(x) as nothing for compilers
// other than gcc, clang and tcc. That silently drops attributes like
// `packed` and `aligned`, both in glibc's own headers (struct epoll_event
// is packed) and in every program that includes one. mucc understands GNU
// attributes, and stops with an error on those it can't honor (see
// attributes() in src/parser.c), so undo that definition.
#include_next <sys/cdefs.h>
#undef __attribute__
