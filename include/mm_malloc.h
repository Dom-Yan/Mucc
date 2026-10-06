// <mm_malloc.h>: aligned memory for SIMD data, from posix_memalign()
#ifndef __MM_MALLOC_H
#define __MM_MALLOC_H

#include <stdlib.h>

extern int posix_memalign(void **, size_t, size_t);

static __inline__ void *_mm_malloc(size_t __size, size_t __align) {
  void *__p;
  if (__align == 1)
    return malloc(__size);
  if (__align < sizeof(void *))
    __align = sizeof(void *);
  if (posix_memalign(&__p, __align, __size))
    return 0;
  return __p;
}

static __inline__ void _mm_free(void *__p) {
  free(__p);
}

#endif
