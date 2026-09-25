#include "test.h"

// GNU asm with operands (see asm_stmt() in src/parser.c). Every expected
// value here is what the same code gives with gcc.

static long add(long a, long b) {
  asm("add %1, %0" : "+r"(a) : "r"(b));
  return a;
}

static int add_int(int a, int b) {
  int r;
  asm("mov %1, %0\n\tadd %2, %0" : "=&r"(r) : "r"(a), "r"(b));
  return r;
}

static unsigned char add_char(unsigned char a, unsigned char b) {
  asm("addb %b1, %b0" : "+q"(a) : "q"(b));
  return a;
}

static short add_short(short a, short b) {
  asm("addw %w1, %w0" : "+r"(a) : "r"(b));
  return a;
}

// Each register that a constraint letter names, as outputs...
static long fixed_outputs(void) {
  long a, b, c, d, S, D;
  asm("mov $1, %%rax\n\tmov $2, %%rbx\n\tmov $3, %%rcx\n\t"
      "mov $4, %%rdx\n\tmov $5, %%rsi\n\tmov $6, %%rdi"
      : "=a"(a), "=b"(b), "=c"(c), "=d"(d), "=S"(S), "=D"(D));
  return a + b * 10 + c * 100 + d * 1000 + S * 10000 + D * 100000;
}

// ...and as inputs.
static long fixed_inputs(long a, long b, long c, long d, long S, long D) {
  long r;
  asm("mov %%rax, %0\n\tadd %%rbx, %0\n\tadd %%rcx, %0\n\t"
      "add %%rdx, %0\n\tadd %%rsi, %0\n\tadd %%rdi, %0"
      : "=&r"(r)
      : "a"(a), "b"(b * 10), "c"(c * 100), "d"(d * 1000), "S"(S * 10000),
        "D"(D * 100000));
  return r;
}

// Named clobbers keep the operands out of those registers, and the
// function saves the callee-saved ones.
static long clobbers(long v) {
  long r;
  asm("mov %1, %%rbx\n\tmov %%rbx, %%r12\n\tmov %%r12, %0"
      : "=r"(r)
      : "r"(v)
      : "rbx", "r12", "cc", "memory");
  return r;
}

// The caller keeps its most used variables in %rbx and %r12-%r15, so the
// functions above must leave those as they were.
static long callee_saved(void) {
  long x = 1, y = 2, z = 3, w = 4, v = 5;
  for (int i = 0; i < 10; i++) {
    x += fixed_inputs(1, 1, 1, 1, 1, 1) - 111111;
    y += clobbers(i) - i;
    z += fixed_outputs() - 654321;
    w += x + y + z;
    v += w;
  }
  return x + y * 10 + z * 100 + w * 1000 + v * 10000;
}

static int mem_inout(void) {
  int x = 5, y;
  asm("movl %1, %%eax\n\tmovl %%eax, %0" : "=m"(y) : "m"(x) : "eax");
  return y;
}

static void inc_mem(int *p) {
  asm("incl %0" : "+m"(*p));
}

struct S { int a, b; };

static int mem_member(struct S *s) {
  int r;
  asm("movl %1, %0" : "=r"(r) : "m"(s->b));
  return r;
}

static long imm(void) {
  long r;
  asm("mov %1, %0" : "=r"(r) : "i"(42));
  return r;
}

static long imm_bare(void) {
  long r, s;
  asm("mov $%c2, %0\n\tmov $%n3, %1" : "=r"(r), "=r"(s) : "n"(7), "i"(5));
  return r * 100 + s;
}

int global_var = 77;

static int imm_address(void) {
  int *p;
  asm("lea %c1(%%rip), %0" : "=r"(p) : "i"(&global_var));
  return *p;
}

static long matching(long a) {
  long r;
  asm("add $10, %0" : "=r"(r) : "0"(a));
  return r;
}

static long named(long a, long b) {
  long r;
  asm("mov %[x], %[out]\n\tsub %[y], %[out]" : [out] "=&r"(r) : [x] "r"(a), [y] "r"(b));
  return r;
}

// %= makes the labels of each asm statement different.
static int unique_labels(int a, int b) {
  asm("test %0, %0\n\tjz .Lz%=\n\tadd $1, %0\n.Lz%=:" : "+r"(a));
  asm("test %0, %0\n\tjz .Lz%=\n\tadd $1, %0\n.Lz%=:" : "+r"(b));
  return a * 10 + b;
}

static int high_byte(void) {
  int r;
  asm("movzbl %h1, %0" : "=r"(r) : "a"(0x1234));
  return r;
}

static long k_modifier(long x) {
  long r;
  asm("movl %k1, %k0" : "=r"(r) : "r"(x));
  return r;
}

static long array_input(void) {
  long a[2] = {3, 9};
  long r;
  asm("mov 8(%1), %0" : "=r"(r) : "r"(a));
  return r;
}

static int att_alternative(int x) {
  asm("{addl $1, %0|add %0, 1}" : "+r"(x));
  return x;
}

static long sys_write(int fd, const void *buf, long n) {
  long ret;
  asm volatile("syscall"
               : "=a"(ret)
               : "a"(1L), "D"((long)fd), "S"(buf), "d"(n)
               : "rcx", "r11", "memory");
  return ret;
}

// As musl does: rt_sigprocmask(SIG_BLOCK, NULL, &old, 8) takes its fourth
// argument in %r10, through a register variable. With anything but 8
// there, the kernel returns -EINVAL.
static long sys_sigprocmask(void) {
  unsigned long old;
  register long r10 asm("r10") = 8;
  long ret;
  asm volatile("syscall"
               : "=a"(ret)
               : "a"(14L), "D"(0L), "S"(0L), "d"(&old), "r"(r10)
               : "rcx", "r11", "memory");
  return ret;
}

int main() {
  ASSERT(12, add(5, 7));
  ASSERT(1, add(0x100000000, 5) == 0x100000005);
  ASSERT(30, add_int(10, 20));
  ASSERT(4, add_char(250, 10));
  ASSERT(-2, add_short(32767, 32767));
  ASSERT(654321, fixed_outputs());
  ASSERT(654321, fixed_inputs(1, 2, 3, 4, 5, 6));
  ASSERT(9, clobbers(9));
  ASSERT(3814321, callee_saved());
  ASSERT(5, mem_inout());
  ASSERT(8, ({ int x = 7; inc_mem(&x); x; }));
  ASSERT(4, ({ struct S s = {3, 4}; mem_member(&s); }));
  ASSERT(42, imm());
  ASSERT(695, imm_bare());
  ASSERT(77, imm_address());
  ASSERT(15, matching(5));
  ASSERT(6, named(10, 4));
  ASSERT(0, unique_labels(0, 0));
  ASSERT(22, unique_labels(1, 1));
  ASSERT(20, unique_labels(1, 0));
  ASSERT(0x12, high_byte());
  ASSERT(1, k_modifier(0x100000005) == 5);
  ASSERT(9, array_input());
  ASSERT(4, att_alternative(3));
  ASSERT(21, sys_write(1, "written by a syscall\n", 21));
  ASSERT(0, sys_sigprocmask());

  printf("OK\n");
  return 0;
}
