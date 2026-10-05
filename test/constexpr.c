#include "test.h"

float g40 = 1.5;
double g41 = 0.0 ? 55 : (0, 1 + 1 * 5.0 / 2 * (double)2 * (int)2.0);

// int and unsigned int arithmetic wraps to 32 bits, as at run time.
unsigned long g42 = 4294967295u + 1u;
unsigned long g43 = ~0u >> 4;
double g44 = -1u;
long g45 = 0u - 1;
long g46 = 0x10000u * 0x10000u;
unsigned long g47 = 1u << 31 << 1;
enum { E48 = ~0u >> 28, E49 = (unsigned char)~0 >> 4 };

int main() {
  ASSERT(10, ({ enum { ten=1+2+3+4 }; ten; }));
  ASSERT(1, ({ int i=0; switch(3) { case 5-2+0*3: i++; } i; }));
  ASSERT(8, ({ int x[1+1]; sizeof(x); }));
  ASSERT(6, ({ char x[8-2]; sizeof(x); }));
  ASSERT(6, ({ char x[2*3]; sizeof(x); }));
  ASSERT(3, ({ char x[12/4]; sizeof(x); }));
  ASSERT(2, ({ char x[12%10]; sizeof(x); }));
  ASSERT(0b100, ({ char x[0b110&0b101]; sizeof(x); }));
  ASSERT(0b111, ({ char x[0b110|0b101]; sizeof(x); }));
  ASSERT(0b110, ({ char x[0b111^0b001]; sizeof(x); }));
  ASSERT(4, ({ char x[1<<2]; sizeof(x); }));
  ASSERT(2, ({ char x[4>>1]; sizeof(x); }));
  ASSERT(2, ({ char x[(1==1)+1]; sizeof(x); }));
  ASSERT(1, ({ char x[(1!=1)+1]; sizeof(x); }));
  ASSERT(1, ({ char x[(1<1)+1]; sizeof(x); }));
  ASSERT(2, ({ char x[(1<=1)+1]; sizeof(x); }));
  ASSERT(2, ({ char x[1?2:3]; sizeof(x); }));
  ASSERT(3, ({ char x[0?2:3]; sizeof(x); }));
  ASSERT(3, ({ char x[(1,3)]; sizeof(x); }));
  ASSERT(2, ({ char x[!0+1]; sizeof(x); }));
  ASSERT(1, ({ char x[!1+1]; sizeof(x); }));
  ASSERT(2, ({ char x[~-3]; sizeof(x); }));
  ASSERT(2, ({ char x[(5||6)+1]; sizeof(x); }));
  ASSERT(1, ({ char x[(0||0)+1]; sizeof(x); }));
  ASSERT(2, ({ char x[(1&&1)+1]; sizeof(x); }));
  ASSERT(1, ({ char x[(1&&0)+1]; sizeof(x); }));
  ASSERT(3, ({ char x[(int)3]; sizeof(x); }));
  ASSERT(15, ({ char x[(char)0xffffff0f]; sizeof(x); }));
  ASSERT(0x10f, ({ char x[(short)0xffff010f]; sizeof(x); }));
  ASSERT(4, ({ char x[(int)0xfffffffffff+5]; sizeof(x); }));
  ASSERT(8, ({ char x[(int*)0+2]; sizeof(x); }));
  ASSERT(12, ({ char x[(int*)16-1]; sizeof(x); }));
  ASSERT(3, ({ char x[(int*)16-(int*)4]; sizeof(x); }));

  ASSERT(4, ({ char x[(-1>>31)+5]; sizeof(x); }));
  ASSERT(255, ({ char x[(unsigned char)0xffffffff]; sizeof(x); }));
  ASSERT(0x800f, ({ char x[(unsigned short)0xffff800f]; sizeof(x); }));
  ASSERT(1, ({ char x[(unsigned int)0xfffffffffff>>31]; sizeof(x); }));
  ASSERT(1, ({ char x[(long)-1/((long)1<<62)+1]; sizeof(x); }));
  ASSERT(4, ({ char x[(unsigned long)-1/((long)1<<62)+1]; sizeof(x); }));
  ASSERT(1, ({ char x[(unsigned)1<-1]; sizeof(x); }));
  ASSERT(1, ({ char x[(unsigned)1<=-1]; sizeof(x); }));

  ASSERT(1, g40==1.5);
  ASSERT(1, g41==11);

  ASSERT(0, g42);
  ASSERT(0x0fffffff, g43);
  ASSERT(1, g44 == 4294967295.0);
  ASSERT(1, g45 == 4294967295);
  ASSERT(0, g46);
  ASSERT(0, g47);
  ASSERT(15, E48);
  ASSERT(15, E49);
  ASSERT(2, ({ int r = 1; if (~4294967295U) r = 3; else r = 2; r; }));
  ASSERT(1, ({ int r = 0; switch (5) { case 0xffffffffu + 6: r = 1; } r; }));
  ASSERT(3, ({ char x[(0u - 1) / 0x40000000]; sizeof(x); }));

  printf("OK\n");
  return 0;
}
