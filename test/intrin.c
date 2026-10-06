// mucc's x86 intrinsics (include/*intrin.h) on some inputs. With an
// argument, it prints each result's bytes: test/intrin.sh compares that,
// built by mucc, with gcc's, built with gcc's own headers.
// flags: -msse4.2 -maes -mpclmul
#include <immintrin.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int line, verbose;

static void show(char *name, void *p, int n) {
  unsigned char b[16];
  memcpy(b, p, n);
  if (!verbose)
    return;
  printf("%d %s:", line++, name);
  for (int i = 0; i < n; i++)
    printf(" %02x", b[i]);
  printf("\n");
}

#define SHOW(name, x) do { __typeof__(x) t_ = (x); show(name, &t_, sizeof(t_)); } while (0)

static __m128 F[6];
static __m128d D[6];
static __m128i I[6];
static __m64 M[6];

static void init(void) {
  float f[6][4] = {{1.5f, -2.25f, 3, 0}, {-0.0f, 1e30f, -1e-40f, 7}, {NAN, 2, -3.5f, INFINITY},
                   {2.5f, -2.5f, 0.5f, -0.5f}, {16, 0.25f, 100, 1e-3f}, {-1, 65536, -7.75f, 3}};
  double d[6][2] = {{1.5, -2.25}, {-0.0, 1e300}, {NAN, -INFINITY}, {2.5, -3.5}, {16, 0.25},
                    {-1e-310, 1}};
  unsigned char b[6][16] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
    {255, 128, 127, 0, 1, 200, 100, 50, 0x80, 0x7f, 0xfe, 0x01, 0x33, 0xcc, 0x55, 0xaa},
    {0x81, 0, 0x80, 0xff, 0xff, 0x7f, 0, 0x80, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0},
    {3, 0, 0, 0, 0xfd, 0xff, 0xff, 0xff, 40, 0, 0, 0, 0, 0, 0, 0x80},
    {0, 1, 0, 0x80, 0xff, 0xff, 0, 0, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80},
    {7, 0x87, 2, 0x8a, 3, 0x0f, 0x41, 0x61, 0x7a, 0x30, 0x39, 0, 0x20, 0x2e, 0x5f, 0x7e},
  };
  for (int i = 0; i < 6; i++) {
    memcpy(&F[i], f[i], 16);
    memcpy(&D[i], d[i], 16);
    memcpy(&I[i], b[i], 16);
    memcpy(&M[i], b[i] + 4, 8);
  }
}

