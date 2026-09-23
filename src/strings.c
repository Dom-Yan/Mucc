//============================================================================
// strings.c - SUPPORT
//
// The memory allocator, growable string arrays, and format(), a printf
// that returns a string.
//============================================================================

// madvise() and MADV_HUGEPAGE are Linux extensions, not POSIX.
#define _DEFAULT_SOURCE
#include "mucc.h"
#include <sys/mman.h>

//---------- Memory ----------------------------------------------------------

// mucc allocates millions of small objects (tokens, AST nodes, types)
// and never frees them; the process just exits when it's done. So rather
// than calling calloc for each one, hand out pieces of big zeroed blocks.
//
// Blocks come straight from mmap: the kernel hands them out zeroed and
// only backs the parts we touch. Most of mucc's time used to go to page
// faults (one per 4 KB touched), so ask for 2 MB huge pages, which need
// 512 times fewer. That's only a hint; without it, this still works.
#define ARENA_BLOCK (64 << 20)

void *arena_alloc(size_t size) {
  static char *p;
  static char *end;

  size = (size + 15) & ~(size_t)15; // keep 16-byte alignment
  if (!p || end - p < size) {
    size_t block = size > ARENA_BLOCK ? size : ARENA_BLOCK;
    p = mmap(NULL, block, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
      error("out of memory");
    madvise(p, block, MADV_HUGEPAGE);
    end = p + block;
  }

  void *mem = p;
  p += size;
  return mem;
}

//---------- String arrays ---------------------------------------------------

void strarray_push(StringArray *arr, char *s) {
  if (!arr->data) {
    arr->data = calloc(8, sizeof(char *));
    arr->capacity = 8;
  }

  if (arr->capacity == arr->len) {
    arr->data = realloc(arr->data, sizeof(char *) * arr->capacity * 2);
    arr->capacity *= 2;
    for (int i = arr->len; i < arr->capacity; i++)
      arr->data[i] = NULL;
  }

  arr->data[arr->len++] = s;
}

//---------- format() --------------------------------------------------------

// Takes a printf-style format string and returns a formatted string.
char *format(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char *buf = vformat(fmt, ap);
  va_end(ap);
  return buf;
}

// Like format(), but takes a va_list. Measures the result first, then
// prints it into arena memory. (open_memstream was simpler to write, but
// set up and zeroed an 8 KB buffer per call: a fifth of all compile time.)
char *vformat(char *fmt, va_list ap) {
  va_list ap2;
  va_copy(ap2, ap);
  int len = vsnprintf(NULL, 0, fmt, ap2);
  va_end(ap2);

  char *buf = arena_alloc(len + 1);
  vsnprintf(buf, len + 1, fmt, ap);
  return buf;
}
