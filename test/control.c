#include "test.h"

/*
 * This is a block comment.
 */

// Code that can't run isn't compiled, as with gcc, so it may name a
// function that isn't defined anywhere: this file only links if every
// call to never_defined() below is left out.
void never_defined(void);
#define FEATURE 0

static int dead_code(int x) {
  if (FEATURE)
    never_defined();
  if (FEATURE && x)
    never_defined();
  if (!FEATURE || x)
    x++;
  else
    never_defined();
  x += FEATURE ? (never_defined(), 1) : 2;
  if (sizeof(int) == 4)
    return x;
  never_defined(); // after an `if` that always returns
  return 0;
}

static int dead_but_labeled(int x) {
  goto inside;
  if (0) {
  inside: // reachable by the goto, so kept
    x = 42;
  }
  return x;
}

int main() {
  ASSERT(4, dead_code(1));
  ASSERT(42, dead_but_labeled(0));

  // Constant conditions with floating operands are decided as such:
  // 2.5 > 2.0, and 0.5 is true.
  ASSERT(1, (2.5 > 2.0) ? 1 : 0);
  ASSERT(0, (2.5 == 2.0) ? 1 : 0);
  ASSERT(2, 0.5 ? 2 : 3);
  ASSERT(1, 0.5 && 1);
  ASSERT(0, !0.5);
  ASSERT(5, ({ int x = 0; if (0.25 < 0.5) x = 5; x; }));
  ASSERT(7, ({ int x = 7; if (1e-300 == 0.0) x = 0; x; }));
  ASSERT(5, ({ int x = 5; switch (x) { case 1: return 1; x = 0; case 5: break; } x; }));
  ASSERT(3, ({ int x; if (0) x=2; else x=3; x; }));
  ASSERT(3, ({ int x; if (1-1) x=2; else x=3; x; }));
  ASSERT(2, ({ int x; if (1) x=2; else x=3; x; }));
  ASSERT(2, ({ int x; if (2-1) x=2; else x=3; x; }));

  ASSERT(55, ({ int i=0; int j=0; for (i=0; i<=10; i=i+1) j=i+j; j; }));

  ASSERT(10, ({ int i=0; while(i<10) i=i+1; i; }));

  ASSERT(3, ({ 1; {2;} 3; }));
  ASSERT(5, ({ ;;; 5; }));

  ASSERT(10, ({ int i=0; while(i<10) i=i+1; i; }));
  ASSERT(55, ({ int i=0; int j=0; while(i<=10) {j=i+j; i=i+1;} j; }));

  ASSERT(3, (1,2,3));
  ASSERT(5, ({ int i=2, j=3; (i=5,j)=6; i; }));
  ASSERT(6, ({ int i=2, j=3; (i=5,j)=6; j; }));

  ASSERT(55, ({ int j=0; for (int i=0; i<=10; i=i+1) j=j+i; j; }));
  ASSERT(3, ({ int i=3; int j=0; for (int i=0; i<=10; i=i+1) j=j+i; i; }));

  ASSERT(1, 0||1);
  ASSERT(1, 0||(2-2)||5);
  ASSERT(0, 0||0);
  ASSERT(0, 0||(2-2));

  ASSERT(0, 0&&1);
  ASSERT(0, (2-2)&&5);
  ASSERT(1, 1&&5);

  ASSERT(3, ({ int i=0; goto a; a: i++; b: i++; c: i++; i; }));
  ASSERT(2, ({ int i=0; goto e; d: i++; e: i++; f: i++; i; }));
  ASSERT(1, ({ int i=0; goto i; g: i++; h: i++; i: i++; i; }));

  ASSERT(1, ({ typedef int foo; goto foo; foo:; 1; }));

  ASSERT(3, ({ int i=0; for(;i<10;i++) { if (i == 3) break; } i; }));
  ASSERT(4, ({ int i=0; while (1) { if (i++ == 3) break; } i; }));
  ASSERT(3, ({ int i=0; for(;i<10;i++) { for (;;) break; if (i == 3) break; } i; }));
  ASSERT(4, ({ int i=0; while (1) { while(1) break; if (i++ == 3) break; } i; }));

  ASSERT(10, ({ int i=0; int j=0; for (;i<10;i++) { if (i>5) continue; j++; } i; }));
  ASSERT(6, ({ int i=0; int j=0; for (;i<10;i++) { if (i>5) continue; j++; } j; }));
  ASSERT(10, ({ int i=0; int j=0; for(;!i;) { for (;j!=10;j++) continue; break; } j; }));
  ASSERT(11, ({ int i=0; int j=0; while (i++<10) { if (i>5) continue; j++; } i; }));
  ASSERT(5, ({ int i=0; int j=0; while (i++<10) { if (i>5) continue; j++; } j; }));
  ASSERT(11, ({ int i=0; int j=0; while(!i) { while (j++!=10) continue; break; } j; }));

  ASSERT(5, ({ int i=0; switch(0) { case 0:i=5;break; case 1:i=6;break; case 2:i=7;break; } i; }));
  ASSERT(6, ({ int i=0; switch(1) { case 0:i=5;break; case 1:i=6;break; case 2:i=7;break; } i; }));
  ASSERT(7, ({ int i=0; switch(2) { case 0:i=5;break; case 1:i=6;break; case 2:i=7;break; } i; }));
  ASSERT(0, ({ int i=0; switch(3) { case 0:i=5;break; case 1:i=6;break; case 2:i=7;break; } i; }));
  ASSERT(5, ({ int i=0; switch(0) { case 0:i=5;break; default:i=7; } i; }));
  ASSERT(7, ({ int i=0; switch(1) { case 0:i=5;break; default:i=7; } i; }));
  ASSERT(2, ({ int i=0; switch(1) { case 0: 0; case 1: 0; case 2: 0; i=2; } i; }));
  ASSERT(0, ({ int i=0; switch(3) { case 0: 0; case 1: 0; case 2: 0; i=2; } i; }));

  ASSERT(3, ({ int i=0; switch(-1) { case 0xffffffff: i=3; break; } i; }));

  ASSERT(7, ({ int i=0; int j=0; do { j++; } while (i++ < 6); j; }));
  ASSERT(4, ({ int i=0; int j=0; int k=0; do { if (++j > 3) break; continue; k++; } while (1); j; }));

  ASSERT(0, 0.0 && 0.0);
  ASSERT(0, 0.0 && 0.1);
  ASSERT(0, 0.3 && 0.0);
  ASSERT(1, 0.3 && 0.5);
  ASSERT(0, 0.0 || 0.0);
  ASSERT(1, 0.0 || 0.1);
  ASSERT(1, 0.3 || 0.0);
  ASSERT(1, 0.3 || 0.5);
  ASSERT(5, ({ int x; if (0.0) x=3; else x=5; x; }));
  ASSERT(3, ({ int x; if (0.1) x=3; else x=5; x; }));
  ASSERT(5, ({ int x=5; if (0.0) x=3; x; }));
  ASSERT(3, ({ int x=5; if (0.1) x=3; x; }));
  ASSERT(10, ({ double i=10.0; int j=0; for (; i; i--, j++); j; }));
  ASSERT(10, ({ double i=10.0; int j=0; do j++; while(--i); j; }));

  ASSERT(2, ({ int i=0; switch(7) { case 0 ... 5: i=1; break; case 6 ... 20: i=2; break; } i; }));
  ASSERT(1, ({ int i=0; switch(7) { case 0 ... 7: i=1; break; case 8 ... 10: i=2; break; } i; }));
  ASSERT(1, ({ int i=0; switch(7) { case 0: i=1; break; case 7 ... 7: i=1; break; } i; }));

  // 64-bit case values, which don't fit in an instruction's immediate.
  ASSERT(1, ({ long x=0x4000000000000001; int i=0; switch(x) { case 0x4000000000000001: i=1; break; case 1: i=2; break; } i; }));
  ASSERT(2, ({ long x=1; int i=0; switch(x) { case 0x4000000000000001: i=1; break; case 1: i=2; break; } i; }));
  ASSERT(0, ({ long x=0x100000001; int i=0; switch(x) { case 1: i=1; break; } i; }));
  ASSERT(1, ({ long x=0x100000003; int i=0; switch(x) { case 0x100000000 ... 0x100000005: i=1; break; } i; }));
  ASSERT(0, ({ long x=3; int i=0; switch(x) { case 0x100000000 ... 0x100000005: i=1; break; } i; }));
  ASSERT(1, ({ long x=-0x100000000; int i=0; switch(x) { case -0x100000000: i=1; break; } i; }));
  ASSERT(1, ({ unsigned long x=-1; int i=0; switch(x) { case 0xffffffffffffffff: i=1; break; } i; }));
  ASSERT(1, ({ unsigned long x=-1; int i=0; switch(x) { case 1 ... 0xffffffffffffffff: i=1; break; } i; }));
  ASSERT(1, ({ unsigned x=-1; int i=0; switch(x) { case 1 ... 0xffffffff: i=1; break; } i; }));
  ASSERT(1, ({ char x=-1; int i=0; switch(x) { case -1: i=1; break; } i; }));

  ASSERT(3, ({ void *p = &&v11; int i=0; goto *p; v11:i++; v12:i++; v13:i++; i; }));
  ASSERT(2, ({ void *p = &&v22; int i=0; goto *p; v21:i++; v22:i++; v23:i++; i; }));
  ASSERT(1, ({ void *p = &&v33; int i=0; goto *p; v31:i++; v32:i++; v33:i++; i; }));

  ASSERT(3, ({ static void *p[]={&&v41,&&v42,&&v43}; int i=0; goto *p[0]; v41:i++; v42:i++; v43:i++; i; }));
  ASSERT(2, ({ static void *p[]={&&v52,&&v52,&&v53}; int i=0; goto *p[1]; v51:i++; v52:i++; v53:i++; i; }));
  ASSERT(1, ({ static void *p[]={&&v62,&&v62,&&v63}; int i=0; goto *p[2]; v61:i++; v62:i++; v63:i++; i; }));

  // [GNU] A statement expression whose last statement isn't an
  // expression has type void.
  ASSERT(3, ({ int x = 1; ({ if (x) x = 3; }); x; }));
  ASSERT(2, ({ int x = 0; ({ for (int i = 0; i < 2; i++) x++; }); x; }));
  ASSERT(5, ({ int x = 5; ({ int y = 1; }); ({}); x; }));

  printf("OK\n");
  return 0;
}
