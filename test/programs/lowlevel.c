// Low level: inline asm (cpuid, rdtsc, extended asm with constraints), a
// function written in a .S file, raw system calls in asm, and bit
// builtins.
#include "check.h"
#include <stdint.h>

long asm_add(long a, long b);       // in lowlevel_asm.S
long asm_strlen(const char *s);     // in lowlevel_asm.S

static long raw_write(int fd, const void *buf, long len) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(1), "D"(fd), "S"(buf), "d"(len)
                   : "rcx", "r11", "memory");
  return ret;
}

int main(void) {
  uint32_t a, b, c, d;
  __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
  char vendor[13] = {0};
  memcpy(vendor, &b, 4);
  memcpy(vendor + 4, &d, 4);
  memcpy(vendor + 8, &c, 4);
  CHECK(!strcmp(vendor, "GenuineIntel") || !strcmp(vendor, "AuthenticAMD"));

  uint32_t lo1, hi1, lo2, hi2;
  __asm__ volatile("rdtsc" : "=a"(lo1), "=d"(hi1));
  __asm__ volatile("rdtsc" : "=a"(lo2), "=d"(hi2));
  CHECK(((uint64_t)hi2 << 32 | lo2) >= ((uint64_t)hi1 << 32 | lo1));

  long x = 40;
  __asm__("addq %1, %0" : "+r"(x) : "ri"(2L));
  CHECK(x == 42);

  CHECK(asm_add(40, 2) == 42);
  CHECK(asm_strlen("assembly") == 8);
  CHECK(raw_write(1, "", 0) == 0);

  CHECK(__builtin_popcountll(0xffffffffffffffffULL) == 64);
  CHECK(__builtin_clz(1) == 31 && __builtin_ctzl(1UL << 40) == 40);
  CHECK(__builtin_bswap32(0x12345678) == 0x78563412);
  long r;
  CHECK(__builtin_mul_overflow(1L << 62, 4, &r));

  return done("lowlevel");
}
