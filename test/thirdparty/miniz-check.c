// miniz: deflate at every level, inflate, and a zip archive in memory.
// Checks each round trip, and prints a hash of each result; miniz.sh
// compares them with gcc's.
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fnv(const void *p, size_t n) {
  const unsigned char *s = p;
  unsigned h = 2166136261u;
  while (n--)
    h = (h ^ *s++) * 16777619u;
  return h;
}

int main(void) {
  // Text-like data with repeats, and some noise
  enum { N = 200000 };
  static unsigned char data[N];
  unsigned seed = 7;
  for (int i = 0; i < N; i++) {
    seed = seed * 1103515245 + 12345;
    data[i] = i % 1000 < 700 ? "the quick brown fox "[i % 20] : seed >> 24;
  }

  static unsigned char comp[N * 2], out[N];
  for (int level = 0; level <= 10; level++) {
    mz_ulong clen = sizeof(comp), olen = sizeof(out);
    int r1 = compress2(comp, &clen, data, N, level);
    int r2 = uncompress(out, &olen, comp, clen);
    printf("level %d %08x %lu %d\n", level, fnv(comp, clen), (unsigned long)clen,
           r1 == Z_OK && r2 == Z_OK && olen == N && !memcmp(out, data, N));
  }

  size_t tlen;
  void *t = tdefl_compress_mem_to_heap(data, N, &tlen, TDEFL_DEFAULT_MAX_PROBES | TDEFL_GREEDY_PARSING_FLAG);
  size_t ulen;
  void *u = tinfl_decompress_mem_to_heap(t, tlen, &ulen, 0);
  printf("tdefl %08x %d\n", fnv(t, tlen), ulen == N && !memcmp(u, data, N));

  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  mz_zip_writer_init_heap(&zip, 0, 0);
  mz_zip_writer_add_mem(&zip, "a.txt", data, 5000, MZ_DEFAULT_COMPRESSION);
  mz_zip_writer_add_mem(&zip, "dir/b.bin", data + 5000, N - 5000, MZ_BEST_COMPRESSION);
  mz_zip_writer_add_mem(&zip, "c.raw", data, 300, MZ_NO_COMPRESSION);
  void *zbuf;
  size_t zlen;
  mz_zip_writer_finalize_heap_archive(&zip, &zbuf, &zlen);
  mz_zip_writer_end(&zip);

  mz_zip_archive rd;
  memset(&rd, 0, sizeof(rd));
  int ok = mz_zip_reader_init_mem(&rd, zbuf, zlen, 0) && mz_zip_reader_get_num_files(&rd) == 3;
  size_t blen;
  void *b = mz_zip_reader_extract_file_to_heap(&rd, "dir/b.bin", &blen, 0);
  ok = ok && b && blen == N - 5000 && !memcmp(b, data + 5000, blen);
  mz_zip_archive_file_stat st;
  ok = ok && mz_zip_reader_file_stat(&rd, 0, &st) && !strcmp(st.m_filename, "a.txt") &&
       st.m_crc32 == mz_crc32(MZ_CRC32_INIT, data, 5000);
  mz_zip_reader_end(&rd);
  // The archive holds the time it was made, so only its length is stable.
  printf("zip %lu %d\n", (unsigned long)zlen, ok);
  printf("crc %08lx adler %08lx\n", (unsigned long)mz_crc32(MZ_CRC32_INIT, data, N),
         (unsigned long)mz_adler32(MZ_ADLER32_INIT, data, N));
  return 0;
}
