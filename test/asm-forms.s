# Every instruction form and directive mucc's code generator can emit,
# plus the edge cases of their encodings. test/asm.sh assembles this with
# both mucc and GNU as and checks the objects are the same.

  .file "asm-forms.c"
  .file 1 "asm-forms.c"
  .file 2 "/usr/include/stdio.h"

  .globl func
  .text
  .type func, @function
func:
  .loc 1 10
  push %rbp
  push %r12
  mov %rsp, %rbp
  sub $16, %rsp
  sub $1024, %rsp
  mov %rsp, -8(%rbp)
  .loc 2 20
  pop %r12
  pop %rdi

  # ALU: imm8, accumulator and general forms, all sizes
  add $1, %rax
  add $1000, %rax
  add $1000, %rdi
  add $-128, %eax
  add $-129, %eax
  and $255, %eax
  and $0xffffffff, %eax
  and $4294967295, %eax
  or $12, %ah
  or $12, %al
  xor $1, %dil
  cmp $0, %eax
  cmp $0, %rax
  cmp $100000, %r9
  sub %edi, %eax
  add %rdi, %rax
  and %r8, %r15
  xor %eax, %eax
  cmp %dil, %al
  addq $8, -16(%rbp)
  addq $300, -16(%rbp)
  add -8(%rbp), %rax
  add %rax, -8(%rbp)
  add $v1@tpoff, %rax

  # mov
  mov $0, %eax
  mov $42, %rax
  mov $-1, %rax
  mov $2147483647, %rax
  mov $2147483648, %rax
  mov $4294967293, %rax
  mov $4607182418800017408, %rax
  mov $18446744073709551615, %rax
  mov $1602224128, %eax
  mov $5, %r10
  mov $5, %r10d
  mov $5, %ax
  mov $5, %al
  mov $5, %sil
  movl $7, -4(%rbp)
  movl $7, 1000(%rbp)
  mov %rsp, %rbp
  mov %r9, %rax
  mov %al, (%rdi)
  mov %sil, 3(%rdi)
  mov %ax, -12(%rsp)
  mov %eax, (%rdx)
  mov %rax, (%r8)
  mov (%rax), %rax
  mov (%rsp), %rdi
  mov 8(%rsp), %rdi
  mov (%rbp), %rax
  mov (%r12), %rax
  mov (%r13), %rax
  mov 8(%r13), %rax
  mov -2000(%rbp), %rax
  mov 16(%rax), %eax
  mov global_var(%rip), %rax
  mov .Ldata(%rip), %rax
  mov %fs:0, %rax
  mov ext_var@GOTPCREL(%rip), %rax
  mov .Ldata@GOTPCREL(%rip), %rax
  movq %rbp, -48(%rbp)
  movq %r9, -8(%rbp)
  movq %rax, -8(%rsp)
  movq %rax, %xmm0
  movq %rax, %xmm1
  movq %xmm0, %rax

  # widening moves
  movsbl (%rax), %eax
  movsbl 5(%rax), %eax
  movsbl %al, %eax
  movswl (%rax), %eax
  movswl %ax, %eax
  movzbl (%rax), %eax
  movzbl %al, %eax
  movzbl %dil, %eax
  movzwl -10(%rsp), %eax
  movzwl %ax, %eax
  movzb %al, %rax
  movzx %al, %rax
  movzx %al, %eax
  movzx %ax, %eax
  movsxd (%rax), %rax
  movsxd -8(%rbp), %rax
  movsxd %eax, %rax
  mov %eax, %eax

  # lea
  lea -8(%rbp), %rax
  lea 16(%rbp), %rdi
  lea .Ldata(%rip), %rax
  lea func(%rip), %rax
  lea .Llabel(%rip), %rax
  lea ext_func(%rip), %rax
  data16 lea v1@tlsgd(%rip), %rdi
  .value 0x6666
  rex64
  call __tls_get_addr@PLT

  # multiply, divide, unary
  imul %rdi, %rax
  imul %edi, %eax
  cqo
  cdq
  idiv %rdi
  idiv %edi
  div %rdi
  div %edi
  neg %rax
  neg %eax
  not %rax
  inc %rax
  dec %eax

  # shifts
  shl $3, %rax
  shl $1, %rax
  shr $1, %eax
  sar $63, %rax
  shr %rdi
  shl %cl, %rax
  shr %cl, %eax
  sar %cl, %rax
  shr $8, %rdi

  # test and set
  test %rax, %rax
  test %eax, %eax
  test %al, %al
  sete %al
  setne %al
  setl %al
  setle %al
  setg %al
  setge %al
  setb %al
  setbe %al
  seta %al
  setae %al
  setp %al
  setnp %dl
  sete %sil

  # jumps: short, long, numeric labels, indirect
  je .Lnear
  jne .Lnear
  jmp .Lnear
  js 1f
  jns 1f
  jbe 1f