int main(int argc, char **argv) {
  verbose = argc > 1;
  init();
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_ss", _mm_add_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_ss", _mm_sub_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_ss", _mm_mul_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("div_ss", _mm_div_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_ss", _mm_min_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_ss", _mm_max_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_ps", _mm_add_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_ps", _mm_sub_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_ps", _mm_mul_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("div_ps", _mm_div_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_ps", _mm_min_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_ps", _mm_max_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("and_ps", _mm_and_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("andnot_ps", _mm_andnot_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("or_ps", _mm_or_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("xor_ps", _mm_xor_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_ss", _mm_cmpeq_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_ss", _mm_cmplt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmple_ss", _mm_cmple_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_ss", _mm_cmpgt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpge_ss", _mm_cmpge_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpneq_ss", _mm_cmpneq_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnlt_ss", _mm_cmpnlt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnle_ss", _mm_cmpnle_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpngt_ss", _mm_cmpngt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnge_ss", _mm_cmpnge_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpord_ss", _mm_cmpord_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpunord_ss", _mm_cmpunord_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_ps", _mm_cmpeq_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_ps", _mm_cmplt_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmple_ps", _mm_cmple_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_ps", _mm_cmpgt_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpge_ps", _mm_cmpge_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpneq_ps", _mm_cmpneq_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnlt_ps", _mm_cmpnlt_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnle_ps", _mm_cmpnle_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpngt_ps", _mm_cmpngt_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnge_ps", _mm_cmpnge_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpord_ps", _mm_cmpord_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpunord_ps", _mm_cmpunord_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("move_ss", _mm_move_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_ps", _mm_unpackhi_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_ps", _mm_unpacklo_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("movehl_ps", _mm_movehl_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("movelh_ps", _mm_movelh_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("addsub_ps", _mm_addsub_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadd_ps", _mm_hadd_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsub_ps", _mm_hsub_ps(F[i], F[j]));
  for (int i = 0; i < 6; i++) SHOW("sqrt_ss", _mm_sqrt_ss(F[i]));
  for (int i = 0; i < 6; i++) SHOW("sqrt_ps", _mm_sqrt_ps(F[i]));
  for (int i = 0; i < 6; i++) SHOW("movehdup_ps", _mm_movehdup_ps(F[i]));
  for (int i = 0; i < 6; i++) SHOW("moveldup_ps", _mm_moveldup_ps(F[i]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comieq_ss", _mm_comieq_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comilt_ss", _mm_comilt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comile_ss", _mm_comile_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comigt_ss", _mm_comigt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comige_ss", _mm_comige_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comineq_ss", _mm_comineq_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomieq_ss", _mm_ucomieq_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomilt_ss", _mm_ucomilt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomile_ss", _mm_ucomile_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomigt_ss", _mm_ucomigt_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomige_ss", _mm_ucomige_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomineq_ss", _mm_ucomineq_ss(F[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_sd", _mm_add_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_sd", _mm_sub_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_sd", _mm_mul_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("div_sd", _mm_div_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_sd", _mm_min_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_sd", _mm_max_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_pd", _mm_add_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_pd", _mm_sub_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_pd", _mm_mul_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("div_pd", _mm_div_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_pd", _mm_min_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_pd", _mm_max_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("and_pd", _mm_and_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("andnot_pd", _mm_andnot_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("or_pd", _mm_or_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("xor_pd", _mm_xor_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sqrt_sd", _mm_sqrt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_sd", _mm_cmpeq_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_sd", _mm_cmplt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmple_sd", _mm_cmple_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_sd", _mm_cmpgt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpge_sd", _mm_cmpge_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpneq_sd", _mm_cmpneq_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnlt_sd", _mm_cmpnlt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnle_sd", _mm_cmpnle_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpngt_sd", _mm_cmpngt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnge_sd", _mm_cmpnge_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpord_sd", _mm_cmpord_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpunord_sd", _mm_cmpunord_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_pd", _mm_cmpeq_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_pd", _mm_cmplt_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmple_pd", _mm_cmple_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_pd", _mm_cmpgt_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpge_pd", _mm_cmpge_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpneq_pd", _mm_cmpneq_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnlt_pd", _mm_cmpnlt_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnle_pd", _mm_cmpnle_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpngt_pd", _mm_cmpngt_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpnge_pd", _mm_cmpnge_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpord_pd", _mm_cmpord_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpunord_pd", _mm_cmpunord_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("move_sd", _mm_move_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_pd", _mm_unpackhi_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_pd", _mm_unpacklo_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("addsub_pd", _mm_addsub_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadd_pd", _mm_hadd_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsub_pd", _mm_hsub_pd(D[i], D[j]));
  for (int i = 0; i < 6; i++) SHOW("sqrt_pd", _mm_sqrt_pd(D[i]));
  for (int i = 0; i < 6; i++) SHOW("movedup_pd", _mm_movedup_pd(D[i]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comieq_sd", _mm_comieq_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comilt_sd", _mm_comilt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comile_sd", _mm_comile_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comigt_sd", _mm_comigt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comige_sd", _mm_comige_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("comineq_sd", _mm_comineq_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomieq_sd", _mm_ucomieq_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomilt_sd", _mm_ucomilt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomile_sd", _mm_ucomile_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomigt_sd", _mm_ucomigt_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomige_sd", _mm_ucomige_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("ucomineq_sd", _mm_ucomineq_sd(D[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_epi8", _mm_add_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_epi16", _mm_add_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_epi32", _mm_add_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_epi64", _mm_add_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_epi8", _mm_sub_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_epi16", _mm_sub_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_epi32", _mm_sub_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_epi64", _mm_sub_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_epi8", _mm_adds_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_epi16", _mm_adds_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_epu8", _mm_adds_epu8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_epu16", _mm_adds_epu16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_epi8", _mm_subs_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_epi16", _mm_subs_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_epu8", _mm_subs_epu8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_epu16", _mm_subs_epu16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("avg_epu8", _mm_avg_epu8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("avg_epu16", _mm_avg_epu16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("madd_epi16", _mm_madd_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_epi16", _mm_max_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_epu8", _mm_max_epu8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_epi16", _mm_min_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_epu8", _mm_min_epu8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mulhi_epi16", _mm_mulhi_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mulhi_epu16", _mm_mulhi_epu16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_epu32", _mm_mul_epu32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sad_epu8", _mm_sad_epu8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mullo_epi16", _mm_mullo_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sll_epi16", _mm_sll_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sll_epi32", _mm_sll_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sll_epi64", _mm_sll_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sra_epi16", _mm_sra_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sra_epi32", _mm_sra_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("srl_epi16", _mm_srl_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("srl_epi32", _mm_srl_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("srl_epi64", _mm_srl_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("and_si128", _mm_and_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("andnot_si128", _mm_andnot_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("or_si128", _mm_or_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("xor_si128", _mm_xor_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_epi8", _mm_cmpeq_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_epi16", _mm_cmpeq_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_epi32", _mm_cmpeq_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_epi8", _mm_cmpgt_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_epi16", _mm_cmpgt_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_epi32", _mm_cmpgt_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_epi8", _mm_cmplt_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_epi16", _mm_cmplt_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmplt_epi32", _mm_cmplt_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packs_epi16", _mm_packs_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packs_epi32", _mm_packs_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packus_epi16", _mm_packus_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_epi8", _mm_unpackhi_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_epi16", _mm_unpackhi_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_epi32", _mm_unpackhi_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_epi64", _mm_unpackhi_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_epi8", _mm_unpacklo_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_epi16", _mm_unpacklo_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_epi32", _mm_unpacklo_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_epi64", _mm_unpacklo_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadd_epi16", _mm_hadd_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadd_epi32", _mm_hadd_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadds_epi16", _mm_hadds_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsub_epi16", _mm_hsub_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsub_epi32", _mm_hsub_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsubs_epi16", _mm_hsubs_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("maddubs_epi16", _mm_maddubs_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mulhrs_epi16", _mm_mulhrs_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_epi8", _mm_shuffle_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sign_epi8", _mm_sign_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sign_epi16", _mm_sign_epi16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sign_epi32", _mm_sign_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_epi64", _mm_cmpeq_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_epi8", _mm_max_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_epi32", _mm_max_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_epu16", _mm_max_epu16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_epu32", _mm_max_epu32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_epi8", _mm_min_epi8(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_epi32", _mm_min_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_epu16", _mm_min_epu16(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_epu32", _mm_min_epu32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_epi32", _mm_mul_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mullo_epi32", _mm_mullo_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packus_epi32", _mm_packus_epi32(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_epi64", _mm_cmpgt_epi64(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("aesenc_si128", _mm_aesenc_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("aesenclast_si128", _mm_aesenclast_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("aesdec_si128", _mm_aesdec_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("aesdeclast_si128", _mm_aesdeclast_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++) SHOW("abs_epi8", _mm_abs_epi8(I[i]));
  for (int i = 0; i < 6; i++) SHOW("abs_epi16", _mm_abs_epi16(I[i]));
  for (int i = 0; i < 6; i++) SHOW("abs_epi32", _mm_abs_epi32(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi8_epi16", _mm_cvtepi8_epi16(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi8_epi32", _mm_cvtepi8_epi32(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi8_epi64", _mm_cvtepi8_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi16_epi32", _mm_cvtepi16_epi32(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi16_epi64", _mm_cvtepi16_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi32_epi64", _mm_cvtepi32_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepu8_epi16", _mm_cvtepu8_epi16(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepu8_epi32", _mm_cvtepu8_epi32(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepu8_epi64", _mm_cvtepu8_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepu16_epi32", _mm_cvtepu16_epi32(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepu16_epi64", _mm_cvtepu16_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepu32_epi64", _mm_cvtepu32_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("minpos_epu16", _mm_minpos_epu16(I[i]));
  for (int i = 0; i < 6; i++) SHOW("move_epi64", _mm_move_epi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("aesimc_si128", _mm_aesimc_si128(I[i]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("testz_si128", _mm_testz_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("testc_si128", _mm_testc_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("testnzc_si128", _mm_testnzc_si128(I[i], I[j]));
  for (int i = 0; i < 6; i++) SHOW("movemask_epi8", _mm_movemask_epi8(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtsi128_si32", _mm_cvtsi128_si32(I[i]));
  for (int i = 0; i < 6; i++) SHOW("test_all_ones", _mm_test_all_ones(I[i]));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("slli_epi16", _mm_slli_epi16(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("slli_epi32", _mm_slli_epi32(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("slli_epi64", _mm_slli_epi64(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srai_epi16", _mm_srai_epi16(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srai_epi32", _mm_srai_epi32(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srli_epi16", _mm_srli_epi16(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srli_epi32", _mm_srli_epi32(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srli_epi64", _mm_srli_epi64(I[i], c));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packs_pi16", _mm_packs_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packs_pi32", _mm_packs_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("packs_pu16", _mm_packs_pu16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_pi8", _mm_unpacklo_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_pi16", _mm_unpacklo_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpacklo_pi32", _mm_unpacklo_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_pi8", _mm_unpackhi_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_pi16", _mm_unpackhi_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("unpackhi_pi32", _mm_unpackhi_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_pi8", _mm_add_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_pi16", _mm_add_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_pi32", _mm_add_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_pi8", _mm_sub_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_pi16", _mm_sub_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_pi32", _mm_sub_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_pi8", _mm_adds_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_pi16", _mm_adds_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_pu8", _mm_adds_pu8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("adds_pu16", _mm_adds_pu16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_pi8", _mm_subs_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_pi16", _mm_subs_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_pu8", _mm_subs_pu8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("subs_pu16", _mm_subs_pu16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("madd_pi16", _mm_madd_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mulhi_pi16", _mm_mulhi_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mullo_pi16", _mm_mullo_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sll_pi16", _mm_sll_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sll_pi32", _mm_sll_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sll_si64", _mm_sll_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sra_pi16", _mm_sra_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sra_pi32", _mm_sra_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("srl_pi16", _mm_srl_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("srl_pi32", _mm_srl_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("srl_si64", _mm_srl_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("and_si64", _mm_and_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("andnot_si64", _mm_andnot_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("or_si64", _mm_or_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("xor_si64", _mm_xor_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_pi8", _mm_cmpeq_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_pi16", _mm_cmpeq_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpeq_pi32", _mm_cmpeq_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_pi8", _mm_cmpgt_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_pi16", _mm_cmpgt_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpgt_pi32", _mm_cmpgt_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_pi16", _mm_max_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("max_pu8", _mm_max_pu8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_pi16", _mm_min_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("min_pu8", _mm_min_pu8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mulhi_pu16", _mm_mulhi_pu16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("avg_pu8", _mm_avg_pu8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("avg_pu16", _mm_avg_pu16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sad_pu8", _mm_sad_pu8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("add_si64", _mm_add_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sub_si64", _mm_sub_si64(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mul_su32", _mm_mul_su32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadd_pi16", _mm_hadd_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadd_pi32", _mm_hadd_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hadds_pi16", _mm_hadds_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsub_pi16", _mm_hsub_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsub_pi32", _mm_hsub_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("hsubs_pi16", _mm_hsubs_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("maddubs_pi16", _mm_maddubs_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mulhrs_pi16", _mm_mulhrs_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_pi8", _mm_shuffle_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sign_pi8", _mm_sign_pi8(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sign_pi16", _mm_sign_pi16(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("sign_pi32", _mm_sign_pi32(M[i], M[j]));
  for (int i = 0; i < 6; i++) SHOW("abs_pi8", _mm_abs_pi8(M[i]));
  for (int i = 0; i < 6; i++) SHOW("abs_pi16", _mm_abs_pi16(M[i]));
  for (int i = 0; i < 6; i++) SHOW("abs_pi32", _mm_abs_pi32(M[i]));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("slli_pi16", _mm_slli_pi16(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("slli_pi32", _mm_slli_pi32(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("slli_si64", _mm_slli_si64(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srai_pi16", _mm_srai_pi16(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srai_pi32", _mm_srai_pi32(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srli_pi16", _mm_srli_pi16(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srli_pi32", _mm_srli_pi32(M[i], c));
  for (int i = 0; i < 6; i++)
    for (int c = 0; c < 70; c += 3) SHOW("srli_si64", _mm_srli_si64(M[i], c));
  for (int i = 0; i < 6; i++) SHOW("cvtepi32_pd", _mm_cvtepi32_pd(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtepi32_ps", _mm_cvtepi32_ps(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpd_epi32", _mm_cvtpd_epi32(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttpd_epi32", _mm_cvttpd_epi32(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpd_ps", _mm_cvtpd_ps(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtps_epi32", _mm_cvtps_epi32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttps_epi32", _mm_cvttps_epi32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtps_pd", _mm_cvtps_pd(F[i]));
  for (int i = 0; i < 6; i++) SHOW("rcp_ps", _mm_rcp_ps(F[i]));
  for (int i = 0; i < 6; i++) SHOW("rsqrt_ps", _mm_rsqrt_ps(F[i]));
  for (int i = 0; i < 6; i++) SHOW("rcp_ss", _mm_rcp_ss(F[i]));
  for (int i = 0; i < 6; i++) SHOW("rsqrt_ss", _mm_rsqrt_ss(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtps_pi32", _mm_cvtps_pi32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttps_pi32", _mm_cvttps_pi32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtps_pi16", _mm_cvtps_pi16(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtps_pi8", _mm_cvtps_pi8(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpd_pi32", _mm_cvtpd_pi32(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttpd_pi32", _mm_cvttpd_pi32(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpi32_pd", _mm_cvtpi32_pd(M[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpi16_ps", _mm_cvtpi16_ps(M[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpu16_ps", _mm_cvtpu16_ps(M[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpi8_ps", _mm_cvtpi8_ps(M[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtpu8_ps", _mm_cvtpu8_ps(M[i]));
  for (int i = 0; i < 6; i++) SHOW("movemask_ps", _mm_movemask_ps(F[i]));
  for (int i = 0; i < 6; i++) SHOW("movemask_pd", _mm_movemask_pd(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtss_si32", _mm_cvtss_si32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttss_si32", _mm_cvttss_si32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtss_si64", _mm_cvtss_si64(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttss_si64", _mm_cvttss_si64(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtsd_si32", _mm_cvtsd_si32(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttsd_si32", _mm_cvttsd_si32(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtsd_si64", _mm_cvtsd_si64(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvttsd_si64", _mm_cvttsd_si64(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtss_f32", _mm_cvtss_f32(F[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtsd_f64", _mm_cvtsd_f64(D[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtsi128_si64", _mm_cvtsi128_si64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("movemask_pi8", _mm_movemask_pi8(M[i]));
  for (int i = 0; i < 6; i++) SHOW("movepi64_pi64", _mm_movepi64_pi64(I[i]));
  for (int i = 0; i < 6; i++) SHOW("movpi64_epi64", _mm_movpi64_epi64(M[i]));
  for (int i = 0; i < 6; i++) SHOW("castpd_ps", _mm_castpd_ps(D[i]));
  for (int i = 0; i < 6; i++) SHOW("castpd_si128", _mm_castpd_si128(D[i]));
  for (int i = 0; i < 6; i++) SHOW("castps_pd", _mm_castps_pd(F[i]));
  for (int i = 0; i < 6; i++) SHOW("castps_si128", _mm_castps_si128(F[i]));
  for (int i = 0; i < 6; i++) SHOW("castsi128_pd", _mm_castsi128_pd(I[i]));
  for (int i = 0; i < 6; i++) SHOW("castsi128_ps", _mm_castsi128_ps(I[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtsi64_si32", _mm_cvtsi64_si32(M[i]));
  for (int i = 0; i < 6; i++) SHOW("cvtm64_si64", _mm_cvtm64_si64(M[i]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cvtsd_ss", _mm_cvtsd_ss(F[i], D[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cvtss_sd", _mm_cvtss_sd(D[i], F[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cvtpi32_ps", _mm_cvtpi32_ps(F[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cvtpi32x2_ps", _mm_cvtpi32x2_ps(M[i], M[j]));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_ps 0", _mm_shuffle_ps(F[i], F[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_ps 27", _mm_shuffle_ps(F[i], F[j], 27));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_ps 228", _mm_shuffle_ps(F[i], F[j], 228));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_ps 78", _mm_shuffle_ps(F[i], F[j], 78));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_pd 0", _mm_shuffle_pd(D[i], D[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_pd 1", _mm_shuffle_pd(D[i], D[j], 1));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_pd 2", _mm_shuffle_pd(D[i], D[j], 2));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("shuffle_pd 3", _mm_shuffle_pd(D[i], D[j], 3));
  for (int i = 0; i < 6; i++) SHOW("shuffle_epi32 0", _mm_shuffle_epi32(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("shuffle_epi32 27", _mm_shuffle_epi32(I[i], 27));
  for (int i = 0; i < 6; i++) SHOW("shuffle_epi32 228", _mm_shuffle_epi32(I[i], 228));
  for (int i = 0; i < 6; i++) SHOW("shuffle_epi32 78", _mm_shuffle_epi32(I[i], 78));
  for (int i = 0; i < 6; i++) SHOW("shufflehi_epi16 0", _mm_shufflehi_epi16(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("shufflehi_epi16 27", _mm_shufflehi_epi16(I[i], 27));
  for (int i = 0; i < 6; i++) SHOW("shufflehi_epi16 228", _mm_shufflehi_epi16(I[i], 228));
  for (int i = 0; i < 6; i++) SHOW("shufflelo_epi16 0", _mm_shufflelo_epi16(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("shufflelo_epi16 27", _mm_shufflelo_epi16(I[i], 27));
  for (int i = 0; i < 6; i++) SHOW("shufflelo_epi16 228", _mm_shufflelo_epi16(I[i], 228));
  for (int i = 0; i < 6; i++) SHOW("slli_si128 0", _mm_slli_si128(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("slli_si128 1", _mm_slli_si128(I[i], 1));
  for (int i = 0; i < 6; i++) SHOW("slli_si128 7", _mm_slli_si128(I[i], 7));
  for (int i = 0; i < 6; i++) SHOW("slli_si128 15", _mm_slli_si128(I[i], 15));
  for (int i = 0; i < 6; i++) SHOW("slli_si128 16", _mm_slli_si128(I[i], 16));
  for (int i = 0; i < 6; i++) SHOW("slli_si128 20", _mm_slli_si128(I[i], 20));
  for (int i = 0; i < 6; i++) SHOW("srli_si128 0", _mm_srli_si128(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("srli_si128 1", _mm_srli_si128(I[i], 1));
  for (int i = 0; i < 6; i++) SHOW("srli_si128 7", _mm_srli_si128(I[i], 7));
  for (int i = 0; i < 6; i++) SHOW("srli_si128 15", _mm_srli_si128(I[i], 15));
  for (int i = 0; i < 6; i++) SHOW("srli_si128 16", _mm_srli_si128(I[i], 16));
  for (int i = 0; i < 6; i++) SHOW("srli_si128 20", _mm_srli_si128(I[i], 20));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_epi8 0", _mm_alignr_epi8(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_epi8 3", _mm_alignr_epi8(I[i], I[j], 3));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_epi8 15", _mm_alignr_epi8(I[i], I[j], 15));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_epi8 16", _mm_alignr_epi8(I[i], I[j], 16));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_epi8 31", _mm_alignr_epi8(I[i], I[j], 31));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_epi16 0", _mm_blend_epi16(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_epi16 85", _mm_blend_epi16(I[i], I[j], 85));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_epi16 255", _mm_blend_epi16(I[i], I[j], 255));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_ps 0", _mm_blend_ps(F[i], F[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_ps 5", _mm_blend_ps(F[i], F[j], 5));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_ps 15", _mm_blend_ps(F[i], F[j], 15));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_pd 0", _mm_blend_pd(D[i], D[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_pd 1", _mm_blend_pd(D[i], D[j], 1));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("blend_pd 3", _mm_blend_pd(D[i], D[j], 3));
  for (int i = 0; i < 6; i++) SHOW("round_ps 0", _mm_round_ps(F[i], 0));
  for (int i = 0; i < 6; i++) SHOW("round_ps 1", _mm_round_ps(F[i], 1));
  for (int i = 0; i < 6; i++) SHOW("round_ps 2", _mm_round_ps(F[i], 2));
  for (int i = 0; i < 6; i++) SHOW("round_ps 3", _mm_round_ps(F[i], 3));
  for (int i = 0; i < 6; i++) SHOW("round_ps 4", _mm_round_ps(F[i], 4));
  for (int i = 0; i < 6; i++) SHOW("round_pd 0", _mm_round_pd(D[i], 0));
  for (int i = 0; i < 6; i++) SHOW("round_pd 1", _mm_round_pd(D[i], 1));
  for (int i = 0; i < 6; i++) SHOW("round_pd 2", _mm_round_pd(D[i], 2));
  for (int i = 0; i < 6; i++) SHOW("round_pd 3", _mm_round_pd(D[i], 3));
  for (int i = 0; i < 6; i++) SHOW("round_pd 4", _mm_round_pd(D[i], 4));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_ss 0", _mm_round_ss(F[i], F[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_ss 1", _mm_round_ss(F[i], F[j], 1));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_ss 2", _mm_round_ss(F[i], F[j], 2));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_ss 3", _mm_round_ss(F[i], F[j], 3));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_sd 0", _mm_round_sd(D[i], D[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_sd 1", _mm_round_sd(D[i], D[j], 1));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_sd 2", _mm_round_sd(D[i], D[j], 2));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("round_sd 3", _mm_round_sd(D[i], D[j], 3));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("dp_ps 255", _mm_dp_ps(F[i], F[j], 255));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("dp_ps 113", _mm_dp_ps(F[i], F[j], 113));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("dp_ps 63", _mm_dp_ps(F[i], F[j], 63));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("dp_pd 51", _mm_dp_pd(D[i], D[j], 51));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("dp_pd 49", _mm_dp_pd(D[i], D[j], 49));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("insert_ps 0", _mm_insert_ps(F[i], F[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("insert_ps 93", _mm_insert_ps(F[i], F[j], 93));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("insert_ps 225", _mm_insert_ps(F[i], F[j], 225));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mpsadbw_epu8 0", _mm_mpsadbw_epu8(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mpsadbw_epu8 5", _mm_mpsadbw_epu8(I[i], I[j], 5));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("mpsadbw_epu8 7", _mm_mpsadbw_epu8(I[i], I[j], 7));
  for (int i = 0; i < 6; i++) SHOW("aeskeygenassist_si128 0", _mm_aeskeygenassist_si128(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("aeskeygenassist_si128 1", _mm_aeskeygenassist_si128(I[i], 1));
  for (int i = 0; i < 6; i++) SHOW("aeskeygenassist_si128 54", _mm_aeskeygenassist_si128(I[i], 54));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("clmulepi64_si128 0", _mm_clmulepi64_si128(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("clmulepi64_si128 1", _mm_clmulepi64_si128(I[i], I[j], 1));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("clmulepi64_si128 16", _mm_clmulepi64_si128(I[i], I[j], 16));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("clmulepi64_si128 17", _mm_clmulepi64_si128(I[i], I[j], 17));
  for (int i = 0; i < 6; i++) SHOW("extract_epi16 0", _mm_extract_epi16(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("extract_epi16 3", _mm_extract_epi16(I[i], 3));
  for (int i = 0; i < 6; i++) SHOW("extract_epi16 7", _mm_extract_epi16(I[i], 7));
  for (int i = 0; i < 6; i++) SHOW("extract_epi8 0", _mm_extract_epi8(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("extract_epi8 9", _mm_extract_epi8(I[i], 9));
  for (int i = 0; i < 6; i++) SHOW("extract_epi8 15", _mm_extract_epi8(I[i], 15));
  for (int i = 0; i < 6; i++) SHOW("extract_epi32 0", _mm_extract_epi32(I[i], 0));
  for (int i = 0; i < 6; i++) SHOW("extract_epi32 3", _mm_extract_epi32(I[i], 3));
  for (int i = 0; i < 6; i++) SHOW("extract_ps 0", _mm_extract_ps(F[i], 0));
  for (int i = 0; i < 6; i++) SHOW("extract_ps 2", _mm_extract_ps(F[i], 2));
  for (int i = 0; i < 6; i++) SHOW("extract_pi16 0", _mm_extract_pi16(M[i], 0));
  for (int i = 0; i < 6; i++) SHOW("extract_pi16 3", _mm_extract_pi16(M[i], 3));
  for (int i = 0; i < 6; i++) SHOW("insert_epi16 0", _mm_insert_epi16(I[i], -2, 0 & 7));
  for (int i = 0; i < 6; i++) SHOW("insert_epi8 0", _mm_insert_epi8(I[i], 0x5a, 0));
  for (int i = 0; i < 6; i++) SHOW("insert_epi32 0", _mm_insert_epi32(I[i], -7, 0 & 3));
  for (int i = 0; i < 6; i++) SHOW("insert_pi16 0", _mm_insert_pi16(M[i], 77, 0 & 3));
  for (int i = 0; i < 6; i++) SHOW("insert_epi16 9", _mm_insert_epi16(I[i], -2, 9 & 7));
  for (int i = 0; i < 6; i++) SHOW("insert_epi8 9", _mm_insert_epi8(I[i], 0x5a, 9));
  for (int i = 0; i < 6; i++) SHOW("insert_epi32 9", _mm_insert_epi32(I[i], -7, 9 & 3));
  for (int i = 0; i < 6; i++) SHOW("insert_pi16 9", _mm_insert_pi16(M[i], 77, 9 & 3));
  for (int i = 0; i < 6; i++) SHOW("shuffle_pi16 0", _mm_shuffle_pi16(M[i], 0));
  for (int i = 0; i < 6; i++) SHOW("shuffle_pi16 27", _mm_shuffle_pi16(M[i], 27));
  for (int i = 0; i < 6; i++) SHOW("shuffle_pi16 228", _mm_shuffle_pi16(M[i], 228));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_pi8 0", _mm_alignr_pi8(M[i], M[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_pi8 3", _mm_alignr_pi8(M[i], M[j], 3));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_pi8 8", _mm_alignr_pi8(M[i], M[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("alignr_pi8 15", _mm_alignr_pi8(M[i], M[j], 15));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistri 0", _mm_cmpistri(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistra 0", _mm_cmpistra(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrc 0", _mm_cmpistrc(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistro 0", _mm_cmpistro(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrs 0", _mm_cmpistrs(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrz 0", _mm_cmpistrz(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrm 0", _mm_cmpistrm(I[i], I[j], 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestri 0", _mm_cmpestri(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestra 0", _mm_cmpestra(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrc 0", _mm_cmpestrc(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestro 0", _mm_cmpestro(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrs 0", _mm_cmpestrs(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrz 0", _mm_cmpestrz(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrm 0", _mm_cmpestrm(I[i], i * 3 - 2, I[j], j * 2, 0));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistri 12", _mm_cmpistri(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistra 12", _mm_cmpistra(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrc 12", _mm_cmpistrc(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistro 12", _mm_cmpistro(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrs 12", _mm_cmpistrs(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrz 12", _mm_cmpistrz(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrm 12", _mm_cmpistrm(I[i], I[j], 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestri 12", _mm_cmpestri(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestra 12", _mm_cmpestra(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrc 12", _mm_cmpestrc(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestro 12", _mm_cmpestro(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrs 12", _mm_cmpestrs(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrz 12", _mm_cmpestrz(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrm 12", _mm_cmpestrm(I[i], i * 3 - 2, I[j], j * 2, 12));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistri 24", _mm_cmpistri(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistra 24", _mm_cmpistra(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrc 24", _mm_cmpistrc(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistro 24", _mm_cmpistro(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrs 24", _mm_cmpistrs(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrz 24", _mm_cmpistrz(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrm 24", _mm_cmpistrm(I[i], I[j], 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestri 24", _mm_cmpestri(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestra 24", _mm_cmpestra(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrc 24", _mm_cmpestrc(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestro 24", _mm_cmpestro(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrs 24", _mm_cmpestrs(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrz 24", _mm_cmpestrz(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrm 24", _mm_cmpestrm(I[i], i * 3 - 2, I[j], j * 2, 24));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistri 68", _mm_cmpistri(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistra 68", _mm_cmpistra(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrc 68", _mm_cmpistrc(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistro 68", _mm_cmpistro(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrs 68", _mm_cmpistrs(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrz 68", _mm_cmpistrz(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrm 68", _mm_cmpistrm(I[i], I[j], 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestri 68", _mm_cmpestri(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestra 68", _mm_cmpestra(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrc 68", _mm_cmpestrc(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestro 68", _mm_cmpestro(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrs 68", _mm_cmpestrs(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrz 68", _mm_cmpestrz(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrm 68", _mm_cmpestrm(I[i], i * 3 - 2, I[j], j * 2, 68));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistri 8", _mm_cmpistri(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistra 8", _mm_cmpistra(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrc 8", _mm_cmpistrc(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistro 8", _mm_cmpistro(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrs 8", _mm_cmpistrs(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrz 8", _mm_cmpistrz(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrm 8", _mm_cmpistrm(I[i], I[j], 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestri 8", _mm_cmpestri(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestra 8", _mm_cmpestra(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrc 8", _mm_cmpestrc(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestro 8", _mm_cmpestro(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrs 8", _mm_cmpestrs(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrz 8", _mm_cmpestrz(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrm 8", _mm_cmpestrm(I[i], i * 3 - 2, I[j], j * 2, 8));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistri 76", _mm_cmpistri(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistra 76", _mm_cmpistra(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrc 76", _mm_cmpistrc(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistro 76", _mm_cmpistro(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrs 76", _mm_cmpistrs(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrz 76", _mm_cmpistrz(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpistrm 76", _mm_cmpistrm(I[i], I[j], 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestri 76", _mm_cmpestri(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestra 76", _mm_cmpestra(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrc 76", _mm_cmpestrc(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestro 76", _mm_cmpestro(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrs 76", _mm_cmpestrs(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrz 76", _mm_cmpestrz(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 6; j++)
      SHOW("cmpestrm 76", _mm_cmpestrm(I[i], i * 3 - 2, I[j], j * 2, 76));
  for (int i = 0; i < 6; i++) {
    SHOW("blendv_epi8", _mm_blendv_epi8(I[i], I[(i + 1) % 6], I[(i + 2) % 6]));
    SHOW("blendv_ps", _mm_blendv_ps(F[i], F[(i + 1) % 6], F[(i + 2) % 6]));
    SHOW("blendv_pd", _mm_blendv_pd(D[i], D[(i + 1) % 6], D[(i + 2) % 6]));
    SHOW("crc32_u8", _mm_crc32_u8(i * 0x12345, i * 37));
    SHOW("crc32_u16", _mm_crc32_u16(i * 0x12345, i * 3737));
    SHOW("crc32_u32", _mm_crc32_u32(i * 0x12345, i * 373737));
    SHOW("crc32_u64", _mm_crc32_u64(i * 0x12345, i * 0x3737373737LL));
    SHOW("popcnt_u32", _mm_popcnt_u32(i * 0x12345));
    SHOW("popcnt_u64", _mm_popcnt_u64(i * 0x1234567890LL));
    SHOW("ceil_ps", _mm_ceil_ps(F[i]));
    SHOW("floor_pd", _mm_floor_pd(D[i]));
    SHOW("floor_ss", _mm_floor_ss(F[i], F[(i + 1) % 6]));
    SHOW("ceil_sd", _mm_ceil_sd(D[i], D[(i + 1) % 6]));
    SHOW("cvtsi32_ss", _mm_cvtsi32_ss(F[i], i * 1000 - 77));
    SHOW("cvtsi64_ss", _mm_cvtsi64_ss(F[i], i * 100000000000LL - 77));
    SHOW("cvtsi32_sd", _mm_cvtsi32_sd(D[i], i * 1000 - 77));
    SHOW("cvtsi64_sd", _mm_cvtsi64_sd(D[i], i * 100000000000LL - 77));
    SHOW("cvtsi32_si128", _mm_cvtsi32_si128(i * 1000 - 77));
    SHOW("cvtsi64_si128", _mm_cvtsi64_si128(i * 100000000000LL - 77));
    SHOW("cvtsi32_si64", _mm_cvtsi32_si64(i * 1000 - 77));
    SHOW("cvtsi64_m64", _mm_cvtsi64_m64(i * 100000000000LL - 77));
  }
  SHOW("set_ps", _mm_set_ps(1, 2, 3, 4));
  SHOW("setr_ps", _mm_setr_ps(1, 2, 3, 4));
  SHOW("set1_ps", _mm_set1_ps(5));
  SHOW("set_ss", _mm_set_ss(6));
  SHOW("setzero_ps", _mm_setzero_ps());
  SHOW("set_pd", _mm_set_pd(1, 2));
  SHOW("setr_pd", _mm_setr_pd(1, 2));
  SHOW("set1_pd", _mm_set1_pd(5));
  SHOW("set_sd", _mm_set_sd(6));
  SHOW("setzero_pd", _mm_setzero_pd());
  SHOW("set_epi64x", _mm_set_epi64x(1, -2));
  SHOW("set_epi32", _mm_set_epi32(1, 2, 3, -4));
  SHOW("set_epi16", _mm_set_epi16(1, 2, 3, 4, 5, 6, 7, -8));
  SHOW("set_epi8", _mm_set_epi8(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -16));
  SHOW("setr_epi32", _mm_setr_epi32(1, 2, 3, -4));
  SHOW("setr_epi16", _mm_setr_epi16(1, 2, 3, 4, 5, 6, 7, -8));
  SHOW("setr_epi8", _mm_setr_epi8(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -16));
  SHOW("set1_epi64x", _mm_set1_epi64x(-3));
  SHOW("set1_epi32", _mm_set1_epi32(-3));
  SHOW("set1_epi16", _mm_set1_epi16(-3));
  SHOW("set1_epi8", _mm_set1_epi8(-3));
  SHOW("setzero_si128", _mm_setzero_si128());
  SHOW("set_pi32", _mm_set_pi32(1, -2));
  SHOW("set_pi16", _mm_set_pi16(1, 2, 3, -4));
  SHOW("set_pi8", _mm_set_pi8(1, 2, 3, 4, 5, 6, 7, -8));
  SHOW("setr_pi16", _mm_setr_pi16(1, 2, 3, -4));
  SHOW("set1_pi8", _mm_set1_pi8(-8));
  SHOW("setzero_si64", _mm_setzero_si64());
  {
    float fa[8] __attribute__((aligned(16))) = {1, 2, 3, 4, 5, 6, 7, 8};
    double da[4] __attribute__((aligned(16))) = {1, 2, 3, 4};
    long long ia[4] __attribute__((aligned(16))) = {1, -2, 3, -4};
    SHOW("load_ps", _mm_load_ps(fa + 4));
    SHOW("loadu_ps", _mm_loadu_ps(fa + 1));
    SHOW("load_ss", _mm_load_ss(fa + 2));
    SHOW("load1_ps", _mm_load1_ps(fa + 3));
    SHOW("loadr_ps", _mm_loadr_ps(fa));
    SHOW("loadh_pi", _mm_loadh_pi(F[0], (__m64 *)(fa + 2)));
    SHOW("loadl_pi", _mm_loadl_pi(F[0], (__m64 *)(fa + 2)));
    SHOW("load_pd", _mm_load_pd(da + 2));
    SHOW("loadu_pd", _mm_loadu_pd(da + 1));
    SHOW("load_sd", _mm_load_sd(da + 1));
    SHOW("load1_pd", _mm_load1_pd(da + 3));
    SHOW("loadr_pd", _mm_loadr_pd(da));
    SHOW("loadh_pd", _mm_loadh_pd(D[0], da + 3));
    SHOW("loadl_pd", _mm_loadl_pd(D[0], da + 3));
    SHOW("loaddup_pd", _mm_loaddup_pd(da + 2));
    SHOW("load_si128", _mm_load_si128((__m128i *)ia));
    SHOW("loadu_si128", _mm_loadu_si128((__m128i *)(ia + 1)));
    SHOW("lddqu_si128", _mm_lddqu_si128((__m128i *)(ia + 1)));
    SHOW("loadl_epi64", _mm_loadl_epi64((__m128i *)(ia + 1)));
    SHOW("stream_load_si128", _mm_stream_load_si128((__m128i *)(ia + 2)));
    _mm_store_ps(fa, F[1]);
    _mm_storeu_ps(fa + 3, F[3]);
    _mm_store_ss(fa + 7, F[0]);
    _mm_storeh_pi((__m64 *)fa, F[4]);
    SHOW("stores ps", *(__m128 *)fa);
    SHOW("stores ps 2", *(__m128 *)(fa + 4));
    _mm_storer_ps(fa, F[0]);
    _mm_store1_ps(fa + 4, F[5]);
    SHOW("storer_ps", *(__m128 *)fa);
    SHOW("store1_ps", *(__m128 *)(fa + 4));
    _mm_storel_pi((__m64 *)fa, F[4]);
    SHOW("storel_pi", *(__m128 *)fa);
    _mm_store_pd(da, D[3]);
    _mm_storeh_pd(da + 2, D[4]);
    _mm_storel_pd(da + 3, D[4]);
    SHOW("stores pd", *(__m128d *)da);
    SHOW("stores pd 2", *(__m128d *)(da + 2));
    _mm_storer_pd(da, D[0]);
    _mm_store1_pd(da + 2, D[4]);
    _mm_storeu_pd(da + 1, D[5]);
    SHOW("storer_pd", *(__m128d *)da);
    SHOW("store1_pd", *(__m128d *)(da + 2));
    _mm_store_si128((__m128i *)ia, I[1]);
    _mm_storeu_si128((__m128i *)(ia + 1), I[2]);
    _mm_storel_epi64((__m128i *)(ia + 3), I[3]);
    SHOW("stores si128", *(__m128i *)ia);
    SHOW("stores si128 2", *(__m128i *)(ia + 2));
    _mm_stream_si128((__m128i *)ia, I[4]);
    _mm_stream_si32((int *)ia + 4, 77);
    _mm_stream_si64(ia + 3, -77);
    _mm_stream_ps(fa, F[2]);
    _mm_stream_pd(da, D[2]);
    SHOW("stream si128", *(__m128i *)ia);
    SHOW("stream si128 2", *(__m128i *)(ia + 2));
    SHOW("stream ps", *(__m128 *)fa);
    SHOW("stream pd", *(__m128d *)da);
    _mm_maskmoveu_si128(I[0], I[1], (char *)ia);
    _mm_maskmove_si64(M[0], M[2], (char *)(ia + 2));
    SHOW("maskmove", *(__m128i *)ia);
    SHOW("maskmove 2", *(__m128i *)(ia + 2));
  }
  {
    __m128 r0 = F[0], r1 = F[1], r2 = F[3], r3 = F[4];
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    SHOW("transpose", r0);
    SHOW("transpose", r1);
    SHOW("transpose", r2);
    SHOW("transpose", r3);
    unsigned csr = _mm_getcsr();
    SHOW("getcsr", csr & 0xffc0);
    _MM_SET_ROUNDING_MODE(_MM_ROUND_UP);
    SHOW("round up", _mm_cvtss_si32(F[0]));
    SHOW("round up", _mm_cvtps_epi32(F[3]));
    SHOW("round up", _mm_round_ps(F[3], _MM_FROUND_CUR_DIRECTION));
    _mm_setcsr(csr);
    SHOW("rounding", _MM_GET_ROUNDING_MODE());
    _mm_sfence();
    _mm_lfence();
    _mm_mfence();
    _mm_pause();
    _mm_prefetch((char *)&csr, _MM_HINT_T0);
    _mm_clflush(&csr);
    _mm_empty();
  }
  if (!verbose)
    printf("OK\n");
  return 0;
}

