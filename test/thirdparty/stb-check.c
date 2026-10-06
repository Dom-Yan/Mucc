// stb's image reader, writer and resizer (their SSE2 paths too, as on any
// x86-64 build) and stb_sprintf, on a made-up image. Prints a hash of
// each result; stb.sh compares them with gcc's.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"
#include "stb_sprintf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 97, H = 61 };

static unsigned fnv(const void *p, size_t n) {
  const unsigned char *s = p;
  unsigned h = 2166136261u;
  while (n--)
    h = (h ^ *s++) * 16777619u;
  return h;
}

static unsigned char buf[1 << 20];
static int buf_len;

static void to_buf(void *ctx, void *data, int n) {
  memcpy(buf + buf_len, data, n);
  buf_len += n;
}

int main(void) {
  static unsigned char img[W * H * 4];
  unsigned seed = 1;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      seed = seed * 1103515245 + 12345;
      unsigned char *p = &img[(y * W + x) * 4];
      p[0] = x * 255 / W;
      p[1] = y * 255 / H;
      p[2] = (x ^ y) * 3 + (seed >> 28);
      p[3] = 255 - (seed >> 25);
    }

  int len, w, h, n;
  unsigned char *png = stbi_write_png_to_mem(img, W * 4, W, H, 4, &len);
  unsigned char *back = stbi_load_from_memory(png, len, &w, &h, &n, 4);
  printf("png %08x %d\n", fnv(png, len), w == W && h == H && !memcmp(back, img, sizeof(img)));

  buf_len = 0;
  stbi_write_jpg_to_func(to_buf, NULL, W, H, 4, img, 90);
  unsigned char *jpg = stbi_load_from_memory(buf, buf_len, &w, &h, &n, 3);
  printf("jpg %08x %08x\n", fnv(buf, buf_len), fnv(jpg, w * h * 3));

  buf_len = 0;
  stbi_write_bmp_to_func(to_buf, NULL, W, H, 3, img);
  stbi_write_tga_to_func(to_buf, NULL, W, H, 4, img);
  printf("bmp+tga %08x\n", fnv(buf, buf_len));

  static unsigned char small[40 * 25 * 4], big[150 * 90 * 4];
  stbir_resize_uint8_srgb(img, W, H, 0, small, 40, 25, 0, STBIR_RGBA);
  stbir_resize_uint8_linear(img, W, H, 0, big, 150, 90, 0, STBIR_RGBA);
  printf("resize %08x %08x\n", fnv(small, sizeof(small)), fnv(big, sizeof(big)));

  static float f[W * H], fout[64 * 64];
  for (int i = 0; i < W * H; i++)
    f[i] = img[i * 4] / 255.0f - 0.25f;
  stbir_resize_float_linear(f, W, H, 0, fout, 64, 64, 0, STBIR_1CHANNEL);
  printf("resize float %08x\n", fnv(fout, sizeof(fout)));

  char s[512];
  int m = stbsp_snprintf(s, sizeof(s), "%d %x %08.3f %e %g %s %-5c| %lld %p %.40f %'d", -42,
                         0xbeef, 3.14159, 6.02e23, 1e-5, "str", 'z', -1LL << 62,
                         (void *)0x1234, 1.0 / 3, 1234567);
  printf("sprintf %08x %d\n", fnv(s, m), m);
  return 0;
}
