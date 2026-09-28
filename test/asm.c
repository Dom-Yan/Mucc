#include "test.h"

// asm at file scope, as musl's crt1.c defines _start (both spellings)
asm(".text\n"
    ".globl asm_toplevel1\n"
    "asm_toplevel1:\n"
    "  mov $42, %eax\n"
    "  ret\n");
__asm__(".globl asm_toplevel2; asm_toplevel2: mov $43, %eax; ret");
int asm_toplevel1(void);
int asm_toplevel2(void);

char *asm_fn1(void) {
  asm("mov $50, %rax\n\t"
      "mov %rbp, %rsp\n\t"
      "pop %rbp\n\t"
      "ret");
}

char *asm_fn2(void) {
  asm inline volatile("mov $55, %rax\n\t"
                      "mov %rbp, %rsp\n\t"
                      "pop %rbp\n\t"
                      "ret");
}

int main() {
  ASSERT(42, asm_toplevel1());
  ASSERT(43, asm_toplevel2());
  ASSERT(50, (long)asm_fn1());
  ASSERT(55, (long)asm_fn2());

  printf("OK\n");
  return 0;
}
