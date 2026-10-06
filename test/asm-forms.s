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
  shld %cl, %rax, %rdx
  shrd %cl, %rdx, %rax
  shld $5, %r8, %r9
  shrd $1, %edx, %eax
  shldq %cl, %rax, 8(%rsp)
  shrd %cl, %r10w, %r11w
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
  sqrtsd %xmm1, %xmm0
  sqrtss 8(%rsp), %xmm9
  minsd %xmm2, %xmm1
  maxsd (%rax), %xmm12
  minss %xmm3, %xmm4
  maxss %xmm15, %xmm0

  # SSE to SSE4.2, AES: register, high register and memory forms
  minpd %xmm1, %xmm2
  minpd %xmm9, %xmm14
  minpd 16(%r12), %xmm3
  minps %xmm1, %xmm2
  minps %xmm9, %xmm14
  minps 16(%r12), %xmm3
  maxpd %xmm1, %xmm2
  maxpd %xmm9, %xmm14
  maxpd 16(%r12), %xmm3
  maxps %xmm1, %xmm2
  maxps %xmm9, %xmm14
  maxps 16(%r12), %xmm3
  rcpps %xmm1, %xmm2
  rcpps %xmm9, %xmm14
  rcpps 16(%r12), %xmm3
  rcpss %xmm1, %xmm2
  rcpss %xmm9, %xmm14
  rcpss 16(%r12), %xmm3
  rsqrtps %xmm1, %xmm2
  rsqrtps %xmm9, %xmm14
  rsqrtps 16(%r12), %xmm3
  rsqrtss %xmm1, %xmm2
  rsqrtss %xmm9, %xmm14
  rsqrtss 16(%r12), %xmm3
  unpcklps %xmm1, %xmm2
  unpcklps %xmm9, %xmm14
  unpcklps 16(%r12), %xmm3
  unpckhps %xmm1, %xmm2
  unpckhps %xmm9, %xmm14
  unpckhps 16(%r12), %xmm3
  unpcklpd %xmm1, %xmm2
  unpcklpd %xmm9, %xmm14
  unpcklpd 16(%r12), %xmm3
  unpckhpd %xmm1, %xmm2
  unpckhpd %xmm9, %xmm14
  unpckhpd 16(%r12), %xmm3
  cvtps2pd %xmm1, %xmm2
  cvtps2pd %xmm9, %xmm14
  cvtps2pd 16(%r12), %xmm3
  cvtpd2ps %xmm1, %xmm2
  cvtpd2ps %xmm9, %xmm14
  cvtpd2ps 16(%r12), %xmm3
  cvtdq2ps %xmm1, %xmm2
  cvtdq2ps %xmm9, %xmm14
  cvtdq2ps 16(%r12), %xmm3
  cvtps2dq %xmm1, %xmm2
  cvtps2dq %xmm9, %xmm14
  cvtps2dq 16(%r12), %xmm3
  cvttps2dq %xmm1, %xmm2
  cvttps2dq %xmm9, %xmm14
  cvttps2dq 16(%r12), %xmm3
  cvtdq2pd %xmm1, %xmm2
  cvtdq2pd %xmm9, %xmm14
  cvtdq2pd 16(%r12), %xmm3
  cvtpd2dq %xmm1, %xmm2
  cvtpd2dq %xmm9, %xmm14
  cvtpd2dq 16(%r12), %xmm3
  cvttpd2dq %xmm1, %xmm2
  cvttpd2dq %xmm9, %xmm14
  cvttpd2dq 16(%r12), %xmm3
  punpcklbw %xmm1, %xmm2
  punpcklbw %xmm9, %xmm14
  punpcklbw 16(%r12), %xmm3
  punpcklwd %xmm1, %xmm2
  punpcklwd %xmm9, %xmm14
  punpcklwd 16(%r12), %xmm3
  punpckldq %xmm1, %xmm2
  punpckldq %xmm9, %xmm14
  punpckldq 16(%r12), %xmm3
  packsswb %xmm1, %xmm2
  packsswb %xmm9, %xmm14
  packsswb 16(%r12), %xmm3
  pcmpgtb %xmm1, %xmm2
  pcmpgtb %xmm9, %xmm14
  pcmpgtb 16(%r12), %xmm3
  pcmpgtw %xmm1, %xmm2
  pcmpgtw %xmm9, %xmm14
  pcmpgtw 16(%r12), %xmm3
  pcmpgtd %xmm1, %xmm2
  pcmpgtd %xmm9, %xmm14
  pcmpgtd 16(%r12), %xmm3
  packuswb %xmm1, %xmm2
  packuswb %xmm9, %xmm14
  packuswb 16(%r12), %xmm3
  punpckhbw %xmm1, %xmm2
  punpckhbw %xmm9, %xmm14
  punpckhbw 16(%r12), %xmm3
  punpckhwd %xmm1, %xmm2
  punpckhwd %xmm9, %xmm14
  punpckhwd 16(%r12), %xmm3
  punpckhdq %xmm1, %xmm2
  punpckhdq %xmm9, %xmm14
  punpckhdq 16(%r12), %xmm3
  packssdw %xmm1, %xmm2
  packssdw %xmm9, %xmm14
  packssdw 16(%r12), %xmm3
  punpcklqdq %xmm1, %xmm2
  punpcklqdq %xmm9, %xmm14
  punpcklqdq 16(%r12), %xmm3
  punpckhqdq %xmm1, %xmm2
  punpckhqdq %xmm9, %xmm14
  punpckhqdq 16(%r12), %xmm3
  pcmpeqb %xmm1, %xmm2
  pcmpeqb %xmm9, %xmm14
  pcmpeqb 16(%r12), %xmm3
  pcmpeqw %xmm1, %xmm2
  pcmpeqw %xmm9, %xmm14
  pcmpeqw 16(%r12), %xmm3
  pcmpeqd %xmm1, %xmm2
  pcmpeqd %xmm9, %xmm14
  pcmpeqd 16(%r12), %xmm3
  paddq %xmm1, %xmm2
  paddq %xmm9, %xmm14
  paddq 16(%r12), %xmm3
  pmullw %xmm1, %xmm2
  pmullw %xmm9, %xmm14
  pmullw 16(%r12), %xmm3
  psubusb %xmm1, %xmm2
  psubusb %xmm9, %xmm14
  psubusb 16(%r12), %xmm3
  psubusw %xmm1, %xmm2
  psubusw %xmm9, %xmm14
  psubusw 16(%r12), %xmm3
  pminub %xmm1, %xmm2
  pminub %xmm9, %xmm14
  pminub 16(%r12), %xmm3
  pand %xmm1, %xmm2
  pand %xmm9, %xmm14
  pand 16(%r12), %xmm3
  paddusb %xmm1, %xmm2
  paddusb %xmm9, %xmm14
  paddusb 16(%r12), %xmm3
  paddusw %xmm1, %xmm2
  paddusw %xmm9, %xmm14
  paddusw 16(%r12), %xmm3
  pmaxub %xmm1, %xmm2
  pmaxub %xmm9, %xmm14
  pmaxub 16(%r12), %xmm3
  pandn %xmm1, %xmm2
  pandn %xmm9, %xmm14
  pandn 16(%r12), %xmm3
  pavgb %xmm1, %xmm2
  pavgb %xmm9, %xmm14
  pavgb 16(%r12), %xmm3
  pavgw %xmm1, %xmm2
  pavgw %xmm9, %xmm14
  pavgw 16(%r12), %xmm3
  pmulhuw %xmm1, %xmm2
  pmulhuw %xmm9, %xmm14
  pmulhuw 16(%r12), %xmm3
  pmulhw %xmm1, %xmm2
  pmulhw %xmm9, %xmm14
  pmulhw 16(%r12), %xmm3
  psubsb %xmm1, %xmm2
  psubsb %xmm9, %xmm14
  psubsb 16(%r12), %xmm3
  psubsw %xmm1, %xmm2
  psubsw %xmm9, %xmm14
  psubsw 16(%r12), %xmm3
  pminsw %xmm1, %xmm2
  pminsw %xmm9, %xmm14
  pminsw 16(%r12), %xmm3
  por %xmm1, %xmm2
  por %xmm9, %xmm14
  por 16(%r12), %xmm3
  paddsb %xmm1, %xmm2
  paddsb %xmm9, %xmm14
  paddsb 16(%r12), %xmm3
  paddsw %xmm1, %xmm2
  paddsw %xmm9, %xmm14
  paddsw 16(%r12), %xmm3
  pmaxsw %xmm1, %xmm2
  pmaxsw %xmm9, %xmm14
  pmaxsw 16(%r12), %xmm3
  pmuludq %xmm1, %xmm2
  pmuludq %xmm9, %xmm14
  pmuludq 16(%r12), %xmm3
  pmaddwd %xmm1, %xmm2
  pmaddwd %xmm9, %xmm14
  pmaddwd 16(%r12), %xmm3
  psadbw %xmm1, %xmm2
  psadbw %xmm9, %xmm14
  psadbw 16(%r12), %xmm3
  psubb %xmm1, %xmm2
  psubb %xmm9, %xmm14
  psubb 16(%r12), %xmm3
  psubw %xmm1, %xmm2
  psubw %xmm9, %xmm14
  psubw 16(%r12), %xmm3
  psubd %xmm1, %xmm2
  psubd %xmm9, %xmm14
  psubd 16(%r12), %xmm3
  psubq %xmm1, %xmm2
  psubq %xmm9, %xmm14
  psubq 16(%r12), %xmm3
  paddb %xmm1, %xmm2
  paddb %xmm9, %xmm14
  paddb 16(%r12), %xmm3
  paddw %xmm1, %xmm2
  paddw %xmm9, %xmm14
  paddw 16(%r12), %xmm3
  paddd %xmm1, %xmm2
  paddd %xmm9, %xmm14
  paddd 16(%r12), %xmm3
  addsubps %xmm1, %xmm2
  addsubps %xmm9, %xmm14
  addsubps 16(%r12), %xmm3
  addsubpd %xmm1, %xmm2
  addsubpd %xmm9, %xmm14
  addsubpd 16(%r12), %xmm3
  haddps %xmm1, %xmm2
  haddps %xmm9, %xmm14
  haddps 16(%r12), %xmm3
  haddpd %xmm1, %xmm2
  haddpd %xmm9, %xmm14
  haddpd 16(%r12), %xmm3
  hsubps %xmm1, %xmm2
  hsubps %xmm9, %xmm14
  hsubps 16(%r12), %xmm3
  hsubpd %xmm1, %xmm2
  hsubpd %xmm9, %xmm14
  hsubpd 16(%r12), %xmm3
  movshdup %xmm1, %xmm2
  movshdup %xmm9, %xmm14
  movshdup 16(%r12), %xmm3
  movsldup %xmm1, %xmm2
  movsldup %xmm9, %xmm14
  movsldup 16(%r12), %xmm3
  movddup %xmm1, %xmm2
  movddup %xmm9, %xmm14
  movddup 16(%r12), %xmm3
  pshufb %xmm1, %xmm2
  pshufb %xmm9, %xmm14
  pshufb 16(%r12), %xmm3
  phaddw %xmm1, %xmm2
  phaddw %xmm9, %xmm14
  phaddw 16(%r12), %xmm3
  phaddd %xmm1, %xmm2
  phaddd %xmm9, %xmm14
  phaddd 16(%r12), %xmm3
  phaddsw %xmm1, %xmm2
  phaddsw %xmm9, %xmm14
  phaddsw 16(%r12), %xmm3
  pmaddubsw %xmm1, %xmm2
  pmaddubsw %xmm9, %xmm14
  pmaddubsw 16(%r12), %xmm3
  phsubw %xmm1, %xmm2
  phsubw %xmm9, %xmm14
  phsubw 16(%r12), %xmm3
  phsubd %xmm1, %xmm2
  phsubd %xmm9, %xmm14
  phsubd 16(%r12), %xmm3
  phsubsw %xmm1, %xmm2
  phsubsw %xmm9, %xmm14
  phsubsw 16(%r12), %xmm3
  psignb %xmm1, %xmm2
  psignb %xmm9, %xmm14
  psignb 16(%r12), %xmm3
  psignw %xmm1, %xmm2
  psignw %xmm9, %xmm14
  psignw 16(%r12), %xmm3
  psignd %xmm1, %xmm2
  psignd %xmm9, %xmm14
  psignd 16(%r12), %xmm3
  pmulhrsw %xmm1, %xmm2
  pmulhrsw %xmm9, %xmm14
  pmulhrsw 16(%r12), %xmm3
  pabsb %xmm1, %xmm2
  pabsb %xmm9, %xmm14
  pabsb 16(%r12), %xmm3
  pabsw %xmm1, %xmm2
  pabsw %xmm9, %xmm14
  pabsw 16(%r12), %xmm3
  pabsd %xmm1, %xmm2
  pabsd %xmm9, %xmm14
  pabsd 16(%r12), %xmm3
  pblendvb %xmm1, %xmm2
  pblendvb %xmm9, %xmm14
  pblendvb 16(%r12), %xmm3
  blendvps %xmm1, %xmm2
  blendvps %xmm9, %xmm14
  blendvps 16(%r12), %xmm3
  blendvpd %xmm1, %xmm2
  blendvpd %xmm9, %xmm14
  blendvpd 16(%r12), %xmm3
  ptest %xmm1, %xmm2
  ptest %xmm9, %xmm14
  ptest 16(%r12), %xmm3
  pmovsxbw %xmm1, %xmm2
  pmovsxbw %xmm9, %xmm14
  pmovsxbw 16(%r12), %xmm3
  pmovsxbd %xmm1, %xmm2
  pmovsxbd %xmm9, %xmm14
  pmovsxbd 16(%r12), %xmm3
  pmovsxbq %xmm1, %xmm2
  pmovsxbq %xmm9, %xmm14
  pmovsxbq 16(%r12), %xmm3
  pmovsxwd %xmm1, %xmm2
  pmovsxwd %xmm9, %xmm14
  pmovsxwd 16(%r12), %xmm3
  pmovsxwq %xmm1, %xmm2
  pmovsxwq %xmm9, %xmm14
  pmovsxwq 16(%r12), %xmm3
  pmovsxdq %xmm1, %xmm2
  pmovsxdq %xmm9, %xmm14
  pmovsxdq 16(%r12), %xmm3
  pmuldq %xmm1, %xmm2
  pmuldq %xmm9, %xmm14
  pmuldq 16(%r12), %xmm3
  pcmpeqq %xmm1, %xmm2
  pcmpeqq %xmm9, %xmm14
  pcmpeqq 16(%r12), %xmm3
  packusdw %xmm1, %xmm2
  packusdw %xmm9, %xmm14
  packusdw 16(%r12), %xmm3
  pmovzxbw %xmm1, %xmm2
  pmovzxbw %xmm9, %xmm14
  pmovzxbw 16(%r12), %xmm3
  pmovzxbd %xmm1, %xmm2
  pmovzxbd %xmm9, %xmm14
  pmovzxbd 16(%r12), %xmm3
  pmovzxbq %xmm1, %xmm2
  pmovzxbq %xmm9, %xmm14
  pmovzxbq 16(%r12), %xmm3
  pmovzxwd %xmm1, %xmm2
  pmovzxwd %xmm9, %xmm14
  pmovzxwd 16(%r12), %xmm3
  pmovzxwq %xmm1, %xmm2
  pmovzxwq %xmm9, %xmm14
  pmovzxwq 16(%r12), %xmm3
  pmovzxdq %xmm1, %xmm2
  pmovzxdq %xmm9, %xmm14
  pmovzxdq 16(%r12), %xmm3
  pminsb %xmm1, %xmm2
  pminsb %xmm9, %xmm14
  pminsb 16(%r12), %xmm3
  pminsd %xmm1, %xmm2
  pminsd %xmm9, %xmm14
  pminsd 16(%r12), %xmm3
  pminuw %xmm1, %xmm2
  pminuw %xmm9, %xmm14
  pminuw 16(%r12), %xmm3
  pminud %xmm1, %xmm2
  pminud %xmm9, %xmm14
  pminud 16(%r12), %xmm3
  pmaxsb %xmm1, %xmm2
  pmaxsb %xmm9, %xmm14
  pmaxsb 16(%r12), %xmm3
  pmaxsd %xmm1, %xmm2
  pmaxsd %xmm9, %xmm14
  pmaxsd 16(%r12), %xmm3
  pmaxuw %xmm1, %xmm2
  pmaxuw %xmm9, %xmm14
  pmaxuw 16(%r12), %xmm3
  pmaxud %xmm1, %xmm2
  pmaxud %xmm9, %xmm14
  pmaxud 16(%r12), %xmm3
  pmulld %xmm1, %xmm2
  pmulld %xmm9, %xmm14
  pmulld 16(%r12), %xmm3
  phminposuw %xmm1, %xmm2
  phminposuw %xmm9, %xmm14
  phminposuw 16(%r12), %xmm3
  pcmpgtq %xmm1, %xmm2
  pcmpgtq %xmm9, %xmm14
  pcmpgtq 16(%r12), %xmm3
  aesimc %xmm1, %xmm2
  aesimc %xmm9, %xmm14
  aesimc 16(%r12), %xmm3
  aesenc %xmm1, %xmm2
  aesenc %xmm9, %xmm14
  aesenc 16(%r12), %xmm3
  aesenclast %xmm1, %xmm2
  aesenclast %xmm9, %xmm14
  aesenclast 16(%r12), %xmm3
  aesdec %xmm1, %xmm2
  aesdec %xmm9, %xmm14
  aesdec 16(%r12), %xmm3
  aesdeclast %xmm1, %xmm2
  aesdeclast %xmm9, %xmm14
  aesdeclast 16(%r12), %xmm3
  pshufd $3, %xmm1, %xmm2
  pshufd $27, %xmm10, %xmm4
  pshufd $1, -32(%rbp), %xmm11
  pshufhw $3, %xmm1, %xmm2
  pshufhw $27, %xmm10, %xmm4
  pshufhw $1, -32(%rbp), %xmm11
  pshuflw $3, %xmm1, %xmm2
  pshuflw $27, %xmm10, %xmm4
  pshuflw $1, -32(%rbp), %xmm11
  shufps $3, %xmm1, %xmm2
  shufps $27, %xmm10, %xmm4
  shufps $1, -32(%rbp), %xmm11
  shufpd $3, %xmm1, %xmm2
  shufpd $27, %xmm10, %xmm4
  shufpd $1, -32(%rbp), %xmm11
  cmpps $3, %xmm1, %xmm2
  cmpps $27, %xmm10, %xmm4
  cmpps $1, -32(%rbp), %xmm11
  cmppd $3, %xmm1, %xmm2
  cmppd $27, %xmm10, %xmm4
  cmppd $1, -32(%rbp), %xmm11
  cmpss $3, %xmm1, %xmm2
  cmpss $27, %xmm10, %xmm4
  cmpss $1, -32(%rbp), %xmm11
  cmpsd $3, %xmm1, %xmm2
  cmpsd $27, %xmm10, %xmm4
  cmpsd $1, -32(%rbp), %xmm11
  palignr $3, %xmm1, %xmm2
  palignr $27, %xmm10, %xmm4
  palignr $1, -32(%rbp), %xmm11
  roundps $3, %xmm1, %xmm2
  roundps $27, %xmm10, %xmm4
  roundps $1, -32(%rbp), %xmm11
  roundpd $3, %xmm1, %xmm2
  roundpd $27, %xmm10, %xmm4
  roundpd $1, -32(%rbp), %xmm11
  roundss $3, %xmm1, %xmm2
  roundss $27, %xmm10, %xmm4
  roundss $1, -32(%rbp), %xmm11
  roundsd $3, %xmm1, %xmm2
  roundsd $27, %xmm10, %xmm4
  roundsd $1, -32(%rbp), %xmm11
  blendps $3, %xmm1, %xmm2
  blendps $27, %xmm10, %xmm4
  blendps $1, -32(%rbp), %xmm11
  blendpd $3, %xmm1, %xmm2
  blendpd $27, %xmm10, %xmm4
  blendpd $1, -32(%rbp), %xmm11
  pblendw $3, %xmm1, %xmm2
  pblendw $27, %xmm10, %xmm4
  pblendw $1, -32(%rbp), %xmm11
  insertps $3, %xmm1, %xmm2
  insertps $27, %xmm10, %xmm4
  insertps $1, -32(%rbp), %xmm11
  dpps $3, %xmm1, %xmm2
  dpps $27, %xmm10, %xmm4
  dpps $1, -32(%rbp), %xmm11
  dppd $3, %xmm1, %xmm2
  dppd $27, %xmm10, %xmm4
  dppd $1, -32(%rbp), %xmm11
  mpsadbw $3, %xmm1, %xmm2
  mpsadbw $27, %xmm10, %xmm4
  mpsadbw $1, -32(%rbp), %xmm11
  pcmpestrm $3, %xmm1, %xmm2
  pcmpestrm $27, %xmm10, %xmm4
  pcmpestrm $1, -32(%rbp), %xmm11
  pcmpestri $3, %xmm1, %xmm2
  pcmpestri $27, %xmm10, %xmm4
  pcmpestri $1, -32(%rbp), %xmm11
  pcmpistrm $3, %xmm1, %xmm2
  pcmpistrm $27, %xmm10, %xmm4
  pcmpistrm $1, -32(%rbp), %xmm11
  pcmpistri $3, %xmm1, %xmm2
  pcmpistri $27, %xmm10, %xmm4
  pcmpistri $1, -32(%rbp), %xmm11
  aeskeygenassist $3, %xmm1, %xmm2
  aeskeygenassist $27, %xmm10, %xmm4
  aeskeygenassist $1, -32(%rbp), %xmm11
  pclmulqdq $3, %xmm1, %xmm2
  pclmulqdq $27, %xmm10, %xmm4
  pclmulqdq $1, -32(%rbp), %xmm11
  lddqu (%rax), %xmm3
  movntdqa 8(%r13), %xmm12
  movhlps %xmm1, %xmm2
  movlhps %xmm8, %xmm2
  maskmovdqu %xmm1, %xmm9
  pblendvb %xmm0, %xmm1, %xmm2
  blendvps %xmm0, (%rax), %xmm9
  movups %xmm1, %xmm2
  movups %xmm13, %xmm2
  movups (%rax), %xmm3
  movups %xmm10, 32(%r8)
  movupd %xmm1, %xmm2
  movupd %xmm13, %xmm2
  movupd (%rax), %xmm3
  movupd %xmm10, 32(%r8)
  movaps %xmm1, %xmm2
  movaps %xmm13, %xmm2
  movaps (%rax), %xmm3
  movaps %xmm10, 32(%r8)
  movapd %xmm1, %xmm2
  movapd %xmm13, %xmm2
  movapd (%rax), %xmm3
  movapd %xmm10, 32(%r8)
  movdqu %xmm1, %xmm2
  movdqu %xmm13, %xmm2
  movdqu (%rax), %xmm3
  movdqu %xmm10, 32(%r8)
  movdqa %xmm1, %xmm2
  movdqa %xmm13, %xmm2
  movdqa (%rax), %xmm3
  movdqa %xmm10, 32(%r8)
  movlps (%rax), %xmm3
  movlps %xmm10, 32(%r8)
  movhps (%rax), %xmm3
  movhps %xmm10, 32(%r8)
  movlpd (%rax), %xmm3
  movlpd %xmm10, 32(%r8)
  movhpd (%rax), %xmm3
  movhpd %xmm10, 32(%r8)
  movntps %xmm1, (%rdi)
  movntps %xmm9, 64(%r9)
  movntpd %xmm1, (%rdi)
  movntpd %xmm9, 64(%r9)
  movntdq %xmm1, (%rdi)
  movntdq %xmm9, 64(%r9)
  cmpeqps %xmm1, %xmm8
  cmpeqpd %xmm1, %xmm2
  cmpeqss %xmm1, %xmm8
  cmpeqsd %xmm1, %xmm2
  cmpltps %xmm1, %xmm8
  cmpltpd %xmm1, %xmm2
  cmpltss %xmm1, %xmm8
  cmpltsd %xmm1, %xmm2
  cmpleps %xmm1, %xmm8
  cmplepd %xmm1, %xmm2
  cmpless %xmm1, %xmm8
  cmplesd %xmm1, %xmm2
  cmpunordps %xmm1, %xmm8
  cmpunordpd %xmm1, %xmm2
  cmpunordss %xmm1, %xmm8
  cmpunordsd %xmm1, %xmm2
  cmpneqps %xmm1, %xmm8
  cmpneqpd %xmm1, %xmm2
  cmpneqss %xmm1, %xmm8
  cmpneqsd %xmm1, %xmm2
  cmpnltps %xmm1, %xmm8
  cmpnltpd %xmm1, %xmm2
  cmpnltss %xmm1, %xmm8
  cmpnltsd %xmm1, %xmm2
  cmpnleps %xmm1, %xmm8
  cmpnlepd %xmm1, %xmm2
  cmpnless %xmm1, %xmm8
  cmpnlesd %xmm1, %xmm2
  cmpordps %xmm1, %xmm8
  cmpordpd %xmm1, %xmm2
  cmpordss %xmm1, %xmm8
  cmpordsd %xmm1, %xmm2
  psllw %xmm1, %xmm2
  psllw (%rax), %xmm12
  psllw $3, %xmm1
  psllw $7, %xmm15
  pslld %xmm1, %xmm2
  pslld (%rax), %xmm12
  pslld $3, %xmm1
  pslld $7, %xmm15
  psllq %xmm1, %xmm2
  psllq (%rax), %xmm12
  psllq $3, %xmm1
  psllq $7, %xmm15
  psrlw %xmm1, %xmm2
  psrlw (%rax), %xmm12
  psrlw $3, %xmm1
  psrlw $7, %xmm15
  psrld %xmm1, %xmm2
  psrld (%rax), %xmm12
  psrld $3, %xmm1
  psrld $7, %xmm15
  psrlq %xmm1, %xmm2
  psrlq (%rax), %xmm12
  psrlq $3, %xmm1
  psrlq $7, %xmm15
  psraw %xmm1, %xmm2
  psraw (%rax), %xmm12
  psraw $3, %xmm1
  psraw $7, %xmm15
  psrad %xmm1, %xmm2
  psrad (%rax), %xmm12
  psrad $3, %xmm1
  psrad $7, %xmm15
  pslldq $4, %xmm1
  psrldq $8, %xmm10
  pmovmskb %xmm1, %eax
  pmovmskb %xmm9, %r10d
  movmskps %xmm2, %ecx
  movmskpd %xmm12, %r11d
  pextrw $3, %xmm1, %eax
  pextrw $7, %xmm11, %r9d
  pinsrw $2, %eax, %xmm1
  pinsrw $1, (%rdi), %xmm9
  pinsrw $5, %r10d, %xmm3
  pinsrb $2, %eax, %xmm1
  pinsrb $15, (%rax), %xmm9
  pinsrd $1, %r11d, %xmm2
  pinsrq $1, %rax, %xmm1
  pinsrq $0, %r9, %xmm10
  pinsrq $1, 8(%rsp), %xmm4
  pextrb $3, %xmm1, %eax
  pextrb $1, %xmm9, (%rdi)
  pextrd $2, %xmm2, %r10d
  pextrq $1, %xmm10, %rax
  pextrq $0, %xmm1, 8(%r12)
  extractps $1, %xmm3, %ecx
  movd %eax, %xmm1
  movd %r10d, %xmm9
  movd (%rax), %xmm2
  movd %xmm1, %eax
  movd %xmm12, %r11d
  movd %xmm3, -16(%rsp)
  movd %rax, %xmm1
  movq (%rax), %xmm1
  movq -8(%rsp), %xmm10
  movq %xmm2, %xmm3
  movq %xmm9, (%rdi)
  movq %xmm1, 8(%r12)
  cvtss2si %xmm1, %eax
  cvtss2si (%rax), %r10
  cvtsd2si %xmm9, %rcx
  cvtsd2sil %xmm1, %eax
  cvtss2siq %xmm1, %rax
  crc32b %al, %eax
  crc32b (%rdi), %r9d
  crc32w %ax, %ecx
  crc32l %eax, %edx
  crc32q %rax, %rcx
  crc32 %sil, %eax
  crc32 %r10, %r11
  emms
  prefetchnta (%rax)
  prefetcht0 8(%rdi)
  prefetcht1 (%r12)
  prefetcht2 -8(%rbp)
  prefetchw (%rax)
  addpd %xmm1, %xmm2
  mulpd %xmm1, %xmm2
  subpd %xmm1, %xmm2
  divpd %xmm1, %xmm2
  addps %xmm1, %xmm2
  mulps %xmm1, %xmm2
  subps %xmm1, %xmm2
  divps (%rdi), %xmm2
  sqrtpd %xmm1, %xmm2
  sqrtps %xmm10, %xmm2
  andpd %xmm1, %xmm2
  andps %xmm1, %xmm2
  andnpd %xmm1, %xmm2
  andnps %xmm1, %xmm2
  orpd %xmm1, %xmm2
  orps -16(%rbp), %xmm2
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

  # hand-written assembly (.S): differences of labels, with a jump
  # between them, strings and lists
.Lmsg_start:
  mov $(.Lmsg_end - .Lmsg_start), %edx
  mov $.Lmsg_end - .Lmsg_start + 4, %ecx
  push $(.Lmsg_end - .Lmsg_start)
  jne .Lmsg_end
  .ascii "hi\n\t\"\\\101\x42", "two"
  .asciz "nul"
  .string "str"
  .byte 1, 2, 0xff, -1
  .short 1, -2
  .long 3, .Lmsg_end - .Lmsg_start
  .quad .Lmsg_start - .Lmsg_end, 5
.Lmsg_end:
  .byte .Lmsg_end - .Lmsg_start

  # atomics and misc
  lock cmpxchg %edx, (%rdi)
  lock cmpxchg %rdx, (%rdi)
  lock cmpxchg16b (%rdi)
  cmpxchg16b 8(%r8)
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
