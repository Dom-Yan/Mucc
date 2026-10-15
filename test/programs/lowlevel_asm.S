// Functions in preprocessed assembly, called from lowlevel.c.
#define FUNC(name) .globl name; .type name, @function; name:

  .text
#ifdef __aarch64__
// long asm_add(long a, long b)
FUNC(asm_add)
  add x0, x0, x1
  ret
  .size asm_add, .-asm_add

// long asm_strlen(const char *s): the C library's, more or less
FUNC(asm_strlen)
  mov x1, x0
1:
  ldrb w2, [x1]
  cbz w2, 2f
  add x1, x1, 1
  b 1b
2:
  sub x0, x1, x0
  ret
  .size asm_strlen, .-asm_strlen
#else
// long asm_add(long a, long b)
FUNC(asm_add)
  lea (%rdi,%rsi), %rax
  ret
  .size asm_add, .-asm_add

// long asm_strlen(const char *s): the C library's, more or less
FUNC(asm_strlen)
  mov %rdi, %rax
1:
  cmpb $0, (%rax)
  je 2f
  inc %rax
  jmp 1b
2:
  sub %rdi, %rax
  ret
  .size asm_strlen, .-asm_strlen
#endif
