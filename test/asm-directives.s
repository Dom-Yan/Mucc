# GNU as directives beyond those mucc's own output uses, which hand-written
# assembly does: test/asm.sh checks that mucc's assembler makes the same
# object as GNU as from this. Code is aligned with an explicit fill byte:
# the NOPs GNU as pads code with differ between its versions, so test/asm.sh
# checks mucc's NOP padding on its own.

  .set COUNT, 3
  .equ SIZE, COUNT * 0 + 8
  .set ALIAS, func
  .globl GCONST
  .set GCONST, 0x1234

  .macro save reg, off=0
  mov \reg, \off(%rsp)
  .endm

  .macro both a, b:vararg
  .byte \a
  .byte \b
  .endm

  .macro label_n n
lbl\@_\n\():
  .byte \n
  .endm

  .text
  .globl func
func:
  int3
  .p2align 4,0xcc
  mov $COUNT, %eax
  mov $SIZE + 1, %ecx
  save %rax
  save %rdi, 8
  save off=16, reg=%rsi
  .balign 32,0xcc
  .rept COUNT
  nop
  .endr
  .p2align 3,0xcc
  .p2align 6,0xcc,7
  ret
  .align 64,0xcc
  push %rbp
  .p2align 7,0xcc
  ret

  .data
  .int 1, -2
  .word 0x1234, 7
  .float 1.5, -0.1
  .double 3.141592653589793, 1e-300
  .skip 3
  .space 2, 0x41
  .fill 3, 2, 0x55
  .fill 2, 8, 0x123456789
  .fill 4
  .balign 16, 0x77
  .byte COUNT, SIZE - 1, -COUNT
  .quad GCONST
  both 1, 2, 3
  label_n 5
  label_n 6
  .rept 2
  .rept 2
  .byte 9
  .endr
  .byte 8
  .endr

  .pushsection .rodata.str1.1,"aMS",@progbits,1
str:
  .string "merged"
  .pushsection .rodata.cst8,"aM",@progbits,8
  .quad 42
  .popsection
  .string "more"
  .popsection
  .quad str

  .section .bss
  .skip 5
  .p2align 4
  .space 16

  .ident "mucc test"
  .section .note.GNU-stack,"",@progbits
