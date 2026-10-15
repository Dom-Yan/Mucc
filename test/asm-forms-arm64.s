// Every ARM64 instruction form src/asm.c knows, for test/asm.sh, which
// checks that it assembles them as GNU as does. cgen_arm64.c emits most;
// the rest are in musl's assembly and in asm statements.

  .text
  .globl forms
  .type forms, %function
forms:
# a # comment starts a line, even after the first
  # and after spaces
  // add and sub: immediates (12 bits, or shifted by 12), shifted
  // registers, extended registers (with sp), and their aliases
  add x0, x1, 5
  add x0, x1, #4095
  add w0, w1, #1, lsl #12
  add x0, x1, 0x7ff000
  add x0, x1, -8
  sub sp, sp, 32
  sub x0, sp, 4095, lsl 12
  adds x0, x1, 7
  subs w2, w3, #0xfff
  add x0, x1, x2
  add w0, w1, w2, lsl #3
  sub x0, x1, x2, asr 63
  subs x0, x1, x2, lsr #1
  add sp, sp, x17
  sub sp, sp, x0
  add x0, sp, x1
  add x0, x1, w2, uxtw
  add x0, x1, w2, sxtw #2
  cmp x0, 5
  cmp w0, w1
  cmp x0, x1, lsl 2
  cmn w0, #1
  cmn x0, x1
  neg x0, x1
  negs w0, w1
  neg x0, x1, lsl #4
  mov x29, sp
  mov sp, x29
  mov x0, x1
  mov w0, w1
  mov w0, wzr

  // Logical: bitmask immediates, shifted registers, aliases
  and x10, x10, -16
  and w0, w0, #0xc00000
  bic w1, w1, #0xc00000
  orr x0, x0, 0x5555555555555555
  eor w0, w1, 0xff
  ands x0, x1, 0xf
  and x9, x0, 0x3333333333333333
  and sp, x9, -64
  and x0, x1, x2
  orr w0, w1, w2, lsl 8
  eor x0, x1, x2, ror #3
  bic x0, x1, x2
  orn w0, w1, w2
  eon x0, x1, x2, lsr 4
  ands x0, x1, x2
  bics w0, w1, w2
  orr x15, x14, x15, lsl 8
  tst x2, 64
  tst w0, w1
  mvn x0, x1
  mvn w0, w1, lsl 2

  // mov of numbers: movz, movn, or a bitmask's orr; and movz, movn, movk
  mov x0, 0
  mov x0, 65535
  mov w0, -1
  mov x0, -5
  mov x0, 0x10000
  mov x0, 0xffff0000
  mov w0, 0x80000000
  mov x0, 0x0101010101010101
  mov x9, 0x5555555555555555
  movz x0, 16384, lsl 48
  movz w0, #0xabcd, lsl #16
  movk x0, 0x1234, lsl 32
  movn x1, 0
  movk w2, 1

  // Shifts by registers and constants, bit-fields, extends, extr
  lsl x0, x0, x1
  lsr w0, w0, w1
  asr x1, x1, x2
  ror w0, w1, w2
  lsl x0, x0, 3
  lsl w0, w1, 31
  lsr x0, x1, 63
  asr w0, w0, 4
  ror x0, x1, 17
  sbfx x0, x0, 3, 5
  ubfx w0, w13, 0, 7
  ubfx x0, x13, 1, 63
  bfxil x0, x1, 4, 8
  sbfiz x0, x1, 2, 10
  ubfiz w0, w1, 5, 3
  bfi x0, x1, 16, 16
  sbfm x0, x1, 3, 7
  ubfm w0, w1, 31, 30
  bfm x2, x3, 0, 63
  sxtb w0, w0
  sxtb x12, w0
  sxth w0, w0
  sxth x12, w0
  sxtw x0, w0
  uxtb w0, w0
  uxth w12, w0
  uxtw x0, w2
  extr x1, x1, x0, 60
  extr w0, w1, w2, 5

  // Data processing on registers
  rbit w0, w0
  rbit x0, x0
  rev w0, w0
  rev x0, x0
  rev16 w0, w0
  rev32 x0, x0
  clz w0, w0
  clz x0, x0
  cls x1, x2
  udiv w0, w0, w1
  sdiv x2, x0, x1
  lslv x0, x1, x2
  asrv w0, w1, w2
  madd x4, x0, x3, x4
  msub w0, w2, w1, w0
  msub x0, x2, x1, x0
  smaddl x0, w1, w2, x3
  umsubl x0, w1, w2, x3
  mul w0, w0, w1
  mul x0, x10, x11
  mneg x0, x1, x2
  smull x0, w1, w2
  umull x0, w1, w2
  smulh x0, x1, x2
  umulh x1, x10, x11
  adc x1, x1, x3
  adcs w0, w1, w2
  sbc x1, x12, x13
  sbcs xzr, x1, x3
  ngc x1, x1
  ngcs w0, w1

  // Conditional select, set and compare
  csel x12, x11, xzr, lt
  csinc w0, w1, w2, ne
  csinv x0, x1, x2, hs
  csneg w0, w1, w2, cc
  cset w0, eq
  cset x0, ls
  csetm x0, ne
  csetm w0, mi
  cinc x0, x1, gt
  cinv w0, w1, le
  cneg x0, x1, vs
  ccmp x0, x1, 4, eq
  ccmp w0, 3, 0, ne
  ccmn x0, 1, 2, ge

  // Loads and stores: unsigned and unscaled offsets, pre- and
  // post-index, register offsets, every size and kind
  ldr x0, [x29, 16]
  ldr x0, [x29, #32760]
  ldr w0, [x0]
  ldr x0, [x29, 3]
  ldr x0, [x29, -8]
  ldr w1, [x29, 4100]
  ldrb w0, [x0, 1]
  ldrsb w0, [x29, 4095]
  ldrsb x0, [sp, 1]
  ldrh w0, [x0, 2]
  ldrsh w0, [x1, -2]
  ldrsh x0, [x1, 6]
  ldrsw x0, [x9, 28]
  ldr s0, [x29, 16]
  ldr d0, [x29, 24]
  ldr q0, [x29, 32]
  ldr q1, [sp, 16]
  ldr b0, [x0]
  ldr h0, [x0, 2]
  str x0, [sp, -16]!
  ldr x1, [sp], 16
  str q0, [sp, -16]!
  ldr q1, [sp], 16
  ldr x13, [x11], 8
  str x13, [x12], 8
  strb w0, [x9]
  strb w0, [x9, 4095]
  strh w0, [x9, -2]
  str w9, [x29, 48]
  str s0, [x29, 4]
  str d1, [x29, 8]
  str q7, [x29, 64]
  strb wzr, [x10, 3]
  str xzr, [x10, 8]
  ldr x10, [x0, x11]
  str x10, [x1, x11]
  ldr x1, [x1, x0, lsl 3]
  ldr w0, [x1, w2, uxtw 2]
  ldrb w0, [x1, x2]
  ldrh w0, [x1, x2, lsl 1]
  ldr d0, [x1, w2, sxtw 3]
  ldur x0, [x1, 8]
  stur w0, [x1, -4]
  ldurb w0, [x1, 1]
  ldursw x0, [x1, -4]

  // Pairs
  stp x29, x30, [sp, -32]!
  stp x29, x30, [sp, -512]!
  ldp x29, x30, [sp], 32
  ldp x29, x30, [sp], 504
  stp x29, x30, [sp]
  ldp x29, x30, [sp]
  stp x0, x1, [sp, -16]!
  ldp x2, x3, [sp], 16
  stp x19, x20, [x0, 0]
  stp d8, d9, [x0, 112]
  stp w1, w2, [x0]
  ldp w1, w2, [x0]
  ldp q0, q1, [sp, 32]
  ldp x13, x14, [x9]
  stp x15, x17, [x9]
  ldpsw x0, x1, [x2, 8]

  // Exclusive, acquire and release
  ldxr w0, [x1]
  ldaxr w15, [x12]
  ldaxr x15, [x12]
  ldaxrb w15, [x12]
  ldaxrh w15, [x12]
  stxr w8, w0, [x1]
  stlxr w8, x10, [x12]
  stlxrb w8, w10, [x12]
  stlxrh w8, w10, [x12]
  ldar x0, [x1]
  ldarb w0, [x1]
  stlr w0, [x1]
  stlrh w0, [x1]
  ldxp x0, x1, [x2]
  ldaxp x15, x17, [x12]
  stxp w8, x0, x1, [x2]
  stlxp w8, x10, x11, [x12]

  // Branches, to labels near and far (see below), and through registers
1:
  b 1b
  b.eq 1b
  b.ne 2f
  b.lt 2f
  b.hs 2f
  b.cc 2f
  cbz w0, 1b
  cbnz x0, 2f
  tbz x2, 63, 1b
  tbnz w0, 3, 2f
  bl forms
  bl ext_fn
  b ext_fn
2:
  br x0
  blr x15
  ret
  ret x1
  adr x0, 1b
  adr x0, forms

  // Addresses of symbols, and relocation operators
  adrp x0, ext_var
  add x0, x0, :lo12:ext_var
  add x0, x0, #:lo12:ext_var
  ldr x0, [x0, :lo12:ext_var]
  ldr w1, [x1, #:lo12:ext_var]
  ldrb w1, [x1, :lo12:ext_var]
  ldr q1, [x1, :lo12:ext_var]
  adrp x0, :got:ext_var
  ldr x0, [x0, :got_lo12:ext_var]
  mrs x0, tpidr_el0
  add x0, x0, :tprel_hi12:tls_var, lsl 12
  add x0, x0, :tprel_lo12_nc:tls_var
  adrp x0, :tlsdesc:tls_var
  ldr x1, [x0, :tlsdesc_lo12:tls_var]
  add x0, x0, :tlsdesc_lo12:tls_var
  .tlsdesccall tls_var
  blr x1
  adrp x0, :gottprel:tls_var
  ldr x0, [x0, :gottprel_lo12:tls_var]
  movz x0, :abs_g1:ext_var
  movk x0, :abs_g0_nc:ext_var
  ldr x0, data
  ldr w0, data
  ldrsw x0, data
  ldr d0, data

  // System
  svc 0
  svc #0
  brk 1000
  nop
  yield
  dmb ish
  dmb ishld
  dmb sy
  dsb ish
  isb
  clrex
  mrs x1, fpcr
  mrs x2, fpsr
  msr fpcr, x1
  msr fpsr, x2
  mrs x0, nzcv
  mrs x0, cntvct_el0

  // Floating point
  fmov s0, w0
  fmov w0, s0
  fmov d0, x0
  fmov x0, d0
  fmov d1, xzr
  fmov x0, v0.d[1]
  fmov v0.d[1], x0
  fmov s0, s1
  fmov d0, d1
  fmov d0, 1.0
  fmov s0, -0.5
  fmov d1, #2.5
  fadd s0, s0, s1
  fadd d0, d0, d1
  fsub d0, d0, d1
  fmul s0, s0, s1
  fdiv d0, d0, d1
  fmax d0, d1, d2
  fminnm s0, s1, s2
  fnmul d0, d1, d2
  fneg s0, s0
  fneg d0, d0
  fabs d0, d1
  fsqrt d0, d1
  frintm d0, d1
  frintx s0, s1
  frinta d0, d1
  fmadd d0, d1, d2, d3
  fnmsub s0, s1, s2, s3
  fcmp s0, 0.0
  fcmp d0, #0.0
  fcmp s0, s1
  fcmpe d0, d1
  fcvt d0, s0
  fcvt s0, d0
  scvtf s0, w0
  scvtf d0, x0
  ucvtf s0, x0
  ucvtf d0, w0
  fcvtzs w0, s0
  fcvtzs x0, d0
  fcvtzu w0, d0
  fcvtzu x0, s0
  fcvtns x0, d0
  fcvtas w0, s0
  fcvtms x0, d1
  fcsel d0, d1, d2, gt

  // Vector elements, and vectors
  mov v0.d[1], x1
  mov x0, v0.d[0]
  mov x1, v0.d[1]
  ins v0.s[1], w0
  umov w0, v1.s[2]
  umov x0, v1.d[1]
  umov w0, v1.b[15]
  add v0.4s, v0.4s, v1.4s
  sub v0.2d, v1.2d, v2.2d
  mul v0.8h, v1.8h, v2.8h
  and v0.16b, v1.16b, v2.16b
  orr v0.8b, v1.8b, v2.8b
  fadd v0.4s, v1.4s, v2.4s
  fmul v0.2d, v1.2d, v2.2d
  fsqrt v0.4s, v1.4s
  fneg v0.2s, v1.2s

  .data
data:
  .quad 1
  .xword forms
  .word 3, ext_var
  .hword 4
  .short 5
  .dword 6
  .inst 0xd503201f
  .p2align 3
  .align 3
  .balign 8