1:
  jmp 1b
.Lnear:
  jmp .Lfar
  je .Lfar
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
  mov $1, %rax
.Lfar:
  jmp *%rax
  call *%r10
  call *%rax
  call ext_func
  call ext_func@PLT
  call func
  call local_func

  # the u64 -> double cast sequence, several statements on one line
  test %rax,%rax; js 1f; pxor %xmm0,%xmm0; cvtsi2sd %rax,%xmm0; jmp 2f; 1: mov %rax,%rdi; and $1,%eax; pxor %xmm0,%xmm0; shr %rdi; or %rax,%rdi; cvtsi2sd %rdi,%xmm0; addsd %xmm0,%xmm0; 2:

  # SSE
  movsd (%rax), %xmm0
  movsd (%rsp), %xmm1
  movsd 8(%rsp), %xmm7
  movsd %xmm0, (%rsp)
  movsd %xmm7, -16(%rbp)
  movsd %xmm1, %xmm0
  movss (%rax), %xmm0
  movss %xmm0, (%rdi)
  movss %xmm2, -4(%rbp)
  addsd %xmm1, %xmm0
  subsd %xmm1, %xmm0
  mulsd %xmm1, %xmm0
  divsd %xmm1, %xmm0
  addss %xmm1, %xmm0
  subss %xmm1, %xmm0
  mulss %xmm1, %xmm0
  divss %xmm1, %xmm0
  cvtsd2ss %xmm0, %xmm0
  cvtss2sd %xmm0, %xmm0
  cvtsi2sdl %eax, %xmm0
  cvtsi2sdq %rax, %xmm0
  cvtsi2ssl %eax, %xmm0
  cvtsi2ssq %rax, %xmm0
  cvttsd2sil %xmm0, %eax
  cvttsd2siq %xmm0, %rax
  cvttss2sil %xmm0, %eax
  cvttss2siq %xmm0, %rax
  ucomisd %xmm0, %xmm1
  ucomisd %xmm1, %xmm0
  ucomiss %xmm0, %xmm1
  comisd %xmm0, %xmm1
  comiss %xmm2, %xmm9
  comisd -8(%rbp), %xmm0
  xorpd %xmm1, %xmm0
  xorps %xmm1, %xmm1
  pxor %xmm0, %xmm0

  # x87
  fldt (%rax)
  fldt 16(%rbp)
  fstpt (%rdi)
  fstpt -32(%rbp)
  flds -4(%rsp)
  fldl -8(%rsp)
  fstps -4(%rsp)
  fstpl -8(%rsp)
  fadds -4(%rsp)
  fildl -8(%rsp)
  fildll -8(%rsp)
  fildq -8(%rsp)
  fistps -24(%rsp)
  fistpl -24(%rsp)
  fistpq -24(%rsp)
  fnstcw -10(%rsp)
  fldcw -12(%rsp)
  fstp %st(0)
  fstp %st(1)
  faddp
  fmulp
  fsubrp
  fdivrp
  fcomip
  fucomip
  fchs
  fldz

  # FPU and SSE control
  fnstsw %ax
  fnstsw -2(%rsp)
  fnclex
  fnstenv (%rdi)
  fldenv -1(%rdi)
  stmxcsr -8(%rsp)
  ldmxcsr 27(%rdi)
  ldmxcsr (%r8)

  # addressing: base, index and scale
  mov (%rdx,%rcx,8), %rax
  add (%rdx,%rcx,8), %rax
  mov %sil, -1(%rdi,%rdx)
  lea -1(%rsi,%rdx), %rsi
  mov %rax, 8(%rsp,%rax,2)
  mov %rax, (%rbp,%r8,4)
  mov %rax, (%r13,%r9)
  mov %r10, 1000(%r12,%r15,1)
  mov %eax, (,%rcx,4)
  mov %eax, 16(,%r11,8)

  # constant expressions, and C comments
  mov %ax, (-1-2)(%rdi,%rdx)
  mov %rbx, 72+8(%rdi)
  cmp $0x3fff+13, %ax
  cmp $0x3fff-64, %ax
  /* a comment */ nop /* and another */
  /* one that
     spans lines */

  # push, pop and test with memory and immediates
  pushq 64(%rbx)
  popq 64(%rdi)
  push (%r12)
  popq 8(%r9,%rax,8)
  pushq $1
  pushq $-128
  pushq $0x1f80
  push $-129
  test $7, %edi
  test $7, %eax
  test $1, %al
  test $0x80, %cl
  test $0x1000, %r9
  test $0x1000, %ax
  testq $1, (%rdi)
  testb $1, 1(%rsp)

  # string instructions
  cld; std
  rep movsb
  rep
  movsq
  movsb
  stosq
  hlt

  # bit scans, and musl's atomics (arch/x86_64/atomic_arch.h)
  bsf %rcx, %rax
  bsr %ecx, %eax
  bsf (%rdi), %r9
  bsrq 8(%rsp), %r10
  bsr %rcx,%rax ; xor $63,%rax
  lock ; xadd %eax, (%rcx)
  lock xadd %r8, (%rdi)
  xadd %al, (%rdx)
  lock ; decl (%rdi)
  lock ; incl 4(%rdi)
  lock ; and %eax, (%rdi)
  lock ; or %rax, (%rdi)
  pause
  imul %rcx, %rax
  imul (%rdi), %r11d

  # __builtin_bswap*, __sync_synchronize and other instructions inline
  # asm uses
  bswap %eax
  bswap %rax
  bswap %r9d
  bswap %r12
  popcnt %ecx, %eax
  popcnt (%rdi), %r10
  popcntw %si, %dx
  lzcnt %rcx, %rax
  tzcnt %r8d, %eax
  mfence; lfence; sfence
  cpuid
  rdtsc
  rdtscp
  rdpmc
  xgetbv
  int3
  clflush (%rdi)
  fxsave (%rsp)
  fxrstor 16(%r9)

  # system instructions and port I/O, for kernels and drivers
  cli; sti
  clts; invd; wbinvd
  rdmsr; wrmsr
  swapgs
  iretq
  sysretq
  lgdt (%rax)
  lidt 8(%rdi)
  sgdt (%r10)
  sidt (%rsp)
  invlpg (%rdi)
  inb %dx, %al
  inw %dx, %ax
  inl %dx, %eax
  in $0x60, %al
  inl $0xcf, %eax
  outb %al, %dx
  outw %ax, %dx
  out %eax, %dx
  outb %al, $0x80
  outw %ax, $0x70

  # atomics and misc
  lock cmpxchg %edx, (%rdi)
  lock cmpxchg %rdx, (%rdi)
  xchg %rax, (%rdi)
  xchg %eax, (%rdi)
  rep stosb
  nop
  ud2
  syscall
  ret

  .local local_func
  .text
  .type local_func, @function
