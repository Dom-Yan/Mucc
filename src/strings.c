//============================================================================
// strings.c - SUPPORT
//
// The memory allocator, growable string arrays, and format(), a printf
// that returns a string.
//============================================================================

#include "mucc.h"

//---------- Memory ----------------------------------------------------------

// mucc allocates millions of small objects (tokens, AST nodes, types)
// and never frees them; the process just exits when it's done. So rather
// than calling calloc for each one, hand out pieces of big zeroed blocks.
// This was the single largest cost in profiles of mucc.
void *arena_alloc(size_t size) {
  static char *p;
  static char *end;

  size = (size + 15) & ~(size_t)15; // keep 16-byte alignment
  if (!p || end - p < size) {
    size_t block = size > (1 << 20) ? size : (1 << 20);
    p = calloc(1, block);
    if (!p)
      error("out of memory");
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

// Like format(), but takes a va_list.
char *vformat(char *fmt, va_list ap) {
  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);
  vfprintf(out, fmt, ap);
  fclose(out);
  return buf;
}
