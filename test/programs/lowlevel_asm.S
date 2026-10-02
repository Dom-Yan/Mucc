// Functions in preprocessed assembly, called from lowlevel.c.
#define FUNC(name) .globl name; .type name, @function; name:

  .text
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