local_func:
.Llabel:
  ret

  # Jumps to symbols: to a hidden one in this section they may be short;
  # to any (not weak) one in this section they're filled in here; others
  # go through the PLT. And a size up to here.
  .globl hidden_near
  .hidden hidden_near
  .globl hidden_far
  .hidden hidden_far
  .globl global_func
  .weak weak_func
  .type sized_func, @function
  .type global_func,%function
sized_func:
  jmp hidden_near
  jz hidden_near
  jmp hidden_far
  jmp global_func
  jmp weak_func
  jne undefined_func
  jmp undefined_func@PLT
hidden_near:
  .zero 200
hidden_far:
global_func:
weak_func:
  ret
  .size sized_func, .-sized_func

  # data
  .globl global_var
  .data
  .type global_var, @object
  .size global_var, 16
  .align 8
global_var:
  .byte 1
  .byte -1
  .byte 255
  .byte 0
  .zero 4
  .quad global_var+8
.Ldata:
  .quad .Ldata-8
  .quad func+0
  .quad .Llabel+0
  .quad ext_var+16

  .local static_var
  .data
  .type static_var, @object
  .size static_var, 2
  .align 2
static_var:
  .byte 1
  .byte 2

  .globl zeroed
  .bss
  .align 16
zeroed:
  .zero 40

  .globl common_var
  .comm common_var, 8, 8
  .local local_common
  .comm local_common, 20, 4

  .globl v1
  .section .tdata,"awT",@progbits
  .type v1, @object
  .size v1, 4
  .align 4
v1:
  .byte 5
  .byte 0
  .byte 0
  .byte 0

  .globl v2
  .section .tbss,"awT",@nobits
  .align 8
v2:
  .zero 8

  # A file's bytes (this one's, from the top directory, where test/asm.sh
  # runs): all of it, then 16 bytes from offset 4.
  .section .rodata
  .incbin "test/asm-forms.s"
  .incbin "test/asm-forms.s", 4, 16

  # .init and .fini, named alone, hold code (musl's crti.s and crtn.s)
  .section .init
  ret
  .section .fini
  ret
