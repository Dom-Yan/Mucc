//============================================================================
// cgen.c - STAGE 4 of 6: CODEGEN
//
// Walks the AST and prints x86-64 assembly (AT&T syntax). It is a
// simple stack machine: each expression leaves its result in %rax.
// Calls follow the System V AMD64 ABI, so mucc links with gcc/glibc.
//============================================================================

#include "mucc.h"

//---------- Codegen state ---------------------------------------------------

#define GP_MAX 6
#define FP_MAX 8

// The assembly text, written to the output file in one go at the end.
static char *out_buf;
static size_t out_len;
static size_t out_cap;

static int depth;

// Whether the program has asm("...") statements, whose instructions the
// built-in assembler may not know (see cc1() in main.c).
bool has_inline_asm;

// Callee-saved registers, which calls leave alone: local variables can
// live in them (see "Register variables"). Obj->reg is an index + 1.
#define NREGS 5
static char *regs64[] = {"%rbx", "%r12", "%r13", "%r14", "%r15"};
static char *regs32[] = {"%ebx", "%r12d", "%r13d", "%r14d", "%r15d"};
static char *regs16[] = {"%bx", "%r12w", "%r13w", "%r14w", "%r15w"};
static char *regs8[] = {"%bl", "%r12b", "%r13b", "%r14b", "%r15b"};

static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
static char *argreg16[] = {"%di", "%si", "%dx", "%cx", "%r8w", "%r9w"};
static char *argreg32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
static char *argreg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static Obj *current_fn;

// The source position of the last .loc emitted (see emit_loc).
static int loc_file;
static int loc_line;

static void gen_expr(Node *node);
static void gen_discard(Node *node);
static void gen_stmt(Node *node);
static bool block_begin(Node *node);
static void block_end(Node *node);
static void cast_int128(Type *from, Type *to);
static void cast_vector(Type *from, Type *to);
static char *high_half(char *addr);
static char *vec_move(int size);

//---------- Output and stack helpers ----------------------------------------

// Every line of assembly goes through println(). It used to call glibc's
// vfprintf, which was about 30% of all compile time, so it has its own
// small formatter that knows only what this file uses: %s, %d, %u, %ld,
// %lu, %+ld, %Lf and %%. Anything else is an internal error.

static void out_bytes(char *s, size_t n) {
  if (out_len + n > out_cap) {
    out_cap = MAX(out_cap * 2, out_len + n + (1 << 20));
    out_buf = realloc(out_buf, out_cap);
    if (!out_buf)
      error("out of memory");
  }
  memcpy(out_buf + out_len, s, n);
  out_len += n;
}

static void out_ulong(unsigned long v) {
  char buf[20];
  int i = sizeof(buf);
  do {
    buf[--i] = '0' + v % 10;
    v /= 10;
  } while (v);
  out_bytes(buf + i, sizeof(buf) - i);
}

static void out_long(long v) {
  if (v < 0) {
    out_bytes("-", 1);
    out_ulong(-(unsigned long)v);
  } else {
    out_ulong(v);
  }
}

// A `push %rax` not written yet. If the next instruction is a pop, the
// pair becomes one mov (see pop()); anything else writes the push first.
static bool pending_push;

static void flush_push(void) {
  if (pending_push) {
    char *line = "  push %rax\n";
    pending_push = false;
    out_bytes(line, strlen(line));
  }
}

__attribute__((format(printf, 1, 2)))
static void println(char *fmt, ...) {
  flush_push();
  va_list ap;
  va_start(ap, fmt);

  for (char *p = fmt; *p;) {
    // Copy the plain text up to the next '%' in one piece.
    char *pct = strchr(p, '%');
    if (!pct) {
      out_bytes(p, strlen(p));
      break;
    }
    out_bytes(p, pct - p);
    p = pct + 1;

    if (*p == '%') {
      out_bytes("%", 1);
      p++;
    } else if (*p == 's') {
      char *s = va_arg(ap, char *);
      out_bytes(s, strlen(s));
      p++;
    } else if (*p == 'd') {
      out_long(va_arg(ap, int));
      p++;
    } else if (*p == 'u') {
      out_ulong(va_arg(ap, unsigned));
      p++;
    } else if (p[0] == 'l' && p[1] == 'd') {
      out_long(va_arg(ap, long));
      p += 2;
    } else if (p[0] == 'l' && p[1] == 'u') {
      out_ulong(va_arg(ap, unsigned long));
      p += 2;
    } else if (p[0] == '+' && p[1] == 'l' && p[2] == 'd') {
      long v = va_arg(ap, long);
      if (v >= 0)
        out_bytes("+", 1);
      out_long(v);
      p += 3;
    } else if (p[0] == 'L' && p[1] == 'f') {
      char *s = format("%Lf", va_arg(ap, long double));
      out_bytes(s, strlen(s));
      p += 2;
    } else {
      unreachable();
    }
  }

  va_end(ap);
  out_bytes("\n", 1);
}

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  flush_push();
  pending_push = true;
  depth++;
}

static void pop(char *arg) {
  if (pending_push) {
    // `push %rax; pop %rdi` is `mov %rax, %rdi`.
    pending_push = false;
    if (strcmp(arg, "%rax"))
      println("  mov %%rax, %s", arg);
  } else {
    println("  pop %s", arg);
  }
  depth--;
}

static void pushf(void) {
  println("  sub $8, %%rsp");
  println("  movsd %%xmm0, (%%rsp)");
  depth++;
}

static void popf(int reg) {
  println("  movsd (%%rsp), %%xmm%d", reg);
  println("  add $8, %%rsp");
  depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

static char *reg_dx(int sz) {
  switch (sz) {
  case 1: return "%dl";
  case 2: return "%dx";
  case 4: return "%edx";
  case 8: return "%rdx";
  }
  unreachable();
}

static char *reg_ax(int sz) {
  switch (sz) {
  case 1: return "%al";
  case 2: return "%ax";
  case 4: return "%eax";
  case 8: return "%rax";
  }
  unreachable();
}

//---------- Addresses, loads and stores -------------------------------------

// Puts the address of local `var` in `reg`. One aligned above 16 is away
// from the frame, and its slot holds its address (see
// assign_lvar_offsets()).
static void addr_of_local(Obj *var, char *reg) {
  if (var->is_overaligned)
    println("  mov %d(%%rbp), %s", var->offset, reg);
  else
    println("  lea %d(%%rbp), %s", var->offset, reg);
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    // A register variable has no address; one whose address is taken
    // is never put in a register.
    if (node->var->reg)
      unreachable();

    // Variable-length array, which is always local.
    if (node->var->ty->kind == TY_VLA) {
      println("  mov %d(%%rbp), %%rax", node->var->offset);
      return;
    }

    // Local variable
    if (node->var->is_local) {
      addr_of_local(node->var, "%rax");
      return;
    }

    if (opt_fpic) {
      // Thread-local variable
      if (node->var->is_tls) {
        println("  data16 lea %s@tlsgd(%%rip), %%rdi", node->var->name);
        println("  .value 0x6666");
        println("  rex64");
        println("  call __tls_get_addr@PLT");
        return;
      }

      // Function or global variable
      println("  mov %s@GOTPCREL(%%rip), %%rax", node->var->name);
      return;
    }

    // Thread-local variable
    if (node->var->is_tls) {
      println("  mov %%fs:0, %%rax");
      println("  add $%s@tpoff, %%rax", node->var->name);
      return;
    }

    // Here, we generate an absolute address of a function or a global
    // variable. Even though they exist at a certain address at runtime,
    // their addresses are not known at link-time for the following
    // two reasons.
    //
    //  - Address randomization: Executables are loaded to memory as a
    //    whole but it is not known what address they are loaded to.
    //    Therefore, at link-time, relative address in the same
    //    exectuable (i.e. the distance between two functions in the
    //    same executable) is known, but the absolute address is not
    //    known.
    //
    //  - Dynamic linking: Dynamic shared objects (DSOs) or .so files
    //    are loaded to memory alongside an executable at runtime and
    //    linked by the runtime loader in memory. We know nothing
    //    about addresses of global stuff that may be defined by DSOs
    //    until the runtime relocation is complete.
    //
    // In order to deal with the former case, we use RIP-relative
    // addressing, denoted by `(%rip)`. For the latter, we obtain an
    // address of a stuff that may be in a shared object file from the
    // Global Offset Table using `@GOTPCREL(%rip)` notation.

    // Function
    if (node->ty->kind == TY_FUNC) {
      if (node->var->is_definition)
        println("  lea %s(%%rip), %%rax", node->var->name);
      else
        println("  mov %s@GOTPCREL(%%rip), %%rax", node->var->name);
      return;
    }

    // Global variable
    println("  lea %s(%%rip), %%rax", node->var->name);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_discard(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    println("  add $%d, %%rax", node->member->offset);
    return;
  case ND_FUNCALL:
    if (node->ret_buffer) {
      gen_expr(node);
      return;
    }
    break;
  case ND_ASSIGN:
  case ND_COND:
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
      gen_expr(node);
      return;
    }
    break;
  case ND_VLA_PTR:
    println("  lea %d(%%rbp), %%rax", node->var->offset);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// True for types whose values live in memory and are handled by address
// (arrays, structs, ...), as opposed to scalars that fit in a register.
static bool is_aggregate(Type *ty) {
  switch (ty->kind) {
  case TY_ARRAY:
  case TY_STRUCT:
  case TY_UNION:
  case TY_FUNC:
  case TY_VLA:
    return true;
  }
  return false;
}

// Returns the memory operand of a local variable, e.g. "-8(%rbp)".
// The string is overwritten by the next call.
static char *local_addr(Obj *var) {
  static char buf[32];
  snprintf(buf, sizeof(buf), "%d(%%rbp)", var->offset);
  return buf;
}

// Where local scalar `var` lives: its register, sized for its type (e.g.
// "%r12d" for an int), or its stack slot. load_from() and store_to()
// take either.
static char *var_operand(Obj *var) {
  if (!var->reg)
    return local_addr(var);
  int i = var->reg - 1;
  switch (var->ty->size) {
  case 1: return regs8[i];
  case 2: return regs16[i];
  case 4: return regs32[i];
  }
  return regs64[i];
}

// Load a value of type `ty` from memory operand `addr`, e.g. "(%rax)",
// into %rax (or %xmm0, or the x87 stack for long double).
static void load_from(Type *ty, char *addr) {
  switch (ty->kind) {
  case TY_FLOAT:
    println("  movss %s, %%xmm0", addr);
    return;
  case TY_DOUBLE:
    println("  movsd %s, %%xmm0", addr);
    return;
  case TY_LDOUBLE:
    println("  fldt %s", addr);
    return;
  case TY_INT128:
    // The high half first: `addr` may be based on %rax.
    println("  mov %s, %%rdx", high_half(addr));
    println("  mov %s, %%rax", addr);
    return;
  case TY_VECTOR:
    println("  %s %s, %%xmm0", vec_move(ty->size), addr);
    return;
  }

  char *insn = ty->is_unsigned ? "movz" : "movs";

  // When we load a char or a short value to a register, we always
  // extend them to the size of int, so we can assume the lower half of
  // a register always contains a valid value. The upper half of a
  // register for char, short and int may contain garbage. When we load
  // a long value to a register, it simply occupies the entire register.
  if (ty->size == 1)
    println("  %sbl %s, %%eax", insn, addr);
  else if (ty->size == 2)
    println("  %swl %s, %%eax", insn, addr);
  else if (ty->size == 4)
    println("  movsxd %s, %%rax", addr);
  else
    println("  mov %s, %%rax", addr);
}

// Load a value from where %rax is pointing to.
static void load(Type *ty) {
  // If it is an array, do not attempt to load a value to the
  // register because in general we can't load an entire array to a
  // register. As a result, the result of an evaluation of an array
  // becomes not the array itself but the address of the array.
  // This is where "array is automatically converted to a pointer to
  // the first element of the array in C" occurs.
  if (is_aggregate(ty) || ty->kind == TY_VOID) // `*p` on a void *p reads nothing
    return;
  load_from(ty, "(%rax)");
}

// Store the scalar in %rax (or %xmm0, or the x87 stack) to memory
// operand `addr`, e.g. "(%rdi)". The value stays where it was, as an
// assignment's value: a long double is loaded back, since fstpt pops it.
static void store_to(Type *ty, char *addr) {
  switch (ty->kind) {
  case TY_FLOAT:
    println("  movss %%xmm0, %s", addr);
    return;
  case TY_DOUBLE:
    println("  movsd %%xmm0, %s", addr);
    return;
  case TY_LDOUBLE:
    println("  fstpt %s", addr);
    println("  fldt %s", addr);
    return;
  case TY_INT128:
    println("  mov %%rax, %s", addr);
    println("  mov %%rdx, %s", high_half(addr));
    return;
  case TY_VECTOR:
    println("  %s %%xmm0, %s", vec_move(ty->size), addr);
    return;
  }

  if (ty->size == 1)
    println("  mov %%al, %s", addr);
  else if (ty->size == 2)
    println("  mov %%ax, %s", addr);
  else if (ty->size == 4)
    println("  mov %%eax, %s", addr);
  else
    println("  mov %%rax, %s", addr);
}

// Loads a bit-field's unit, the mem->unit bytes at `addr`, into %rax (the
// bits above them may be anything). An odd size, which only a packed
// struct has (see place_loose_bitfields), is read a byte at a time.
static void load_unit(int unit, char *addr) {
  switch (unit) {
  case 1: println("  movzbl %s, %%eax", addr); return;
  case 2: println("  movzwl %s, %%eax", addr); return;
  case 4: println("  mov %s, %%eax", addr); return;
  case 8: println("  mov %s, %%rax", addr); return;
  }
  println("  lea %s, %%rcx", addr);
  println("  xor %%eax, %%eax");
  for (int i = unit - 1; i >= 0; i--) {
    println("  shl $8, %%rax");
    println("  mov %d(%%rcx), %%al", i);
  }
}

// Stores %rax's low `unit` bytes to (%rdi), as load_unit reads them.
static void store_unit(int unit) {
  switch (unit) {
  case 1: println("  mov %%al, (%%rdi)"); return;
  case 2: println("  mov %%ax, (%%rdi)"); return;
  case 4: println("  mov %%eax, (%%rdi)"); return;
  case 8: println("  mov %%rax, (%%rdi)"); return;
  }
  for (int i = 0; i < unit; i++) {
    println("  mov %%al, %d(%%rdi)", i);
    println("  shr $8, %%rax");
  }
}

// Store %rax to an address that the stack top is pointing to.
static void store(Type *ty) {
  pop("%rdi");

  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (int i = 0; i < ty->size; i++) {
      println("  mov %d(%%rax), %%r8b", i);
      println("  mov %%r8b, %d(%%rdi)", i);
    }
    return;
  }
  store_to(ty, "(%rdi)");
}

// Compares the value with 0: afterwards ZF is set if it's zero, as after
// `cmp $0`. A NaN isn't zero, but compares unordered, which sets ZF too,
// so for floating point ZF comes from (x != 0 || unordered) in %al.
static void cmp_zero(Type *ty) {
  // A complex number, whose address is in %rax: nonzero if either part is
  if (is_complex(ty)) {
    Type *part = complex_part(ty);
    println("  mov %%rax, %%rcx");
    load_from(part, "(%rcx)");
    cmp_zero(part);
    println("  setne %%r8b");
    load_from(part, format("%d(%%rcx)", part->size));
    cmp_zero(part);
    println("  setne %%al");
    println("  or %%r8b, %%al");
    return;
  }

  switch (ty->kind) {
  case TY_FLOAT:
    println("  xorps %%xmm1, %%xmm1");
    println("  ucomiss %%xmm1, %%xmm0");
    break;
  case TY_DOUBLE:
    println("  xorpd %%xmm1, %%xmm1");
    println("  ucomisd %%xmm1, %%xmm0");
    break;
  case TY_LDOUBLE:
    println("  fldz");
    println("  fucomip");
    println("  fstp %%st(0)");
    break;
  case TY_INT128:
    println("  mov %%rax, %%rcx");
    println("  or %%rdx, %%rcx");
    return;
  default:
    if (is_integer(ty) && ty->size <= 4)
      println("  cmp $0, %%eax");
    else
      println("  cmp $0, %%rax");
    return;
  }

  println("  setne %%al");
  println("  setp %%dl");
  println("  or %%dl, %%al");
}

//---------- Type casts ------------------------------------------------------

enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, F80 };

static int getTypeId(Type *ty) {
  switch (ty->kind) {
  case TY_CHAR:
    return ty->is_unsigned ? U8 : I8;
  case TY_SHORT:
    return ty->is_unsigned ? U16 : I16;
  case TY_INT:
    return ty->is_unsigned ? U32 : I32;
  case TY_LONG:
    return ty->is_unsigned ? U64 : I64;
  case TY_FLOAT:
    return F32;
  case TY_DOUBLE:
    return F64;
  case TY_LDOUBLE:
    return F80;
  }
  return U64;
}

// The table for type casts
static char i32i8[] = "movsbl %al, %eax";
static char i32u8[] = "movzbl %al, %eax";
static char i32i16[] = "movswl %ax, %eax";
static char i32u16[] = "movzwl %ax, %eax";
static char i32f32[] = "cvtsi2ssl %eax, %xmm0";
static char i32i64[] = "movsxd %eax, %rax";
static char i32f64[] = "cvtsi2sdl %eax, %xmm0";
static char i32f80[] = "mov %eax, -4(%rsp); fildl -4(%rsp)";

static char u32f32[] = "mov %eax, %eax; cvtsi2ssq %rax, %xmm0";
static char u32i64[] = "mov %eax, %eax";
static char u32f64[] = "mov %eax, %eax; cvtsi2sdq %rax, %xmm0";
static char u32f80[] = "mov %eax, %eax; mov %rax, -8(%rsp); fildll -8(%rsp)";

static char i64f32[] = "cvtsi2ssq %rax, %xmm0";
static char i64f64[] = "cvtsi2sdq %rax, %xmm0";
static char i64f80[] = "movq %rax, -8(%rsp); fildll -8(%rsp)";

// From 2^63 up, the signed conversions don't work: halve the value
// (keeping the low bit, so it rounds the same), convert, and double it.
static char u64f32[] =
  "test %rax,%rax; js 1f; pxor %xmm0,%xmm0; cvtsi2ss %rax,%xmm0; jmp 2f; "
  "1: mov %rax,%rdi; and $1,%eax; pxor %xmm0,%xmm0; shr %rdi; "
  "or %rax,%rdi; cvtsi2ss %rdi,%xmm0; addss %xmm0,%xmm0; 2:";
static char u64f64[] =
  "test %rax,%rax; js 1f; pxor %xmm0,%xmm0; cvtsi2sd %rax,%xmm0; jmp 2f; "
  "1: mov %rax,%rdi; and $1,%eax; pxor %xmm0,%xmm0; shr %rdi; "
  "or %rax,%rdi; cvtsi2sd %rdi,%xmm0; addsd %xmm0,%xmm0; 2:";
static char u64f80[] =
  "mov %rax, -8(%rsp); fildq -8(%rsp); test %rax, %rax; jns 1f;"
  "mov $1602224128, %eax; mov %eax, -4(%rsp); fadds -4(%rsp); 1:";

static char f32i8[] = "cvttss2sil %xmm0, %eax; movsbl %al, %eax";
static char f32u8[] = "cvttss2sil %xmm0, %eax; movzbl %al, %eax";
static char f32i16[] = "cvttss2sil %xmm0, %eax; movswl %ax, %eax";
static char f32u16[] = "cvttss2sil %xmm0, %eax; movzwl %ax, %eax";
static char f32i32[] = "cvttss2sil %xmm0, %eax";
static char f32u32[] = "cvttss2siq %xmm0, %rax";
static char f32i64[] = "cvttss2siq %xmm0, %rax";
// From 2^63 up, which cvtt*2siq can't convert, subtract 2^63 first and put
// it back as the top bit.
static char f32u64[] =
  "mov $1593835520, %eax; mov %eax, -4(%rsp); comiss -4(%rsp), %xmm0; "
  "jae 1f; cvttss2siq %xmm0, %rax; jmp 2f; "
  "1: subss -4(%rsp), %xmm0; cvttss2siq %xmm0, %rax; "
  "mov $1, %edi; shl $63, %rdi; xor %rdi, %rax; 2:";
static char f32f64[] = "cvtss2sd %xmm0, %xmm0";
static char f32f80[] = "movss %xmm0, -4(%rsp); flds -4(%rsp)";

static char f64i8[] = "cvttsd2sil %xmm0, %eax; movsbl %al, %eax";
static char f64u8[] = "cvttsd2sil %xmm0, %eax; movzbl %al, %eax";
static char f64i16[] = "cvttsd2sil %xmm0, %eax; movswl %ax, %eax";
static char f64u16[] = "cvttsd2sil %xmm0, %eax; movzwl %ax, %eax";
static char f64i32[] = "cvttsd2sil %xmm0, %eax";
static char f64u32[] = "cvttsd2siq %xmm0, %rax";
static char f64i64[] = "cvttsd2siq %xmm0, %rax";
static char f64u64[] =
  "mov $4890909195324358656, %rax; mov %rax, -8(%rsp); comisd -8(%rsp), %xmm0; "
  "jae 1f; cvttsd2siq %xmm0, %rax; jmp 2f; "
  "1: subsd -8(%rsp), %xmm0; cvttsd2siq %xmm0, %rax; "
  "mov $1, %edi; shl $63, %rdi; xor %rdi, %rax; 2:";
static char f64f32[] = "cvtsd2ss %xmm0, %xmm0";
static char f64f80[] = "movsd %xmm0, -8(%rsp); fldl -8(%rsp)";

#define FROM_F80_1                                           \
  "fnstcw -10(%rsp); movzwl -10(%rsp), %eax; or $12, %ah; " \
  "mov %ax, -12(%rsp); fldcw -12(%rsp); "

#define FROM_F80_2 " -24(%rsp); fldcw -10(%rsp); "

static char f80i8[] = FROM_F80_1 "fistps" FROM_F80_2 "movsbl -24(%rsp), %eax";
static char f80u8[] = FROM_F80_1 "fistps" FROM_F80_2 "movzbl -24(%rsp), %eax";
static char f80i16[] = FROM_F80_1 "fistps" FROM_F80_2 "movswl -24(%rsp), %eax";
static char f80u16[] = FROM_F80_1 "fistpl" FROM_F80_2 "movzwl -24(%rsp), %eax";
static char f80i32[] = FROM_F80_1 "fistpl" FROM_F80_2 "mov -24(%rsp), %eax";
static char f80u32[] = FROM_F80_1 "fistpq" FROM_F80_2 "mov -24(%rsp), %eax";
static char f80i64[] = FROM_F80_1 "fistpq" FROM_F80_2 "mov -24(%rsp), %rax";
// As f64u64, with x87 compares: 2^63 is 1593835520 as a float, and adding
// -2^63 (-553648128) subtracts it.
static char f80u64[] =
  FROM_F80_1 "mov $1593835520, %eax; mov %eax, -4(%rsp); flds -4(%rsp); "
  "fucomip; jbe 1f; fistpq -24(%rsp); mov -24(%rsp), %rax; jmp 2f; "
  "1: mov $-553648128, %eax; mov %eax, -4(%rsp); fadds -4(%rsp); "
  "fistpq -24(%rsp); mov -24(%rsp), %rax; "
  "mov $1, %edi; shl $63, %rdi; xor %rdi, %rax; 2: fldcw -10(%rsp)";
static char f80f32[] = "fstps -8(%rsp); movss -8(%rsp), %xmm0";
static char f80f64[] = "fstpl -8(%rsp); movsd -8(%rsp), %xmm0";

static char *cast_table[][11] = {
  // i8   i16     i32     i64     u8     u16     u32     u64     f32     f64     f80
  {NULL,  NULL,   NULL,   i32i64, i32u8, i32u16, NULL,   i32i64, i32f32, i32f64, i32f80}, // i8
  {i32i8, NULL,   NULL,   i32i64, i32u8, i32u16, NULL,   i32i64, i32f32, i32f64, i32f80}, // i16
  {i32i8, i32i16, NULL,   i32i64, i32u8, i32u16, NULL,   i32i64, i32f32, i32f64, i32f80}, // i32
  {i32i8, i32i16, NULL,   NULL,   i32u8, i32u16, NULL,   NULL,   i64f32, i64f64, i64f80}, // i64

  {i32i8, NULL,   NULL,   i32i64, NULL,  NULL,   NULL,   i32i64, i32f32, i32f64, i32f80}, // u8
  {i32i8, i32i16, NULL,   i32i64, i32u8, NULL,   NULL,   i32i64, i32f32, i32f64, i32f80}, // u16
  {i32i8, i32i16, NULL,   u32i64, i32u8, i32u16, NULL,   u32i64, u32f32, u32f64, u32f80}, // u32
  {i32i8, i32i16, NULL,   NULL,   i32u8, i32u16, NULL,   NULL,   u64f32, u64f64, u64f80}, // u64

  {f32i8, f32i16, f32i32, f32i64, f32u8, f32u16, f32u32, f32u64, NULL,   f32f64, f32f80}, // f32
  {f64i8, f64i16, f64i32, f64i64, f64u8, f64u16, f64u32, f64u64, f64f32, NULL,   f64f80}, // f64
  {f80i8, f80i16, f80i32, f80i64, f80u8, f80u16, f80u32, f80u64, f80f32, f80f64, NULL},   // f80
};

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID) {
    if (from->kind == TY_LDOUBLE)
      println("  fstp %%st(0)"); // off the x87 stack
    return;
  }

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  setne %%al");
    println("  movzx %%al, %%eax");
    return;
  }

  if (is_vector(from) || is_vector(to)) {
    cast_vector(from, to);
    return;
  }

  if (is_int128(from) || is_int128(to)) {
    cast_int128(from, to);
    return;
  }

  int t1 = getTypeId(from);
  int t2 = getTypeId(to);
  if (cast_table[t1][t2])
    println("  %s", cast_table[t1][t2]);
}

//---------- 128-bit integers ------------------------------------------------
//
// An __int128 value is in %rdx:%rax, the high half in %rdx, where the
// psABI returns one. Pushed, it takes two slots, %rdx first, so that its
// halves are in memory order. Most operators are a few instructions on
// the halves. Division and conversions to and from floating point are
// routines, emitted at the end of each file that uses them (see
// emit_int128_routines()): mucc never calls libgcc's.

static bool uses_int128_routines;

// Memory operand `addr` 8 bytes on: "(%rax)" -> "8(%rax)", "-16(%rbp)" ->
// "-8(%rbp)". The high half of an __int128 is there.
static char *high_half(char *addr) {
  char *rest;
  long offset = strtol(addr, &rest, 10);
  return format("%ld%s", offset + 8, rest);
}

static void push128(void) {
  println("  push %%rdx");
  depth++;
  push();
}

static void neg128(char *lo, char *hi) {
  println("  neg %s", lo);
  println("  adc $0, %s", hi);
  println("  neg %s", hi);
}

static void cast_int128(Type *from, Type *to) {
  if (is_int128(from) && is_int128(to))
    return;

  if (is_int128(to)) {
    if (is_flonum(from)) {
      if (from->kind != TY_LDOUBLE)
        cast(from, ty_ldouble);
      uses_int128_routines = true;
      println("  call .L.int128.from_f80%s", to->is_unsigned ? "u" : "");
      return;
    }
    // An integer or a pointer: as 64 bits, extended
    cast(from, from->is_unsigned ? ty_ulong : ty_long);
    if (from->is_unsigned)
      println("  xor %%edx, %%edx");
    else
      println("  cqo");
    return;
  }

  // To floating point, through a long double. One for float or double
  // keeps the bits that matter for rounding to them once (see
  // emit_int128_routines()).
  if (is_flonum(to)) {
    uses_int128_routines = true;
    char *u = from->is_unsigned ? "u" : "";
    if (to->kind == TY_LDOUBLE) {
      println("  call .L.int128.to_f80%s", u);
    } else {
      println("  call .L.int128.to_f80%s_sticky", u);
      cast(ty_ldouble, to);
    }
    return;
  }

  // To a narrower integer or a pointer: the low half, cut down as a long
  cast(from->is_unsigned ? ty_ulong : ty_long, to);
}

// Binary operator `node` on __int128 operands. A shift's count may be
// any integer; it goes in %cl.
static void gen_int128_binary(Node *node) {
  bool u = node->lhs->ty->is_unsigned;

  if (node->kind == ND_SHL || node->kind == ND_SHR) {
    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rcx");
    if (node->kind == ND_SHL) {
      println("  shld %%cl, %%rax, %%rdx");
      println("  shl %%cl, %%rax");
      println("  test $64, %%cl");
      println("  jz 1f");
      println("  mov %%rax, %%rdx");
      println("  xor %%eax, %%eax");
    } else {
      println("  shrd %%cl, %%rdx, %%rax");
      println("  %s %%cl, %%rdx", u ? "shr" : "sar");
      println("  test $64, %%cl");
      println("  jz 1f");
      println("  mov %%rdx, %%rax");
      if (u)
        println("  xor %%edx, %%edx");
      else
        println("  sar $63, %%rdx");
    }
    println("1:");
    return;
  }

  // The left side in %rdx:%rax, the right in %rsi:%rdi
  gen_expr(node->rhs);
  push128();
  gen_expr(node->lhs);
  pop("%rdi");
  pop("%rsi");

  switch (node->kind) {
  case ND_ADD:
    println("  add %%rdi, %%rax");
    println("  adc %%rsi, %%rdx");
    return;
  case ND_SUB:
    println("  sub %%rdi, %%rax");
    println("  sbb %%rsi, %%rdx");
    return;
  case ND_MUL:
    // The low halves' full product, with the cross products added to
    // its high half
    println("  mov %%rdx, %%rcx");
    println("  imul %%rdi, %%rcx");
    println("  mov %%rax, %%r8");
    println("  imul %%rsi, %%r8");
    println("  add %%r8, %%rcx");
    println("  mul %%rdi");
    println("  add %%rcx, %%rdx");
    return;
  case ND_DIV:
  case ND_MOD:
    uses_int128_routines = true;
    println("  call .L.int128.%sdivmod", u ? "u" : "");
    if (node->kind == ND_MOD) {
      println("  mov %%rdi, %%rax");
      println("  mov %%rsi, %%rdx");
    }
    return;
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR: {
    char *op = node->kind == ND_BITAND ? "and" :
               node->kind == ND_BITOR ? "or" : "xor";
    println("  %s %%rdi, %%rax", op);
    println("  %s %%rsi, %%rdx", op);
    return;
  }
  case ND_EQ:
  case ND_NE:
    println("  xor %%rdi, %%rax");
    println("  xor %%rsi, %%rdx");
    println("  or %%rdx, %%rax");
    println("  set%s %%al", node->kind == ND_EQ ? "e" : "ne");
    println("  movzb %%al, %%rax");
    return;
  case ND_LT:
    // The flags of left - right, in 128 bits
    println("  cmp %%rdi, %%rax");
    println("  sbb %%rsi, %%rdx");
    println("  set%s %%al", u ? "b" : "l");
    println("  movzb %%al, %%rax");
    return;
  case ND_LE:
    // right - left >= 0
    println("  cmp %%rax, %%rdi");
    println("  sbb %%rdx, %%rsi");
    println("  set%s %%al", u ? "ae" : "ge");
    println("  movzb %%al, %%rax");
    return;
  }
  error_tok(node->tok, "invalid expression");
}

// The routines take a value in %rdx:%rax or on the x87 stack, and use
// only caller-saved registers and the red zone.
static void emit_int128_routines(void) {
  if (!uses_int128_routines)
    return;
  println("  .text");

  // udivmod: %rdx:%rax / %rsi:%rdi, unsigned. The quotient goes in
  // %rdx:%rax, the remainder in %rsi:%rdi. A divisor below 2^64 takes
  // two div instructions (dividing by 0 traps, as with gcc); a larger
  // one, a bit at a time: each of the dividend's bits is shifted into
  // the remainder, which is above the divisor if that carries out of it.
  println(".L.int128.udivmod:");
  println("  test %%rsi, %%rsi");
  println("  jnz 1f");
  println("  mov %%rax, %%r8");
  println("  mov %%rdx, %%rax");
  println("  xor %%edx, %%edx");
  println("  div %%rdi");
  println("  mov %%rax, %%r9");
  println("  mov %%r8, %%rax");
  println("  div %%rdi");
  println("  mov %%rdx, %%rdi");
  println("  mov %%r9, %%rdx");
  println("  ret");
  println("1:");
  println("  xor %%r10, %%r10");
  println("  xor %%r11, %%r11");
  println("  mov $128, %%ecx");
  println("2:");
  println("  add %%rax, %%rax");
  println("  adc %%rdx, %%rdx");
  println("  adc %%r10, %%r10");
  println("  adc %%r11, %%r11");
  println("  jc 3f");
  println("  cmp %%rdi, %%r10");
  println("  mov %%r11, %%r8");
  println("  sbb %%rsi, %%r8");
  println("  jb 4f");
  println("3:");
  println("  sub %%rdi, %%r10");
  println("  sbb %%rsi, %%r11");
  println("  or $1, %%rax");
  println("4:");
  println("  dec %%ecx");
  println("  jnz 2b");
  println("  mov %%r10, %%rdi");
  println("  mov %%r11, %%rsi");
  println("  ret");

  // divmod: signed, on the magnitudes. The quotient is negative if the
  // signs differ; the remainder has the dividend's sign.
  println(".L.int128.divmod:");
  println("  push %%rdx");
  println("  mov %%rdx, %%r8");
  println("  xor %%rsi, %%r8");
  println("  push %%r8");
  println("  test %%rdx, %%rdx");
  println("  jns 1f");
  neg128("%rax", "%rdx");
  println("1:");
  println("  test %%rsi, %%rsi");
  println("  jns 2f");
  neg128("%rdi", "%rsi");
  println("2:");
  println("  call .L.int128.udivmod");
  println("  pop %%r8");
  println("  test %%r8, %%r8");
  println("  jns 3f");
  neg128("%rax", "%rdx");
  println("3:");
  println("  pop %%r8");
  println("  test %%r8, %%r8");
  println("  jns 4f");
  neg128("%rdi", "%rsi");
  println("4:");
  println("  ret");

  // to_f80u: to long double, the high half times 2^64 (a float at
  // -12(%rsp)), exactly, plus the low half, rounded once
  println(".L.int128.to_f80u:");
  println("  movl $1602224128, -12(%%rsp)");
  println("  mov %%rdx, -8(%%rsp)");
  println("  fildq -8(%%rsp)");
  println("  test %%rdx, %%rdx");
  println("  jns 1f");
  println("  fadds -12(%%rsp)");
  println("1:");
  println("  flds -12(%%rsp)");
  println("  fmulp");
  println("  mov %%rax, -8(%%rsp)");
  println("  fildq -8(%%rsp)");
  println("  test %%rax, %%rax");
  println("  jns 2f");
  println("  fadds -12(%%rsp)");
  println("2:");
  println("  faddp");
  println("  ret");

  println(".L.int128.to_f80:");
  println("  test %%rdx, %%rdx");
  println("  jns .L.int128.to_f80u");
  neg128("%rax", "%rdx");
  println("  call .L.int128.to_f80u");
  println("  fchs");
  println("  ret");

  // to_f80u_sticky: for float and double, which a long double rounded
  // from 128 bits would round a second time. A value of 2^64 or more is
  // shifted right to 64 bits, keeping whether any one bits were shifted
  // out in its lowest bit (the sticky bit), converted exactly, and
  // scaled back up by a float power of two built in %ecx.
  println(".L.int128.to_f80u_sticky:");
  println("  test %%rdx, %%rdx");
  println("  jz .L.int128.to_f80u");
  println("  bsr %%rdx, %%rcx");
  println("  mov $-2, %%r8");
  println("  shl %%cl, %%r8");
  println("  not %%r8");
  println("  and %%rax, %%r8");
  println("  shrd $1, %%rdx, %%rax");
  println("  shr $1, %%rdx");
  println("  shrd %%cl, %%rdx, %%rax");
  println("  test %%r8, %%r8");
  println("  setne %%r8b");
  println("  movzbl %%r8b, %%r8d");
  println("  or %%r8, %%rax");
  println("  xor %%edx, %%edx");
  println("  call .L.int128.to_f80u");
  println("  add $128, %%ecx");
  println("  shl $23, %%ecx");
  println("  mov %%ecx, -4(%%rsp)");
  println("  flds -4(%%rsp)");
  println("  fmulp");
  println("  ret");

  println(".L.int128.to_f80_sticky:");
  println("  test %%rdx, %%rdx");
  println("  jns .L.int128.to_f80u_sticky");
  neg128("%rax", "%rdx");
  println("  call .L.int128.to_f80u_sticky");
  println("  fchs");
  println("  ret");

  // from_f80u: the long double on the x87 stack, from 0 up to 2^128,
  // truncated. The high half is the value times 2^-64 (exact),
  // truncated; the low half is the rest, value - high * 2^64 (exact
  // too). The value waits at -48(%rsp), below what the casts use.
  println(".L.int128.from_f80u:");
  println("  fstpt -48(%%rsp)");
  println("  fldt -48(%%rsp)");
  println("  movl $528482304, -32(%%rsp)");
  println("  flds -32(%%rsp)");
  println("  fmulp");
  cast(ty_ldouble, ty_ulong);
  println("  mov %%rax, -56(%%rsp)");
  cast(ty_ulong, ty_ldouble);
  println("  movl $1602224128, -32(%%rsp)");
  println("  flds -32(%%rsp)");
  println("  fmulp");
  println("  fldt -48(%%rsp)");
  println("  fsubrp");
  println("  fchs");
  cast(ty_ldouble, ty_ulong);
  println("  mov -56(%%rsp), %%rdx");
  println("  ret");

  // from_f80: a negative value's magnitude, negated
  println(".L.int128.from_f80:");
  println("  fldz");
  println("  fucomip");
  println("  jbe .L.int128.from_f80u");
  println("  fchs");
  println("  call .L.int128.from_f80u");
  neg128("%rax", "%rdx");
  println("  ret");
}

//---------- Vectors ---------------------------------------------------------
//
// [GNU] A vector's value (of 4, 8 or 16 bytes, see vector_type() in
// parser.c) is in %xmm0, its elements from the low end, where the psABI
// passes one. Pushed, it takes 16 bytes, or 8 if it has 8 or less. An
// operator that SSE2 has an instruction for, for the element type, is
// that instruction on %xmm0 and %xmm1: every one on floats and doubles,
// and + - & | ^ == != on integers, * on shorts and < <= on signed integers
// of up to 4 bytes. The others are done an element at a time (see
// vector_by_element()). Nothing past SSE2 is used, so the code runs on any
// x86-64 CPU.

// The instruction that moves a vector of `size` bytes between an XMM
// register and memory
static char *vec_move(int size) {
  return size == 16 ? "movdqu" : size == 8 ? "movq" : "movd";
}

static void push_vec(Type *ty) {
  int sz = ty->size == 16 ? 16 : 8;
  println("  sub $%d, %%rsp", sz);
  println("  %s %%xmm0, (%%rsp)", vec_move(ty->size));
  depth += sz / 8;
}

static void pop_vec(Type *ty, int reg) {
  int sz = ty->size == 16 ? 16 : 8;
  println("  %s (%%rsp), %%xmm%d", vec_move(ty->size), reg);
  println("  add $%d, %%rsp", sz);
  depth -= sz / 8;
}

// The suffix of SSE2's integer instructions for elements of `size` bytes
static char *int_suffix(int size) {
  return size == 1 ? "b" : size == 2 ? "w" : size == 4 ? "d" : "q";
}

// Sets XMM register `reg` to all one bits.
static void all_ones(int reg) {
  println("  pcmpeqd %%xmm%d, %%xmm%d", reg, reg);
}

// A cast to or from a vector keeps the bits (see check_vector_cast() in
// parser.c), but for a number to a larger vector, which the parser makes
// only of a number of the element type: every element is it, as in `v + 1`.
static void cast_vector(Type *from, Type *to) {
  if (is_vector(from) && is_vector(to))
    return;

  if (is_vector(to)) {
    if (from->size < to->size) {
      for (int i = 0; i < to->array_len; i++)
        store_to(from, format("%d(%%rsp)", -16 + i * from->size));
      println("  %s -16(%%rsp), %%xmm0", vec_move(to->size));
    } else if (is_int128(from)) {
      println("  movq %%rax, %%xmm0");
      println("  movq %%rdx, %%xmm1");
      println("  punpcklqdq %%xmm1, %%xmm0");
    } else {
      println(from->size == 8 ? "  movq %%rax, %%xmm0" : "  movd %%eax, %%xmm0");
    }
    return;
  }

  if (is_int128(to)) {
    println("  movq %%xmm0, %%rax");
    println("  pshufd $0xee, %%xmm0, %%xmm0");
    println("  movq %%xmm0, %%rdx");
  } else {
    println(to->size == 8 ? "  movq %%xmm0, %%rax" : "  movd %%xmm0, %%eax");
  }
}

// Loads the vector element at `off`(%rsp), of type `ty`, into 64-bit
// register `reg` ("ax" or "di"), extended as its type says.
static void load_elem(Type *ty, int off, char *reg) {
  char *insn = ty->is_unsigned ? "movz" : "movs";
  switch (ty->size) {
  case 1: println("  %sbq %d(%%rsp), %%r%s", insn, off, reg); return;
  case 2: println("  %swq %d(%%rsp), %%r%s", insn, off, reg); return;
  case 4:
    if (ty->is_unsigned)
      println("  mov %d(%%rsp), %%e%s", off, reg);
    else
      println("  movslq %d(%%rsp), %%r%s", off, reg);
    return;
  }
  println("  mov %d(%%rsp), %%r%s", off, reg);
}

// Integer operator `node` on vectors %xmm0 and %xmm1, an element at a
// time: both are put in the red zone, each pair of elements is computed in
// %rax and %rdi (as 64-bit values, extended from their type), and the
// result goes back in place of the left side's element.
static void vector_by_element(Node *node) {
  Type *vec = node->lhs->ty;
  Type *elem = vec->elem;
  int sz = elem->size;
  bool u = elem->is_unsigned;

  println("  %s %%xmm0, -32(%%rsp)", vec_move(vec->size));
  println("  %s %%xmm1, -16(%%rsp)", vec_move(vec->size));
  for (int i = 0; i < vec->array_len; i++) {
    int off = -32 + i * sz;
    load_elem(elem, off, "ax");
    load_elem(elem, off + 16, "di");

    switch (node->kind) {
    case ND_ADD: println("  add %%rdi, %%rax"); break;
    case ND_SUB: println("  sub %%rdi, %%rax"); break;
    case ND_MUL: println("  imul %%rdi, %%rax"); break;
    case ND_BITAND: println("  and %%rdi, %%rax"); break;
    case ND_BITOR: println("  or %%rdi, %%rax"); break;
    case ND_BITXOR: println("  xor %%rdi, %%rax"); break;
    case ND_DIV:
    case ND_MOD:
      if (u) {
        println("  xor %%edx, %%edx");
        println("  div %%rdi");
      } else {
        println("  cqo");
        println("  idiv %%rdi");
      }
      if (node->kind == ND_MOD)
        println("  mov %%rdx, %%rax");
      break;
    case ND_SHL:
    case ND_SHR:
      println("  mov %%rdi, %%rcx");
      println("  %s %%cl, %%rax", node->kind == ND_SHL ? "shl" : u ? "shr" : "sar");
      break;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE: {
      char *cc = node->kind == ND_EQ ? "e" : node->kind == ND_NE ? "ne" :
                 node->kind == ND_LT ? (u ? "b" : "l") : (u ? "be" : "le");
      println("  cmp %%rdi, %%rax");
      println("  set%s %%al", cc);
      println("  movzbl %%al, %%eax");
      println("  neg %%rax");
      break;
    }
    default:
      error_tok(node->tok, "invalid expression");
    }
    println("  mov %s, %d(%%rsp)", reg_ax(sz), off);
  }
  println("  %s -32(%%rsp), %%xmm0", vec_move(vec->size));
}

static void gen_vector_binary(Node *node) {
  Type *vec = node->lhs->ty;
  Type *elem = vec->elem;
  gen_expr(node->rhs);
  push_vec(vec);
  gen_expr(node->lhs);
  pop_vec(vec, 1);

  if (is_flonum(elem)) {
    char *s = elem->kind == TY_FLOAT ? "ps" : "pd";
    switch (node->kind) {
    case ND_ADD: println("  add%s %%xmm1, %%xmm0", s); return;
    case ND_SUB: println("  sub%s %%xmm1, %%xmm0", s); return;
    case ND_MUL: println("  mul%s %%xmm1, %%xmm0", s); return;
    case ND_DIV: println("  div%s %%xmm1, %%xmm0", s); return;
    // cmpps's predicates: 0 is ==, 1 <, 2 <=, 4 != (true for a NaN)
    case ND_EQ: println("  cmp%s $0, %%xmm1, %%xmm0", s); return;
    case ND_LT: println("  cmp%s $1, %%xmm1, %%xmm0", s); return;
    case ND_LE: println("  cmp%s $2, %%xmm1, %%xmm0", s); return;
    case ND_NE: println("  cmp%s $4, %%xmm1, %%xmm0", s); return;
    }
    error_tok(node->tok, "invalid expression");
  }

  char *c = int_suffix(elem->size);
  switch (node->kind) {
  case ND_ADD: println("  padd%s %%xmm1, %%xmm0", c); return;
  case ND_SUB: println("  psub%s %%xmm1, %%xmm0", c); return;
  case ND_BITAND: println("  pand %%xmm1, %%xmm0"); return;
  case ND_BITOR: println("  por %%xmm1, %%xmm0"); return;
  case ND_BITXOR: println("  pxor %%xmm1, %%xmm0"); return;
  case ND_MUL:
    if (elem->size != 2)
      break;
    println("  pmullw %%xmm1, %%xmm0");
    return;
  case ND_EQ:
  case ND_NE:
    if (elem->size == 8)
      break;
    println("  pcmpeq%s %%xmm1, %%xmm0", c);
    if (node->kind == ND_NE) {
      all_ones(1);
      println("  pxor %%xmm1, %%xmm0");
    }
    return;
  case ND_LT:
  case ND_LE:
    if (elem->size == 8 || elem->is_unsigned)
      break;
    if (node->kind == ND_LT) {
      // a < b is b > a
      println("  pcmpgt%s %%xmm0, %%xmm1", c);
      println("  movdqa %%xmm1, %%xmm0");
    } else {
      // a <= b is !(a > b)
      println("  pcmpgt%s %%xmm1, %%xmm0", c);
      all_ones(1);
      println("  pxor %%xmm1, %%xmm0");
    }
    return;
  }
  vector_by_element(node);
}

// -v and ~v. A floating-point element's sign bit is flipped, as for -x.
static void gen_vector_unary(Node *node) {
  Type *elem = node->ty->elem;
  if (node->kind == ND_BITNOT) {
    all_ones(1);
    println("  pxor %%xmm1, %%xmm0");
  } else if (is_flonum(elem)) {
    all_ones(1);
    println("  psll%s $%d, %%xmm1", int_suffix(elem->size), elem->size * 8 - 1);
    println("  pxor %%xmm1, %%xmm0");
  } else {
    println("  pxor %%xmm1, %%xmm1");
    println("  psub%s %%xmm0, %%xmm1", int_suffix(elem->size));
    println("  movdqa %%xmm1, %%xmm0");
  }
}

//---------- Constant folding ------------------------------------------------

// Integer expressions made only of constants are computed here, at
// compile time, into a single `mov $value, %rax`: `1000 * 1000` in a loop
// condition, sizes scaled for pointer arithmetic, and so on. The result
// must be exactly what the CPU would compute, so each step wraps around
// to the width of its type, and only operators that can't trap are
// folded: no division (by zero traps), no shifts by the width or more.

static bool is_foldable(Node *node) {
  // Integers, and integers cast to a pointer, like NULL: (void *)0.
  // (Some nodes, like ones that only zero memory, have no type.)
  if (!node->ty)
    return false;
  bool is_ptr_cast = node->kind == ND_CAST && node->ty->kind == TY_PTR;
  if ((!is_integer(node->ty) && !is_ptr_cast) || is_int128(node->ty))
    return false;

  switch (node->kind) {
  case ND_NUM:
    return true;
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_BITAND: case ND_BITOR:
  case ND_BITXOR: case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
    return is_foldable(node->lhs) && is_foldable(node->rhs);
  case ND_SHL:
  case ND_SHR:
    return is_foldable(node->lhs) && node->rhs->kind == ND_NUM &&
           node->rhs->val >= 0 && node->rhs->val < node->lhs->ty->size * 8;
  case ND_NEG: case ND_BITNOT: case ND_NOT: case ND_CAST:
    return is_foldable(node->lhs);
  }
  return false;
}

// `val` as a value of type `ty`: its low bits, sign- or zero-extended.
static int64_t wrap(uint64_t val, Type *ty) {
  switch (ty->size) {
  case 1: return ty->is_unsigned ? (int64_t)(uint8_t)val : (int8_t)val;
  case 2: return ty->is_unsigned ? (int64_t)(uint16_t)val : (int16_t)val;
  case 4: return ty->is_unsigned ? (int64_t)(uint32_t)val : (int32_t)val;
  }
  return val;
}

// The value of `node`, which is_foldable().
static int64_t fold(Node *node) {
  Type *ty = node->ty;

  switch (node->kind) {
  case ND_NUM:
    return wrap(node->val, ty);
  case ND_CAST:
    if (ty->kind == TY_BOOL)
      return fold(node->lhs) != 0;
    return wrap(fold(node->lhs), ty);
  case ND_NEG:
    return wrap(-(uint64_t)fold(node->lhs), ty);
  case ND_BITNOT:
    return wrap(~(uint64_t)fold(node->lhs), ty);
  case ND_NOT:
    return !fold(node->lhs);
  }

  // Binary operators. Both operands have the same type here.
  uint64_t l = fold(node->lhs), r = fold(node->rhs);
  bool is_unsigned = node->lhs->ty->is_unsigned;

  switch (node->kind) {
  case ND_ADD: return wrap(l + r, ty);
  case ND_SUB: return wrap(l - r, ty);
  case ND_MUL: return wrap(l * r, ty);
  case ND_BITAND: return wrap(l & r, ty);
  case ND_BITOR: return wrap(l | r, ty);
  case ND_BITXOR: return wrap(l ^ r, ty);
  case ND_SHL: return wrap(l << r, ty);
  case ND_SHR: return wrap(is_unsigned ? l >> r : (uint64_t)((int64_t)l >> r), ty);
  case ND_EQ: return l == r;
  case ND_NE: return l != r;
  case ND_LT: return is_unsigned ? l < r : (int64_t)l < (int64_t)r;
  case ND_LE: return is_unsigned ? l <= r : (int64_t)l <= (int64_t)r;
  }
  unreachable();
}

//---------- Simple operands -------------------------------------------------

// An integer or a pointer in %rax alone
static bool is_int_or_ptr(Type *ty) {
  return (is_integer(ty) && ty->kind != TY_BOOL && !is_int128(ty)) ||
         ty->kind == TY_PTR;
}

// A local scalar variable, whose value can be read straight from the
// stack frame. Returns NULL for anything else.
static Obj *local_scalar(Node *node) {
  if (node->kind == ND_VAR && node->var->is_local && !is_aggregate(node->ty) &&
      !node->var->is_overaligned)
    return node->var;
  return NULL;
}

// Does gen_expr() compute `node` by loading a signed int (with movsxd,
// see load_from()), so its value is already correct as 64 bits?
static bool is_int_load(Node *node) {
  if (node->ty->kind != TY_INT || node->ty->is_unsigned)
    return false;
  if (node->kind == ND_VAR)
    return local_scalar(node) || !node->var->is_local;
  if (node->kind == ND_DEREF)
    return true;
  if (node->kind == ND_MEMBER)
    return !node->member->is_bitfield;
  return false;
}

// Some operands can be loaded into %rdi with a single instruction: an
// integer constant that fits in 32 bits, or an integer or pointer local
// variable. For `a + 5` or `a < n`, mucc then computes `a` and loads the
// right side straight into %rdi, instead of computing the right side,
// pushing it, computing the left side and popping it back. Neither kind
// of operand has side effects, so evaluating it last is safe.
//
// Returns the instruction that loads `node` into %rdi, or NULL. The
// string is overwritten by the next call.
static char *simple_operand(Node *node) {
  static char buf[64];

  // Look through casts that emit no code, like int -> unsigned int or
  // long -> int (which just uses the low 32 bits).
  if (node->kind == ND_CAST && is_int_or_ptr(node->ty) &&
      is_int_or_ptr(node->lhs->ty) &&
      !cast_table[getTypeId(node->lhs->ty)][getTypeId(node->ty)])
    node = node->lhs;

  // So do int locals converted to 64 bits: loading an int already
  // sign-extends it (movsxd).
  if (node->kind == ND_CAST && node->ty->size == 8 && is_int_or_ptr(node->ty) &&
      is_int_load(node->lhs) && node->lhs->kind == ND_VAR)
    node = node->lhs;

  // Integer constant (see "Constant folding") that fits in 32 bits, which
  // mov sign-extends to 64.
  if (is_foldable(node)) {
    int64_t val = fold(node);
    if (val != (int32_t)val)
      return NULL;
    snprintf(buf, sizeof(buf), "  mov $%ld, %%rdi", val);
    return buf;
  }

  // Local variable, loaded the same way load() would, but into %rdi.
  Obj *var = local_scalar(node);
  if (!var || !is_int_or_ptr(var->ty))
    return NULL;

  char *insn = var->ty->is_unsigned ? "movz" : "movs";
  char *src = var_operand(var);
  if (var->ty->size == 1)
    snprintf(buf, sizeof(buf), "  %sbl %s, %%edi", insn, src);
  else if (var->ty->size == 2)
    snprintf(buf, sizeof(buf), "  %swl %s, %%edi", insn, src);
  else if (var->ty->size == 4)
    snprintf(buf, sizeof(buf), "  movsxd %s, %%rdi", src);
  else
    snprintf(buf, sizeof(buf), "  mov %s, %%rdi", src);
  return buf;
}

//---------- Function calls (System V ABI) -----------------------------------

// Structs or unions equal or smaller than 16 bytes are passed
// using up to two registers.
//
// If the first 8 bytes contains only floating-point type members,
// they are passed in an XMM register. Otherwise, they are passed
// in a general-purpose register.
//
// If a struct/union is larger than 8 bytes, the same rule is
// applied to the the next 8 byte chunk.
//
// This function returns true if `ty` has only floating-point
// members in its byte range [lo, hi).
static bool has_flonum(Type *ty, int lo, int hi, int offset) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (Member *mem = ty->members; mem; mem = mem->next)
      if (!has_flonum(mem->ty, lo, hi, offset + mem->offset))
        return false;
    return true;
  }

  if (ty->kind == TY_ARRAY) {
    for (int i = 0; i < ty->array_len; i++)
      if (!has_flonum(ty->base, lo, hi, offset + ty->base->size * i))
        return false;
    return true;
  }

  // A scalar outside the range, or a float or double. (A 16-byte one,
  // like __int128, reaches into the upper half from offset 0.)
  return offset + ty->size <= lo || hi <= offset ||
         ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || ty->kind == TY_VECTOR;
}

// Does `ty` have anything (not only padding) in its byte range [lo, hi)?
static bool has_data(Type *ty, int lo, int hi, int offset) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (Member *mem = ty->members; mem; mem = mem->next)
      if (has_data(mem->ty, lo, hi, offset + mem->offset))
        return true;
    return false;
  }
  if (ty->kind == TY_ARRAY)
    return ty->array_len > 0 && offset < hi &&
           offset + ty->size > lo; // its elements fill it
  return offset < hi && offset + ty->size > lo;
}

// Is a struct or union of 16 bytes or less passed in two registers? Not
// if its upper 8 bytes are only padding, as in
// `struct { float f; } __attribute__((aligned(16)))`: they take none.
static bool has_two_parts(Type *ty) {
  return ty->size > 8 && has_data(ty, 8, 16, 0);
}

static bool has_flonum1(Type *ty) {
  return has_flonum(ty, 0, 8, 0);
}

static bool has_flonum2(Type *ty) {
  return has_flonum(ty, 8, 16, 0);
}

// A struct or union of only a 16-byte vector, as `struct { __m128 v; }`,
// is passed and returned whole in one XMM register (the psABI's classes
// SSE and SSEUP); any other one of 16 bytes, a half in each of two.
static bool is_vector16(Type *ty) {
  if (ty->kind == TY_VECTOR)
    return ty->size == 16;
  if ((ty->kind != TY_STRUCT && ty->kind != TY_UNION) || ty->size != 16 || !ty->members)
    return false;
  for (Member *mem = ty->members; mem; mem = mem->next)
    if (!is_vector16(mem->ty))
      return false;
  return true;
}

// Counts the registers a struct or union of 16 bytes or less needs: one
// per 8-byte half, an XMM register if that half holds only floating-point
// values, otherwise a general-purpose one.
static void struct_regs(Type *ty, int *ngp, int *nfp) {
  *ngp = *nfp = 0;
  if (is_vector16(ty)) {
    *nfp = 1;
    return;
  }
  if (has_flonum1(ty))
    (*nfp)++;
  else
    (*ngp)++;

  if (has_two_parts(ty)) {
    if (has_flonum2(ty))
      (*nfp)++;
    else
      (*ngp)++;
  }
}

// Is a struct or union passed in registers, when `gp` general-purpose and
// `fp` XMM registers are already taken (these count past the last one
// once arguments go on the stack)? It goes either entirely in registers
// or entirely on the stack, and only the kinds of registers it needs
// matter. The caller and the callee must both decide this the same way,
// so both use this function.
static bool struct_in_regs(Type *ty, int gp, int fp) {
  // An empty struct (GNU) takes no register, and no stack either (its
  // size rounds up to 0 bytes there). One holding a long double goes on
  // the stack.
  if (ty->size > 16 || ty->size == 0 || has_ldouble(ty) || has_unaligned_member(ty))
    return false;
  int ngp, nfp;
  struct_regs(ty, &ngp, &nfp);
  return (!ngp || gp + ngp <= GP_MAX) && (!nfp || fp + nfp <= FP_MAX);
}

// va_arg(ap, ty): the address of the next variadic argument, found the
// way the caller passed it (see push_args). An argument that went in
// registers is in the register save area the prologue filled, at
// gp_offset or fp_offset; one on the stack is at overflow_arg_area. A
// struct in two registers is copied back together into node->var.
static void gen_va_arg(Node *node) {
  Type *ty = node->ty->base;
  int c = count();
  gen_expr(node->lhs);
  println("  mov %%rax, %%rcx"); // the va_list

  int ngp = 0, nfp = 0;
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    if (ty->size <= 16 && ty->size && !has_ldouble(ty) && !has_unaligned_member(ty))
      struct_regs(ty, &ngp, &nfp);
  } else if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || is_vector(ty)) {
    nfp = 1;
  } else if (ty->kind != TY_LDOUBLE) {
    ngp = is_int128(ty) ? 2 : 1;
  }

  if (ngp + nfp) {
    // Only if all of it fits in the registers left
    if (ngp) {
      println("  cmpl $%d, (%%rcx)", 48 - ngp * 8);
      println("  ja .L.va_stack.%d", c);
    }
    if (nfp) {
      println("  cmpl $%d, 4(%%rcx)", 176 - nfp * 16);
      println("  ja .L.va_stack.%d", c);
    }

    // Each 8-byte part, from a general-purpose or an XMM register
    int nparts = (ty->kind == TY_STRUCT || ty->kind == TY_UNION) && !is_vector16(ty)
                   ? 1 + has_two_parts(ty) : ngp + nfp;
    for (int i = 0; i < nparts; i++) {
      bool fp = ty->kind == TY_STRUCT || ty->kind == TY_UNION
                  ? has_flonum(ty, i * 8, i * 8 + 8, 0) : nfp > 0;
      println("  movl %d(%%rcx), %%eax", fp ? 4 : 0);
      println("  add 16(%%rcx), %%rax");
      println("  addl $%d, %d(%%rcx)", fp ? 16 : 8, fp ? 4 : 0);
      if (nparts == 1)
        break;
      println("  mov (%%rax), %%rdx");
      println("  mov %%rdx, %d(%%rbp)", node->var->offset + i * 8);
      if (i == 1)
        println("  lea %d(%%rbp), %%rax", node->var->offset);
    }
    println("  jmp .L.va_end.%d", c);
  }

  // On the stack: aligned to 16 if its type is, and taking a multiple of
  // 8 bytes
  println(".L.va_stack.%d:", c);
  println("  mov 8(%%rcx), %%rax");
  if (ty->align > 8) {
    println("  add $15, %%rax");
    println("  and $-16, %%rax");
  }
  println("  lea %d(%%rax), %%rdx", align_to(ty->size, 8));
  println("  mov %%rdx, 8(%%rcx)");
  println(".L.va_end.%d:", c);
}

static void push_struct(Type *ty) {
  int sz = align_to(ty->size, 8);
  println("  sub $%d, %%rsp", sz);
  depth += sz / 8;

  int i = 0;
  for (; i + 8 <= ty->size; i += 8) {
    println("  mov %d(%%rax), %%r10", i);
    println("  mov %%r10, %d(%%rsp)", i);
  }
  for (; i < ty->size; i++) {
    println("  mov %d(%%rax), %%r10b", i);
    println("  mov %%r10b, %d(%%rsp)", i);
  }
}

static void push_args2(Node *args, bool first_pass) {
  if (!args)
    return;
  push_args2(args->next, first_pass);

  if ((first_pass && !args->pass_by_stack) || (!first_pass && args->pass_by_stack))
    return;

  gen_expr(args);

  switch (args->ty->kind) {
  case TY_STRUCT:
  case TY_UNION:
    push_struct(args->ty);
    break;
  case TY_FLOAT:
  case TY_DOUBLE:
    pushf();
    break;
  case TY_LDOUBLE:
    println("  sub $16, %%rsp");
    println("  fstpt (%%rsp)");
    depth += 2;
    break;
  case TY_INT128:
    push128();
    break;
  case TY_VECTOR:
    push_vec(args->ty);
    break;
  default:
    push();
  }

  // Padding between it and the argument before it (see push_args)
  if (args->stack_pad) {
    println("  sub $8, %%rsp");
    depth++;
  }
}

// Load function call arguments. Arguments are already evaluated and
// stored to the stack as local variables. What we need to do in this
// function is to load them to registers or push them to the stack as
// specified by the x86-64 psABI. Here is what the spec says:
//
// - Up to 6 arguments of integral type are passed using RDI, RSI,
//   RDX, RCX, R8 and R9.
//
// - Up to 8 arguments of floating-point type are passed using XMM0 to
//   XMM7.
//
// - If all registers of an appropriate type are already used, push an
//   argument to the stack in the right-to-left order.
//
// - Each argument passed on the stack takes 8 bytes, and the end of
//   the argument area must be aligned to a 16 byte boundary.
//
// - If a function is variadic, set the number of floating-point type
//   arguments to RAX.
static int push_args(Node *node) {
  int stack = 0, gp = 0, fp = 0;

  // If the return type is a large struct/union, the caller passes
  // a pointer to a buffer as if it were the first argument.
  if (node->ret_buffer && is_ret_in_memory(node->ty))
    gp++;

  // Load as many arguments to the registers as possible.
  for (Node *arg = node->args; arg; arg = arg->next) {
    Type *ty = arg->ty;

    switch (ty->kind) {
    case TY_STRUCT:
    case TY_UNION:
      if (struct_in_regs(ty, gp, fp)) {
        int ngp, nfp;
        struct_regs(ty, &ngp, &nfp);
        gp += ngp;
        fp += nfp;
      } else {
        // On the stack, 16-byte aligned if its type is
        arg->pass_by_stack = true;
        arg->stack_pad = ty->align > 8 && stack % 2;
        stack += arg->stack_pad + align_to(ty->size, 8) / 8;
      }
      break;
    case TY_FLOAT:
    case TY_DOUBLE:
      if (fp++ >= FP_MAX) {
        arg->pass_by_stack = true;
        stack++;
      }
      break;
    case TY_LDOUBLE:
      arg->pass_by_stack = true;
      arg->stack_pad = stack % 2;
      stack += arg->stack_pad + 2;
      break;
    case TY_INT128:
      // Both halves in registers, or all of it on the stack
      if (gp + 2 <= GP_MAX) {
        gp += 2;
      } else {
        arg->pass_by_stack = true;
        arg->stack_pad = stack % 2;
        stack += arg->stack_pad + 2;
      }
      break;
    case TY_VECTOR:
      // On the stack, one of 16 bytes is 16-byte aligned
      if (fp++ >= FP_MAX) {
        arg->pass_by_stack = true;
        arg->stack_pad = ty->size == 16 && stack % 2;
        stack += arg->stack_pad + (ty->size == 16 ? 2 : 1);
      }
      break;
    default:
      if (gp++ >= GP_MAX) {
        arg->pass_by_stack = true;
        stack++;
      }
    }
  }

  if ((depth + stack) % 2 == 1) {
    println("  sub $8, %%rsp");
    depth++;
    stack++;
  }

  push_args2(node->args, true);
  push_args2(node->args, false);

  // If the return type is a large struct/union, the caller passes
  // a pointer to a buffer as if it were the first argument.
  if (node->ret_buffer && is_ret_in_memory(node->ty)) {
    addr_of_local(node->ret_buffer, "%rax");
    push();
  }

  return stack;
}

static void copy_ret_buffer(Obj *var) {
  Type *ty = var->ty;
  int gp = 0, fp = 0;

  if (!ty->size) // an empty struct (GNU) comes back in no register
    return;

  if (is_vector16(ty)) {
    println("  movdqu %%xmm0, %d(%%rbp)", var->offset);
    return;
  }

  // One that is only a long double comes back in %st0, a long double
  // _Complex in %st0 and %st1.
  if (has_ldouble(ty)) {
    println("  fstpt %d(%%rbp)", var->offset);
    if (is_complex(ty))
      println("  fstpt %d(%%rbp)", var->offset + 16);
    return;
  }

  if (has_flonum1(ty)) {
    assert(ty->size == 4 || 8 <= ty->size);
    if (ty->size == 4)
      println("  movss %%xmm0, %d(%%rbp)", var->offset);
    else
      println("  movsd %%xmm0, %d(%%rbp)", var->offset);
    fp++;
  } else {
    for (int i = 0; i < MIN(8, ty->size); i++) {
      println("  mov %%al, %d(%%rbp)", var->offset + i);
      println("  shr $8, %%rax");
    }
    gp++;
  }

  if (has_two_parts(ty)) {
    if (has_flonum2(ty)) {
      assert(ty->size == 12 || ty->size == 16);
      if (ty->size == 12)
        println("  movss %%xmm%d, %d(%%rbp)", fp, var->offset + 8);
      else
        println("  movsd %%xmm%d, %d(%%rbp)", fp, var->offset + 8);
    } else {
      char *reg1 = (gp == 0) ? "%al" : "%dl";
      char *reg2 = (gp == 0) ? "%rax" : "%rdx";
      for (int i = 8; i < MIN(16, ty->size); i++) {
        println("  mov %s, %d(%%rbp)", reg1, var->offset + i);
        println("  shr $8, %s", reg2);
      }
    }
  }
}

static void copy_struct_reg(void) {
  Type *ty = current_fn->ty->return_ty;
  int gp = 0, fp = 0;

  if (!ty->size) // an empty struct (GNU) goes back in no register
    return;

  if (has_ldouble(ty)) {
    if (is_complex(ty))
      println("  fldt 16(%%rax)");
    println("  fldt (%%rax)");
    return;
  }

  if (is_vector16(ty)) {
    println("  movdqu (%%rax), %%xmm0");
    return;
  }

  println("  mov %%rax, %%rdi");

  if (has_flonum(ty, 0, 8, 0)) {
    assert(ty->size == 4 || 8 <= ty->size);
    if (ty->size == 4)
      println("  movss (%%rdi), %%xmm0");
    else
      println("  movsd (%%rdi), %%xmm0");
    fp++;
  } else {
    println("  mov $0, %%rax");
    for (int i = MIN(8, ty->size) - 1; i >= 0; i--) {
      println("  shl $8, %%rax");
      println("  mov %d(%%rdi), %%al", i);
    }
    gp++;
  }

  if (has_two_parts(ty)) {
    if (has_flonum(ty, 8, 16, 0)) {
      assert(ty->size == 12 || ty->size == 16);
      if (ty->size == 4)
        println("  movss 8(%%rdi), %%xmm%d", fp);
      else
        println("  movsd 8(%%rdi), %%xmm%d", fp);
    } else {
      char *reg1 = (gp == 0) ? "%al" : "%dl";
      char *reg2 = (gp == 0) ? "%rax" : "%rdx";
      println("  mov $0, %s", reg2);
      for (int i = MIN(16, ty->size) - 1; i >= 8; i--) {
        println("  shl $8, %s", reg2);
        println("  mov %d(%%rdi), %s", i, reg1);
      }
    }
  }
}

static void copy_struct_mem(void) {
  Type *ty = current_fn->ty->return_ty;
  Obj *var = current_fn->params; // the hidden pointer to the return buffer

  println("  mov %s, %%rdi", var_operand(var));

  int i = 0;
  for (; i + 8 <= ty->size; i += 8) {
    println("  mov %d(%%rax), %%rdx", i);
    println("  mov %%rdx, %d(%%rdi)", i);
  }
  for (; i < ty->size; i++) {
    println("  mov %d(%%rax), %%dl", i);
    println("  mov %%dl, %d(%%rdi)", i);
  }

  // As the psABI says, the buffer's address comes back in %rax: a caller
  // reads the value from there.
  println("  mov %%rdi, %%rax");
}

static void builtin_alloca(void) {
  // Align size to 16 bytes.
  println("  add $15, %%rdi");
  println("  and $0xfffffff0, %%edi");

  // Shift the temporary area by %rdi.
  println("  mov %d(%%rbp), %%rcx", current_fn->alloca_bottom->offset);
  println("  sub %%rsp, %%rcx");
  println("  mov %%rsp, %%rax");
  println("  sub %%rdi, %%rsp");
  println("  mov %%rsp, %%rdx");
  println("1:");
  println("  cmp $0, %%rcx");
  println("  je 2f");
  println("  mov (%%rax), %%r8b");
  println("  mov %%r8b, (%%rdx)");
  println("  inc %%rdx");
  println("  inc %%rax");
  println("  dec %%rcx");
  println("  jmp 1b");
  println("2:");

  // Move alloca_bottom pointer.
  println("  mov %d(%%rbp), %%rax", current_fn->alloca_bottom->offset);
  println("  sub %%rdi, %%rax");
  println("  mov %%rax, %d(%%rbp)", current_fn->alloca_bottom->offset);
}

//---------- Operands, branches and unused values ----------------------------

static bool is_commutative(Node *node) {
  switch (node->kind) {
  case ND_ADD: case ND_MUL: case ND_BITAND: case ND_BITOR: case ND_BITXOR:
  case ND_EQ: case ND_NE:
    return true;
  }
  return false;
}

// Computes a binary operator's lhs into %rax and rhs into %rdi (or, for
// `a + b` where only a is simple, b into %rax and a into %rdi).
static void gen_operands(Node *node) {
  if (!simple_operand(node->rhs) && simple_operand(node->lhs) && is_commutative(node)) {
    gen_expr(node->rhs);
    println("%s", simple_operand(node->lhs));
    return;
  }

  if (simple_operand(node->rhs)) {
    gen_expr(node->lhs);
    println("%s", simple_operand(node->rhs));
  } else {
    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");
  }
}

// Whether a binary operator on `ty` operands works on 64-bit registers.
static bool is_64bit(Type *ty) {
  return ty->kind == TY_LONG || ty->base;
}

// The condition code (as in jCC) under which comparison `node` is true,
// or false if `when` is false.
static char *condition(Node *node, bool when) {
  bool u = node->lhs->ty->is_unsigned;
  switch (node->kind) {
  case ND_EQ: return when ? "e" : "ne";
  case ND_NE: return when ? "ne" : "e";
  case ND_LT: return when ? (u ? "b" : "l") : (u ? "ae" : "ge");
  case ND_LE: return when ? (u ? "be" : "le") : (u ? "a" : "g");
  }
  unreachable();
}

// Jumps to `label` if `cond` is true (or, if `when` is false, if it's
// false). Comparisons, !, && and || jump on the CPU flags directly,
// instead of making a 0 or 1 in %rax and testing that.
static void gen_branch(Node *cond, bool when, char *label) {
  switch (cond->kind) {
  case ND_NOT:
    gen_branch(cond->lhs, !when, label);
    return;
  case ND_LOGAND:
  case ND_LOGOR:
    // Jumping when `a && b` is true needs both; when it's false, either
    // one will do (and the reverse for ||).
    if ((cond->kind == ND_LOGAND) == when) {
      char *skip = format(".L.skip.%d", count());
      gen_branch(cond->lhs, !when, skip);
      gen_branch(cond->rhs, when, label);
      println("%s:", skip);
    } else {
      gen_branch(cond->lhs, when, label);
      gen_branch(cond->rhs, when, label);
    }
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    if (is_flonum(cond->lhs->ty) || is_int128(cond->lhs->ty))
      break;
    gen_operands(cond);
    if (is_64bit(cond->lhs->ty))
      println("  cmp %%rdi, %%rax");
    else
      println("  cmp %%edi, %%eax");
    println("  j%s %s", condition(cond, when), label);
    return;
  }

  gen_expr(cond);
  cmp_zero(cond->ty);
  println("  %s %s", when ? "jne" : "je", label);
}

// Computes `node` only for its side effects, as in an expression
// statement. Casts, and adding a constant, change nothing else, so they
// are skipped: `x++;` becomes just `x += 1`, without computing x's old
// value. (Only for integers and pointers: a long double must still be
// popped off the x87 stack, where every long double expression leaves
// its value.)
static void gen_discard(Node *node) {
  for (;;) {
    if (node->kind == ND_CAST && is_int_or_ptr(node->lhs->ty)) {
      node = node->lhs;
      continue;
    }
    if ((node->kind == ND_ADD || node->kind == ND_SUB) &&
        is_int_or_ptr(node->ty) && is_int_or_ptr(node->lhs->ty) &&
        is_foldable(node->rhs)) {
      node = node->lhs;
      continue;
    }
    break;
  }

  if (node->kind == ND_COMMA) {
    gen_discard(node->lhs);
    gen_discard(node->rhs);
    return;
  }
  gen_expr(node);
  if (node->ty && node->ty->kind == TY_LDOUBLE)
    println("  fstp %%st(0)");
}

//---------- Expressions -----------------------------------------------------

// The number of the file `tok` is in, as .file and .loc name it.
static int file_number(Token *tok) {
  int file_no = tok->file->file_no;

  // A name given by #line or a line marker (in -E output compiled again)
  // gets a file number of its own, after the files actually read.
  if (tok->filename && strcmp(tok->filename, tok->file->name)) {
    static HashMap names;
    static int last_no;
    file_no = (int)(intptr_t)hashmap_get(&names, tok->filename);
    if (!file_no) {
      if (!last_no)
        while (get_input_files()[last_no])
          last_no++;
      file_no = ++last_no;
      hashmap_put(&names, tok->filename, (void *)(intptr_t)file_no);
      println("  .file %d \"%s\"", file_no, tok->filename);
    }
  }
  return file_no;
}

// Tells the assembler which source line the next instructions come
// from, for debuggers. Most expressions share a line with the statement
// around them, so only emit a .loc when the line actually changes. Code
// from a macro is on the line the macro is used on, as with gcc, not in
// the header that defines it.
static void emit_loc(Token *tok) {
  while (tok->origin)
    tok = tok->origin;
  int file_no = file_number(tok);
  if (file_no == loc_file && tok->line_no == loc_line)
    return;
  loc_file = file_no;
  loc_line = tok->line_no;
  println("  .loc %d %d", loc_file, loc_line);
}

// Generate code for a given node.
static void gen_expr(Node *node) {
  emit_loc(node->tok);

  if (node->kind != ND_NUM && is_foldable(node)) {
    println("  mov $%ld, %%rax", fold(node));
    return;
  }

  // gen_expr() has three parts, in this order: the node kinds that aren't
  // binary operators (this switch: values, memory, ?:, && and ||, calls,
  // builtins and atomics), then binary operators on floating-point
  // operands, then binary operators on integers and pointers.
  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM: {
    switch (node->ty->kind) {
    case TY_FLOAT: {
      union { float f32; uint32_t u32; } u = { *node->fval };
      println("  mov $%u, %%eax  # float %Lf", u.u32, *node->fval);
      println("  movq %%rax, %%xmm0");
      return;
    }
    case TY_DOUBLE: {
      union { double f64; uint64_t u64; } u = { *node->fval };
      println("  mov $%lu, %%rax  # double %Lf", u.u64, *node->fval);
      println("  movq %%rax, %%xmm0");
      return;
    }
    case TY_LDOUBLE: {
      union { long double f80; uint64_t u64[2]; } u;
      memset(&u, 0, sizeof(u));
      u.f80 = *node->fval;
      println("  mov $%lu, %%rax  # long double %Lf", u.u64[0], *node->fval);
      println("  mov %%rax, -16(%%rsp)");
      println("  mov $%lu, %%rax", u.u64[1]);
      println("  mov %%rax, -8(%%rsp)");
      println("  fldt -16(%%rsp)");
      return;
    }
    }

    println("  mov $%ld, %%rax", node->val);
    if (is_int128(node->ty))
      println("  cqo");
    return;
  }
  case ND_NEG:
    gen_expr(node->lhs);
    if (is_vector(node->ty)) {
      gen_vector_unary(node);
      return;
    }

    switch (node->ty->kind) {
    case TY_FLOAT:
      println("  mov $1, %%rax");
      println("  shl $31, %%rax");
      println("  movq %%rax, %%xmm1");
      println("  xorps %%xmm1, %%xmm0");
      return;
    case TY_DOUBLE:
      println("  mov $1, %%rax");
      println("  shl $63, %%rax");
      println("  movq %%rax, %%xmm1");
      println("  xorpd %%xmm1, %%xmm0");
      return;
    case TY_LDOUBLE:
      println("  fchs");
      return;
    case TY_INT128:
      neg128("%rax", "%rdx");
      return;
    }

    println("  neg %%rax");
    return;
  case ND_VAR:
    if (local_scalar(node)) {
      load_from(node->ty, var_operand(node->var));
      return;
    }
    gen_addr(node);
    load(node->ty);
    return;
  case ND_MEMBER: {
    Member *mem = node->member;

    // A scalar member is loaded from base + offset in one instruction,
    // e.g. `mov 8(%rax), %rax` for p->x, or `mov -24(%rbp), %rax` for
    // s.x when s is a local struct.
    if (is_aggregate(node->ty)) {
      gen_addr(node);
    } else {
      char addr[32];
      Node *base = node->lhs;
      if (base->kind == ND_VAR && base->var->is_local && base->ty->kind != TY_VLA &&
          !base->var->is_overaligned) {
        snprintf(addr, sizeof(addr), "%d(%%rbp)", base->var->offset + mem->offset);
      } else {
        gen_addr(base);
        snprintf(addr, sizeof(addr), "%d(%%rax)", mem->offset);
      }
      // A bit-field's whole unit, not its promoted type (see add_type())
      if (mem->is_bitfield)
        load_unit(mem->unit, addr);
      else
        load_from(node->ty, addr);
    }

    if (mem->is_bitfield) {
      println("  shl $%d, %%rax", 64 - mem->bit_width - mem->bit_offset);
      if (mem->ty->is_unsigned || mem->ty->kind == TY_BOOL) // a _Bool is 0 or 1
        println("  shr $%d, %%rax", 64 - mem->bit_width);
      else
        println("  sar $%d, %%rax", 64 - mem->bit_width);
    }
    return;
  }
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    // Storing to a local scalar needs no address computation.
    if (local_scalar(node->lhs)) {
      gen_expr(node->rhs);
      store_to(node->ty, var_operand(node->lhs->var));
      if (node->lhs->ty->is_atomic)
        println("  mfence"); // see below
      return;
    }

    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);

    if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
      println("  mov %%rax, %%r8");

      // If the lhs is a bitfield, we need to read the current value
      // from memory and merge it with a new value.
      Member *mem = node->lhs->member;
      // The mask goes through a register: `and` only takes 32-bit
      // immediates, too small for the mask of a 32-bit-wide field.
      uint64_t bits = mem->bit_width == 64 ? -1 : (1UL << mem->bit_width) - 1;
      println("  mov %%rax, %%rdi");
      println("  mov $%ld, %%r9", (long)bits);
      println("  and %%r9, %%rdi");
      println("  shl $%d, %%rdi", mem->bit_offset);

      println("  mov (%%rsp), %%rax");
      load_unit(mem->unit, "(%rax)");

      println("  mov $%ld, %%r9", (long)~(bits << mem->bit_offset));
      println("  and %%r9, %%rax");
      println("  or %%rdi, %%rax");
      pop("%rdi");
      store_unit(mem->unit); // the whole unit loaded, not the promoted type

      // The assignment's value is what the field now holds: the new
      // value cut to its width, sign- or zero-extended.
      println("  mov %%r8, %%rax");
      println("  shl $%d, %%rax", 64 - mem->bit_width);
      if (mem->ty->is_unsigned || mem->ty->kind == TY_BOOL)
        println("  shr $%d, %%rax", 64 - mem->bit_width);
      else
        println("  sar $%d, %%rax", 64 - mem->bit_width);
      return;
    }

    store(node->ty);

    // A store to an atomic object is sequentially consistent: no later
    // load may be done before it, which x86 allows without a fence.
    if (node->lhs->ty->is_atomic)
      println("  mfence");
    return;
  case ND_STMT_EXPR: {
    // The value of the last statement, if it's an expression, is the
    // result, so it's computed with gen_expr, not discarded.
    bool labeled = block_begin(node);
    for (Node *n = node->body; n; n = n->next) {
      if (!n->next && n->kind == ND_EXPR_STMT)
        gen_expr(n->lhs);
      else
        gen_stmt(n);
    }
    if (labeled)
      block_end(node);
    return;
  }
  case ND_COMMA:
    gen_discard(node->lhs);
    gen_expr(node->rhs);
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    // An int load is already sign-extended to 64 bits.
    if (is_int_load(node->lhs) && node->ty->size == 8 && is_int_or_ptr(node->ty))
      return;
    cast(node->lhs->ty, node->ty);
    return;
  case ND_MEMZERO:
    if (node->var->reg) {
      println("  mov $0, %s", regs64[node->var->reg - 1]);
      return;
    }

    // `rep stosb` is equivalent to `memset(%rdi, %al, %rcx)`.
    println("  mov $%d, %%rcx", node->var->ty->size);
    addr_of_local(node->var, "%rdi");
    println("  mov $0, %%al");
    println("  rep stosb");
    return;
  case ND_COND: {
    int c = count();
    gen_branch(node->cond, false, format(".L.else.%d", c));
    gen_expr(node->then);
    println("  jmp .L.end.%d", c);
    println(".L.else.%d:", c);
    gen_expr(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  sete %%al");
    println("  movzx %%al, %%rax");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    if (is_vector(node->ty)) {
      gen_vector_unary(node);
      return;
    }
    println("  not %%rax");
    if (is_int128(node->ty))
      println("  not %%rdx");
    return;
  case ND_LOGAND: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  je .L.false.%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("  je .L.false.%d", c);
    println("  mov $1, %%rax");
    println("  jmp .L.end.%d", c);
    println(".L.false.%d:", c);
    println("  mov $0, %%rax");
    println(".L.end.%d:", c);
    return;
  }
  case ND_LOGOR: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  jne .L.true.%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("  jne .L.true.%d", c);
    println("  mov $0, %%rax");
    println("  jmp .L.end.%d", c);
    println(".L.true.%d:", c);
    println("  mov $1, %%rax");
    println(".L.end.%d:", c);
    return;
  }
  case ND_FUNCALL: {
    if (node->lhs->kind == ND_VAR && !strcmp(node->lhs->var->name, "alloca")) {
      gen_expr(node->args);
      println("  mov %%rax, %%rdi");
      builtin_alloca();
      return;
    }

    int stack_args = push_args(node);

    // Calling a function by name is a direct `call f`. Anything else,
    // like a function pointer, is computed into %rax now, before the
    // argument registers are loaded, and called through %r10.
    bool direct = node->lhs->kind == ND_VAR && node->lhs->ty->kind == TY_FUNC;
    if (!direct)
      gen_expr(node->lhs);

    int gp = 0, fp = 0;

    // If the return type is a large struct/union, the caller passes
    // a pointer to a buffer as if it were the first argument.
    if (node->ret_buffer && is_ret_in_memory(node->ty))
      pop(argreg64[gp++]);

    for (Node *arg = node->args; arg; arg = arg->next) {
      Type *ty = arg->ty;

      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (arg->pass_by_stack)
          continue;

        if (is_vector16(ty)) {
          println("  movdqu (%%rsp), %%xmm%d", fp++);
          println("  add $16, %%rsp");
          depth -= 2;
          break;
        }

        if (has_flonum1(ty))
          popf(fp++);
        else
          pop(argreg64[gp++]);

        if (has_two_parts(ty)) {
          if (has_flonum2(ty))
            popf(fp++);
          else
            pop(argreg64[gp++]);
        } else if (ty->size > 8) {
          // Padding, pushed but in no register
          println("  add $8, %%rsp");
          depth--;
        }
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        if (fp < FP_MAX)
          popf(fp++);
        break;
      case TY_LDOUBLE:
        break;
      case TY_INT128:
        if (!arg->pass_by_stack) {
          pop(argreg64[gp++]);
          pop(argreg64[gp++]);
        }
        break;
      case TY_VECTOR:
        if (!arg->pass_by_stack)
          pop_vec(ty, fp++);
        break;
      default:
        if (gp < GP_MAX)
          pop(argreg64[gp++]);
      }
    }

    // %al tells a variadic callee how many vector registers hold arguments.
    if (direct) {
      println("  mov $%d, %%rax", fp);
      println("  call %s@PLT", node->lhs->var->name);
    } else {
      println("  mov %%rax, %%r10");
      println("  mov $%d, %%rax", fp);
      println("  call *%%r10");
    }
    if (stack_args)
      println("  add $%d, %%rsp", stack_args * 8);

    depth -= stack_args;

    // It looks like the most significant 48 or 56 bits in RAX may
    // contain garbage if a function return type is short or bool/char,
    // respectively. We clear the upper bits here.
    switch (node->ty->kind) {
    case TY_BOOL:
      println("  movzx %%al, %%eax");
      return;
    case TY_CHAR:
      if (node->ty->is_unsigned)
        println("  movzbl %%al, %%eax");
      else
        println("  movsbl %%al, %%eax");
      return;
    case TY_SHORT:
      if (node->ty->is_unsigned)
        println("  movzwl %%ax, %%eax");
      else
        println("  movswl %%ax, %%eax");
      return;
    }

    // If the return type is a small struct, a value is returned
    // using up to two registers.
    if (node->ret_buffer && !is_ret_in_memory(node->ty)) {
      copy_ret_buffer(node->ret_buffer);
      println("  lea %d(%%rbp), %%rax", node->ret_buffer->offset);
    }

    return;
  }
  case ND_LABEL_VAL:
    println("  lea %s(%%rip), %%rax", node->unique_label);
    return;
  case ND_UNREACHABLE:
    println("  ud2");
    return;
  case ND_CLZ:
    // bsr finds the highest one bit; its index from the top is the count.
    // (As with gcc, the result for 0 is undefined.)
    gen_expr(node->lhs);
    if (node->lhs->ty->size == 8) {
      println("  bsr %%rax, %%rax");
      println("  xor $63, %%eax");
    } else {
      println("  bsr %%eax, %%eax");
      println("  xor $31, %%eax");
    }
    return;
  case ND_CTZ:
    gen_expr(node->lhs);
    if (node->lhs->ty->size == 8)
      println("  bsf %%rax, %%rax");
    else
      println("  bsf %%eax, %%eax");
    return;
  case ND_POPCOUNT:
    // Counted in parallel within 2-, 4- and 8-bit fields, then summed by
    // a multiply, without popcnt, which older x86-64 CPUs lack.
    gen_expr(node->lhs);
    if (node->lhs->ty->size < 8)
      println("  mov %%eax, %%eax");
    println("  mov %%rax, %%rdx");
    println("  shr $1, %%rdx");
    println("  mov $%ld, %%rcx", 0x5555555555555555L);
    println("  and %%rcx, %%rdx");
    println("  sub %%rdx, %%rax");
    println("  mov $%ld, %%rcx", 0x3333333333333333L);
    println("  mov %%rax, %%rdx");
    println("  shr $2, %%rdx");
    println("  and %%rcx, %%rax");
    println("  and %%rcx, %%rdx");
    println("  add %%rdx, %%rax");
    println("  mov %%rax, %%rdx");
    println("  shr $4, %%rdx");
    println("  add %%rdx, %%rax");
    println("  mov $%ld, %%rcx", 0x0f0f0f0f0f0f0f0fL);
    println("  and %%rcx, %%rax");
    println("  mov $%ld, %%rcx", 0x0101010101010101L);
    println("  imul %%rcx, %%rax");
    println("  shr $56, %%rax");
    return;
  case ND_BSWAP:
    gen_expr(node->lhs);
    if (node->ty->size == 8) {
      println("  bswap %%rax");
    } else if (node->ty->size == 4) {
      println("  bswap %%eax");
    } else {
      println("  bswap %%eax");
      println("  shr $16, %%eax");
    }
    return;
  case ND_FENCE:
    println("  mfence");
    return;
  case ND_OVERFLOW: {
    // a and b are 64-bit (see gnu_builtin()), so a op b is computed
    // exactly in 128 bits, in %rdx:%rax. It fits *res's type if that
    // type's value of its low bits, extended back to 128 bits, is the
    // same. (Every exact result is less than 2^128 and at least -2^127,
    // so equal bits mean equal values.)
    bool signed_a = !node->lhs->ty->is_unsigned;
    bool signed_b = !node->rhs->ty->is_unsigned;
    gen_expr(node->cas_addr);
    push();
    gen_expr(node->lhs);
    push();
    gen_expr(node->rhs);
    println("  mov %%rax, %%rcx");
    pop("%rdi");
    println("  mov %%rdi, %%rax");

    if (node->val == ND_MUL) {
      // The unsigned product, less 2^64 * b for a negative a and
      // 2^64 * a for a negative b
      println("  mul %%rcx");
      if (signed_a) {
        println("  test %%rdi, %%rdi");
        println("  jns 1f");
        println("  sub %%rcx, %%rdx");
        println("1:");
      }
      if (signed_b) {
        println("  test %%rcx, %%rcx");
        println("  jns 1f");
        println("  sub %%rdi, %%rdx");
        println("1:");
      }
    } else {
      // Each operand's high 64 bits: copies of its sign, or 0
      if (signed_a) {
        println("  mov %%rdi, %%rdx");
        println("  sar $63, %%rdx");
      } else {
        println("  xor %%edx, %%edx");
      }
      if (signed_b) {
        println("  mov %%rcx, %%rsi");
        println("  sar $63, %%rsi");
      } else {
        println("  xor %%esi, %%esi");
      }
      if (node->val == ND_ADD) {
        println("  add %%rcx, %%rax");
        println("  adc %%rsi, %%rdx");
      } else {
        println("  sub %%rcx, %%rax");
        println("  sbb %%rsi, %%rdx");
      }
    }

    Type *ty = node->cas_addr->ty->base;
    pop("%rsi");
    println("  mov %s, (%%rsi)", reg_ax(ty->size));

    // *res's value, extended to 128 bits in %r9:%r8
    switch (ty->size) {
    case 1:
      println(ty->is_unsigned ? "  movzbl %%al, %%r8d" : "  movsbq %%al, %%r8");
      break;
    case 2:
      println(ty->is_unsigned ? "  movzwl %%ax, %%r8d" : "  movswq %%ax, %%r8");
      break;
    case 4:
      println(ty->is_unsigned ? "  mov %%eax, %%r8d" : "  movslq %%eax, %%r8");
      break;
    default:
      println("  mov %%rax, %%r8");
    }
    if (ty->is_unsigned) {
      println("  xor %%r9d, %%r9d");
    } else {
      println("  mov %%r8, %%r9");
      println("  sar $63, %%r9");
    }
    println("  xor %%rax, %%r8");
    println("  xor %%rdx, %%r9");
    println("  or %%r9, %%r8");
    println("  setne %%al");
    println("  movzbl %%al, %%eax");
    return;
  }
  case ND_FRAME_ADDR:
    // Every function keeps its caller's %rbp at 0(%rbp) (see the
    // prologue), so frames are followed up the chain.
    println("  mov %%rbp, %%rax");
    for (int i = 0; i < node->val; i++)
      println("  mov (%%rax), %%rax");
    return;
  case ND_VA_ARG:
    gen_va_arg(node);
    return;
  case ND_CAS: {
    // The values are compared and swapped as bits: a float or a double
    // goes through %rax too. A long double, 16 bytes, goes through
    // cmpxchg16b, which compares %rdx:%rax and stores %rcx:%rbx (%rbx
    // may hold a register variable, so it's saved).
    Type *ty = node->cas_addr->ty->base;
    int sz = ty->size;
    gen_expr(node->cas_addr);
    push();
    gen_expr(node->cas_new);

    if (ty->kind == TY_LDOUBLE) {
      println("  sub $16, %%rsp");
      println("  fstpt (%%rsp)");
      depth += 2;
      gen_expr(node->cas_old);
      println("  mov %%rax, %%r8");
      println("  mov %%rbx, %%r9");
      println("  mov (%%rsp), %%rbx");
      println("  mov 8(%%rsp), %%rcx");
      println("  add $16, %%rsp");
      depth -= 2;
      pop("%rdi"); // addr
      println("  mov (%%r8), %%rax");
      println("  mov 8(%%r8), %%rdx");
      println("  lock cmpxchg16b (%%rdi)");
      println("  mov %%r9, %%rbx");
      println("  sete %%cl");
      println("  je 1f");
      println("  mov %%rax, (%%r8)");
      println("  mov %%rdx, 8(%%r8)");
      println("1:");
      println("  movzbl %%cl, %%eax");
      return;
    }

    if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE)
      println("  movq %%xmm0, %%rax"); // a float's bits are in %eax
    push();
    gen_expr(node->cas_old);
    println("  mov %%rax, %%r8");
    println("  mov (%%r8), %s", reg_ax(sz));
    pop("%rdx"); // new
    pop("%rdi"); // addr

    println("  lock cmpxchg %s, (%%rdi)", reg_dx(sz));
    println("  sete %%cl");
    println("  je 1f");
    println("  mov %s, (%%r8)", reg_ax(sz));
    println("1:");
    println("  movzbl %%cl, %%eax");
    return;
  }
  case ND_EXCH: {
    gen_expr(node->lhs);
    push();
    gen_expr(node->rhs);
    pop("%rdi");

    int sz = node->lhs->ty->base->size;
    println("  xchg %s, (%%rdi)", reg_ax(sz));
    return;
  }
  }

  if (is_vector(node->lhs->ty)) {
    gen_vector_binary(node);
    return;
  }

  // Binary operators on float, double and long double operands
  switch (node->lhs->ty->kind) {
  case TY_FLOAT:
  case TY_DOUBLE: {
    gen_expr(node->rhs);
    pushf();
    gen_expr(node->lhs);
    popf(1);

    char *sz = (node->lhs->ty->kind == TY_FLOAT) ? "ss" : "sd";

    switch (node->kind) {
    case ND_ADD:
      println("  add%s %%xmm1, %%xmm0", sz);
      return;
    case ND_SUB:
      println("  sub%s %%xmm1, %%xmm0", sz);
      return;
    case ND_MUL:
      println("  mul%s %%xmm1, %%xmm0", sz);
      return;
    case ND_DIV:
      println("  div%s %%xmm1, %%xmm0", sz);
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      // As gcc does: == and != compare quietly; < and <= raise
      // "invalid" for a NaN.
      if (node->kind == ND_EQ || node->kind == ND_NE)
        println("  ucomi%s %%xmm0, %%xmm1", sz);
      else
        println("  comi%s %%xmm0, %%xmm1", sz);

      if (node->kind == ND_EQ) {
        println("  sete %%al");
        println("  setnp %%dl");
        println("  and %%dl, %%al");
      } else if (node->kind == ND_NE) {
        println("  setne %%al");
        println("  setp %%dl");
        println("  or %%dl, %%al");
      } else if (node->kind == ND_LT) {
        println("  seta %%al");
      } else {
        println("  setae %%al");
      }

      println("  and $1, %%al");
      println("  movzb %%al, %%rax");
      return;
    }

    error_tok(node->tok, "invalid expression");
  }
  case TY_LDOUBLE: {
    // The right side waits in memory, not on the x87 stack, while the
    // left side is computed: that stack has only 8 registers, and must
    // be empty at a call. Then st(0) is the right side, st(1) the left.
    gen_expr(node->rhs);
    println("  sub $16, %%rsp");
    println("  fstpt (%%rsp)");
    depth += 2;
    gen_expr(node->lhs);
    println("  fldt (%%rsp)");
    println("  add $16, %%rsp");
    depth -= 2;

    switch (node->kind) {
    case ND_ADD:
      println("  faddp");
      return;
    case ND_SUB:
      println("  fsubrp");
      return;
    case ND_MUL:
      println("  fmulp");
      return;
    case ND_DIV:
      println("  fdivrp");
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      // As gcc does: == and != compare quietly, and with a NaN (parity
      // set) are false and true; < and <= raise "invalid" for a NaN.
      if (node->kind == ND_EQ || node->kind == ND_NE)
        println("  fucomip");
      else
        println("  fcomip");
      println("  fstp %%st(0)");

      if (node->kind == ND_EQ) {
        println("  sete %%al");
        println("  setnp %%dl");
        println("  and %%dl, %%al");
      } else if (node->kind == ND_NE) {
        println("  setne %%al");
        println("  setp %%dl");
        println("  or %%dl, %%al");
      } else if (node->kind == ND_LT) {
        println("  seta %%al");
      } else {
        println("  setae %%al");
      }

      println("  movzb %%al, %%rax");
      return;
    }

    error_tok(node->tok, "invalid expression");
  }
  }

  if (is_int128(node->lhs->ty)) {
    gen_int128_binary(node);
    return;
  }

  // Binary operators on integers and pointers
  gen_operands(node);

  char *ax, *di, *dx;

  if (is_64bit(node->lhs->ty)) {
    ax = "%rax";
    di = "%rdi";
    dx = "%rdx";
  } else {
    ax = "%eax";
    di = "%edi";
    dx = "%edx";
  }

  switch (node->kind) {
  case ND_ADD:
    println("  add %s, %s", di, ax);
    return;
  case ND_SUB:
    println("  sub %s, %s", di, ax);
    return;
  case ND_MUL:
    println("  imul %s, %s", di, ax);
    return;
  case ND_DIV:
  case ND_MOD:
    if (node->ty->is_unsigned) {
      println("  mov $0, %s", dx);
      println("  div %s", di);
    } else {
      if (node->lhs->ty->size == 8)
        println("  cqo");
      else
        println("  cdq");
      println("  idiv %s", di);
    }

    if (node->kind == ND_MOD)
      println("  mov %%rdx, %%rax");
    return;
  case ND_BITAND:
    println("  and %s, %s", di, ax);
    return;
  case ND_BITOR:
    println("  or %s, %s", di, ax);
    return;
  case ND_BITXOR:
    println("  xor %s, %s", di, ax);
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    println("  cmp %s, %s", di, ax);

    if (node->kind == ND_EQ) {
      println("  sete %%al");
    } else if (node->kind == ND_NE) {
      println("  setne %%al");
    } else if (node->kind == ND_LT) {
      if (node->lhs->ty->is_unsigned)
        println("  setb %%al");
      else
        println("  setl %%al");
    } else if (node->kind == ND_LE) {
      if (node->lhs->ty->is_unsigned)
        println("  setbe %%al");
      else
        println("  setle %%al");
    }

    println("  movzb %%al, %%rax");
    return;
  case ND_SHL:
    println("  mov %%rdi, %%rcx");
    println("  shl %%cl, %s", ax);
    return;
  case ND_SHR:
    println("  mov %%rdi, %%rcx");
    if (node->lhs->ty->is_unsigned)
      println("  shr %%cl, %s", ax);
    else
      println("  sar %%cl, %s", ax);
    return;
  }

  error_tok(node->tok, "invalid expression");
}

//---------- asm statements with operands ------------------------------------
//
// The parser has put each operand in a temporary local and picked its
// register (see asm_stmt() in parser.c). Here they are loaded, the
// template is written with the operands filled in, and outputs are
// stored back.

static char *reg64(int reg) {
  return format("%%%s", gp_reg_name(reg, 8));
}

// Register `reg` as a `size`-byte operand, or as modifier `mod` asks.
static char *asm_reg_text(Node *node, int reg, int size, char mod) {
  if (mod == 'h') {
    static char *high[] = {"ah", "ch", "dh", "bh"};
    if (reg > 3)
      error_tok(node->tok, "%%h needs %%rax, %%rbx, %%rcx or %%rdx");
    return format("%%%s", high[reg]);
  }
  if (mod == 'b') size = 1;
  if (mod == 'w') size = 2;
  if (mod == 'k') size = 4;
  if (mod == 'q') size = 8;
  if (size != 1 && size != 2 && size != 4)
    size = 8;
  return format("%%%s", gp_reg_name(reg, size));
}

// Operand `op` as the template's `%N` (with modifier `mod`) asks.
static char *asm_operand_text(Node *node, AsmOperand *op, char mod) {
  if (mod && !strchr("bhwkqcPna", mod))
    error_tok(node->tok, "asm operand modifier '%c' is not supported", mod);

  if (op->kind == 'r') {
    if (mod == 'a')
      return format("(%s)", reg64(op->reg));
    return asm_reg_text(node, op->reg, op->ty->size, mod);
  }
  if (op->kind == 'x')
    return format("%%xmm%d", op->reg);
  if (op->kind == 'm') {
    if (op->addr)
      return format("(%s)", reg64(op->reg));
    return format("%d(%%rbp)", op->value->offset);
  }

  // A constant: `$5`, or with c, P, a or n (negated) just `5`.
  int64_t val = (mod == 'n') ? -op->val : op->val;
  char *dollar = (mod && strchr("cPan", mod)) ? "" : "$";
  if (!op->label)
    return format("%s%ld", dollar, val);
  if (!val)
    return format("%s%s", dollar, *op->label);
  return format("%s%s%+ld", dollar, *op->label, val);
}

// asm goto's label `%l0` or `%l[name]`, at `*p`, as its index in
// `asm_labels`. Labels are numbered after the operands.
static int asm_label_index(Node *node, char **p) {
  int i = 0;
  Node *g = node->asm_labels;
  if (isdigit(**p)) {
    int n = strtol(*p, p, 10) - node->asm_nops;
    (*p)--;
    for (; g && i < n; g = g->next)
      i++;
    if (n < 0)
      g = NULL;
  } else if (**p == '[') {
    char *end = strchr(*p, ']');
    if (!end)
      error_tok(node->tok, "missing ']' in asm template");
    int len = end - *p - 1;
    for (; g; g = g->next, i++)
      if (strlen(g->label) == len && !strncmp(g->label, *p + 1, len))
        break;
    *p = end;
  }
  if (!g)
    error_tok(node->tok, "%%l in an asm template must name one of its goto labels");
  return i;
}

// The template with its operands filled in: `%0` or `%[name]`, maybe
// with a modifier (`%k0`), `%=` (a number unique to this asm), `%%`,
// and asm goto's labels as `targets` says. `{att|intel}` picks the first
// alternative, as for AT&T syntax.
static char *asm_template(Node *node, char **targets) {
  static int id;
  id++;

  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);
  int alt = 0; // 1 in {'s first alternative, 2 in the others

  for (char *p = node->asm_str; *p; p++) {
    if (*p == '{') {
      alt = 1;
      continue;
    }
    if (*p == '|' && alt) {
      alt = 2;
      continue;
    }
    if (*p == '}' && alt) {
      alt = 0;
      continue;
    }
    if (alt == 2)
      continue;
    if (*p != '%') {
      fputc(*p, out);
      continue;
    }

    p++;
    if (*p && strchr("%{|}", *p)) {
      fputc(*p, out);
      continue;
    }
    if (*p == '=') {
      fprintf(out, "%d", id);
      continue;
    }

    char mod = 0;
    if (isalpha(*p))
      mod = *p++;

    if (mod == 'l') {
      fputs(targets[asm_label_index(node, &p)], out);
      continue;
    }

    AsmOperand *op = NULL;
    if (isdigit(*p)) {
      int i = strtol(p, &p, 10);
      p--;
      if (i >= node->asm_nops)
        error_tok(node->tok, "asm operand number %d out of range", i);
      op = &node->asm_ops[i];
    } else if (*p == '[') {
      char *end = strchr(p, ']');
      if (!end)
        error_tok(node->tok, "missing ']' in asm template");
      for (int i = 0; i < node->asm_nops; i++) {
        char *name = node->asm_ops[i].name;
        if (name && strlen(name) == end - p - 1 && !strncmp(name, p + 1, end - p - 1))
          op = &node->asm_ops[i];
      }
      if (!op)
        error_tok(node->tok, "undefined asm operand name '%.*s'",
                  (int)(end - p - 1), p + 1);
      p = end;
    } else {
      error_tok(node->tok, "invalid '%%' in asm template");
    }
    fputs(asm_operand_text(node, op, mod), out);
  }

  fclose(out);
  return buf;
}

// Loads the `ty` value at `src` into register `reg`.
static void asm_load(Type *ty, char *src, int reg) {
  switch (ty->size) {
  case 1: println("  movzbl %s, %s", src, format("%%%s", gp_reg_name(reg, 4))); return;
  case 2: println("  movzwl %s, %s", src, format("%%%s", gp_reg_name(reg, 4))); return;
  case 4: println("  mov %s, %s", src, format("%%%s", gp_reg_name(reg, 4))); return;
  }
  println("  mov %s, %s", src, reg64(reg));
}

// The instruction that moves an SSE operand of type `ty`
static char *sse_move(Type *ty) {
  if (is_vector(ty))
    return vec_move(ty->size);
  if (ty->kind == TY_FLOAT)
    return "movss";
  if (ty->kind == TY_DOUBLE)
    return "movsd";
  return ty->size == 4 ? "movd" : "movq";
}

// Stores the outputs in registers through their addresses.
static void store_asm_outputs(Node *node) {
  for (int i = 0; i < node->asm_nops; i++) {
    AsmOperand *op = &node->asm_ops[i];
    if (!op->is_output || (op->kind != 'r' && op->kind != 'x'))
      continue;
    println("  mov %d(%%rbp), %s", op->addr->offset, reg64(node->asm_scratch));
    if (op->kind == 'x')
      println("  %s %%xmm%d, (%s)", sse_move(op->ty), op->reg, reg64(node->asm_scratch));
    else
      println("  mov %s, (%s)", asm_reg_text(node, op->reg, op->ty->size, 0),
              reg64(node->asm_scratch));
  }
}

static void gen_asm(Node *node) {
  // The operands' values and addresses, into their temporaries
  for (Node *n = node->body; n; n = n->next)
    gen_stmt(n);

  // SSE operands first, while %rax is free to hold an address
  for (int i = 0; i < node->asm_nops; i++) {
    AsmOperand *op = &node->asm_ops[i];
    if (op->kind == 'x' && op->is_rw) {
      println("  mov %d(%%rbp), %%rax", op->addr->offset);
      println("  %s (%%rax), %%xmm%d", sse_move(op->ty), op->reg);
    } else if (op->kind == 'x' && op->value) {
      println("  %s %d(%%rbp), %%xmm%d", sse_move(op->ty), op->value->offset, op->reg);
    }
  }

  for (int i = 0; i < node->asm_nops; i++) {
    AsmOperand *op = &node->asm_ops[i];
    if (op->kind == 'm' && op->addr) {
      println("  mov %d(%%rbp), %s", op->addr->offset, reg64(op->reg));
    } else if (op->kind == 'r' && op->is_rw) {
      println("  mov %d(%%rbp), %s", op->addr->offset, reg64(op->reg));
      asm_load(op->ty, format("(%s)", reg64(op->reg)), op->reg);
    } else if (op->kind == 'r' && op->value) {
      asm_load(op->ty, format("%d(%%rbp)", op->value->offset), op->reg);
    }
  }

  // asm goto jumps straight to a label, or, when there are register
  // outputs to store or cleanups to run on the way, to a stub that does
  // that first.
  int c = count(), nlabels = 0;
  bool outputs = false, stubs = false;
  for (int i = 0; i < node->asm_nops; i++)
    outputs |= node->asm_ops[i].is_output &&
               (node->asm_ops[i].kind == 'r' || node->asm_ops[i].kind == 'x');
  for (Node *g = node->asm_labels; g; g = g->next)
    nlabels++;
  char **targets = calloc(nlabels + 1, sizeof(char *));
  int i = 0;
  for (Node *g = node->asm_labels; g; g = g->next, i++) {
    targets[i] = g->unique_label;
    if (outputs || g->lhs) {
      targets[i] = format(".L.asm_goto.%d.%d", c, i);
      stubs = true;
    }
  }

  println("  %s", asm_template(node, targets));
  store_asm_outputs(node);
  if (!stubs)
    return;

  println("  jmp .L.asm_goto.%d", c);
  i = 0;
  for (Node *g = node->asm_labels; g; g = g->next, i++) {
    if (targets[i] == g->unique_label)
      continue;
    println("%s:", targets[i]);
    store_asm_outputs(node);
    if (g->lhs)
      gen_stmt(g->lhs);
    println("  jmp %s", g->unique_label);
  }
  println(".L.asm_goto.%d:", c);
}

//---------- Statements ------------------------------------------------------

// The operand for comparing a switch's value with `val`. An instruction's
// immediate is 32 bits, sign-extended, so a 64-bit value outside that
// range is loaded into %rdx first.
static char *case_operand(int64_t val, bool is64) {
  if (!is64)
    return format("$%d", (int32_t)val);
  if (val == (int32_t)val)
    return format("$%ld", val);
  println("  mov $%ld, %%rdx", val);
  return "%rdx";
}

// -g: labels around the code of a block with variables (see LexBlock),
// the first time it's written. True if the begin label was.
static bool block_begin(Node *node) {
  LexBlock *b = node->block;
  if (!opt_g || !b || !b->has_vars || b->emitted)
    return false;
  println(".L.block.%d.begin:", b->id);
  b->emitted = true;
  return true;
}

static void block_end(Node *node) {
  println(".L.block.%d.end:", node->block->id);
}

static void gen_stmt(Node *node) {
  emit_loc(node->tok);

  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_branch(node->cond, false, format(".L.else.%d", c));
    gen_stmt(node->then);
    println("  jmp .L.end.%d", c);
    println(".L.else.%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    bool labeled = block_begin(node);
    if (node->init)
      gen_stmt(node->init);
    println(".L.begin.%d:", c);
    if (node->cond)
      gen_branch(node->cond, false, node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    if (node->inc)
      gen_discard(node->inc);
    println("  jmp .L.begin.%d", c);
    println("%s:", node->brk_label);
    if (labeled)
      block_end(node);
    return;
  }
  case ND_DO: {
    int c = count();
    println(".L.begin.%d:", c);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    gen_branch(node->cond, true, format(".L.begin.%d", c));
    println("%s:", node->brk_label);
    return;
  }
  case ND_SWITCH:
    gen_expr(node->cond);

    // On __int128 (in %rdx:%rax), both halves are compared. The high half
    // goes to %rsi, since case_operand() may use %rdx.
    if (is_int128(node->cond->ty)) {
      println("  mov %%rdx, %%rsi");
      for (Node *n = node->case_next; n; n = n->case_next) {
        int c = count();
        if (n->begin == n->end && n->begin_hi == n->end_hi) {
          println("  cmp %s, %%rax", case_operand(n->begin, true));
          println("  jne .L.case.%d", c);
          println("  cmp %s, %%rsi", case_operand(n->begin_hi, true));
          println("  je %s", n->label);
          println(".L.case.%d:", c);
          continue;
        }

        // [GNU] A range: is x - begin <= end - begin, unsigned?
        unsigned __int128 b = (unsigned __int128)(uint64_t)n->begin_hi << 64 | (uint64_t)n->begin;
        unsigned __int128 e = (unsigned __int128)(uint64_t)n->end_hi << 64 | (uint64_t)n->end;
        unsigned __int128 d = e - b;
        println("  mov %%rax, %%rdi");
        println("  mov %%rsi, %%rcx");
        println("  sub %s, %%rdi", case_operand(n->begin, true));
        println("  sbb %s, %%rcx", case_operand(n->begin_hi, true)); // (a mov keeps CF)
        println("  cmp %s, %%rcx", case_operand((int64_t)(d >> 64), true));
        println("  jb %s", n->label);
        println("  jne .L.case.%d", c);
        println("  cmp %s, %%rdi", case_operand((int64_t)d, true));
        println("  jbe %s", n->label);
        println(".L.case.%d:", c);
      }
    }

    for (Node *n = node->case_next; n && !is_int128(node->cond->ty); n = n->case_next) {
      bool is64 = node->cond->ty->size == 8;
      char *ax = is64 ? "%rax" : "%eax";
      char *di = is64 ? "%rdi" : "%edi";

      if (n->begin == n->end) {
        println("  cmp %s, %s", case_operand(n->begin, is64), ax);
        println("  je %s", n->label);
        continue;
      }

      // [GNU] Case ranges
      println("  mov %s, %s", ax, di);
      println("  sub %s, %s", case_operand(n->begin, is64), di);
      println("  cmp %s, %s", case_operand(n->end - n->begin, is64), di);
      println("  jbe %s", n->label);
    }

    if (node->default_case)
      println("  jmp %s", node->default_case->label);

    println("  jmp %s", node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->brk_label);
    return;
  case ND_CASE:
    println("%s:", node->label);
    gen_stmt(node->lhs);
    return;
  case ND_BLOCK: {
    bool labeled = block_begin(node);
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    if (labeled)
      block_end(node);
    return;
  }
  case ND_GOTO:
    if (node->lhs)
      gen_stmt(node->lhs); // cleanups
    println("  jmp %s", node->unique_label);
    return;
  case ND_GOTO_EXPR:
    gen_expr(node->lhs);
    println("  jmp *%%rax");
    return;
  case ND_LABEL:
    println("%s:", node->unique_label);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      Type *ty = node->lhs->ty;

      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (!is_ret_in_memory(ty))
          copy_struct_reg();
        else
          copy_struct_mem();
        break;
      }
    }

    println("  jmp .L.return.%s", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_discard(node->lhs);
    return;
  case ND_VLA_FREE:
    // With no temporaries on the stack, %rsp is the stack bottom, and
    // both go back up to where they were before the VLAs were
    // allocated. With some, in a statement expression, the VLAs stay
    // until the function returns (moving the temporaries isn't worth it).
    if (depth)
      return;
    gen_expr(node->lhs);
    println("  mov %%rax, %d(%%rbp)", current_fn->alloca_bottom->offset);
    println("  mov %%rax, %%rsp");
    return;
  case ND_ASM:
    has_inline_asm = true;
    if (node->asm_extended)
      gen_asm(node);
    else
      println("  %s", node->asm_str);
    return;
  }

  error_tok(node->tok, "invalid statement");
}

//---------- Register variables ----------------------------------------------

// A function's most used local variables live in the callee-saved
// registers %rbx and %r12-%r15 instead of its stack frame: calls leave
// those registers alone, and loads and stores become register moves.
// Only integer and pointer locals whose address is never taken (no &x)
// qualify, since a register has no address. The function saves the
// registers it uses in its prologue and restores them before returning.
//
// A function that calls setjmp() keeps every variable in memory: a
// register variable changed after setjmp() would come back from longjmp()
// with its old value. So does one with an asm() statement, which might
// use these registers.

static bool no_register_vars; // set by scan_uses()

static bool is_setjmp(Node *fn) {
  static char *names[] = {
    "setjmp", "_setjmp", "sigsetjmp", "__sigsetjmp", "savectx", "vfork",
    "getcontext",
  };
  if (fn->kind != ND_VAR)
    return false;
  for (int i = 0; i < sizeof(names) / sizeof(*names); i++)
    if (!strcmp(fn->var->name, names[i]))
      return true;
  return false;
}

// Counts the uses of each variable in `node`, `weight` each (a use in a
// loop counts 8 times more per level), and notes the variables whose
// address is taken.
static void scan_uses(Node *node, int weight) {
  if (!node)
    return;

  switch (node->kind) {
  case ND_VAR:
    node->var->uses += weight;
    return;
  case ND_ADDR: {
    // The variable gen_addr() finds, as in &x, &s.m and &(int){0} (a
    // compound literal is `(init, var)`)
    Node *n = node->lhs;
    while (n->kind == ND_COMMA || n->kind == ND_MEMBER)
      n = n->kind == ND_COMMA ? n->rhs : n->lhs;
    if (n->kind == ND_VAR)
      n->var->is_addr_taken = true;
    break;
  }
  case ND_ASM:
    no_register_vars = true;
    return;
  case ND_FUNCALL:
    if (is_setjmp(node->lhs))
      no_register_vars = true;
    break;
  case ND_FOR:
  case ND_DO:
    scan_uses(node->init, weight);
    weight = MIN(weight * 8, 512);
    scan_uses(node->cond, weight);
    scan_uses(node->inc, weight);
    scan_uses(node->then, weight);
    return;
  }

  scan_uses(node->lhs, weight);
  scan_uses(node->rhs, weight);
  if (has_stmt_fields(node->kind)) {
    scan_uses(node->cond, weight);
    scan_uses(node->then, weight);
    scan_uses(node->els, weight);
    scan_uses(node->init, weight);
    scan_uses(node->inc, weight);
    for (Node *n = node->body; n; n = n->next)
      scan_uses(n, weight);
  }
  if (node->kind == ND_CAS || node->kind == ND_OVERFLOW) {
    scan_uses(node->cas_addr, weight);
    scan_uses(node->cas_old, weight);
    scan_uses(node->cas_new, weight);
  }
  if (node->kind == ND_FUNCALL)
    for (Node *n = node->args; n; n = n->next)
      scan_uses(n, weight);
}

static bool can_be_in_register(Obj *fn, Obj *var) {
  Type *ty = var->ty;
  return !var->is_addr_taken && var != fn->alloca_bottom && !ty->is_atomic &&
         ((is_integer(ty) && !is_int128(ty)) || ty->kind == TY_PTR);
}

// Gives the most used eligible variables of `fn` a register each.
static void assign_registers(Obj *fn) {
  no_register_vars = false;
  scan_uses(fn->body, 1);

  // With variables in memory, the prologue saves the callee-saved
  // registers (regs64[]: %rbx, %r12-%r15) its asm statements use instead.
  if (no_register_vars) {
    static int nums[] = {3, 12, 13, 14, 15};
    for (int i = 0; i < NREGS; i++)
      if (fn->asm_regs & (1 << nums[i]))
        fn->nregs = i + 1;
    return;
  }

  while (fn->nregs < NREGS) {
    // A variable used only once or twice isn't worth saving and
    // restoring a register for.
    Obj *best = NULL;
    for (Obj *var = fn->locals; var; var = var->next)
      if (!var->reg && var->uses >= 3 && can_be_in_register(fn, var) &&
          (!best || var->uses > best->uses))
        best = var;
    if (!best)
      return;
    best->reg = ++fn->nregs;
  }
}

//---------- Stack frames and the data section -------------------------------

static bool is_param(Obj *fn, Obj *var) {
  for (Obj *p = fn->params; p; p = p->next)
    if (p == var)
      return true;
  return false;
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    // If a function has many parameters, some parameters are
    // inevitably passed by stack rather than by register.
    // The first passed-by-stack parameter resides at RBP+16.
    int top = 16;
    int bottom = 0;

    int gp = 0, fp = 0;

    // Assign offsets to pass-by-stack parameters.
    for (Obj *var = fn->params; var; var = var->next) {
      Type *ty = var->ty;

      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (struct_in_regs(ty, gp, fp)) {
          int ngp, nfp;
          struct_regs(ty, &ngp, &nfp);
          gp += ngp;
          fp += nfp;
          continue;
        }
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
      case TY_VECTOR:
        if (fp++ < FP_MAX)
          continue;
        break;
      case TY_LDOUBLE:
        break;
      case TY_INT128:
        if (gp + 2 <= GP_MAX) {
          gp += 2;
          continue;
        }
        break;
      default:
        if (gp++ < GP_MAX)
          continue;
      }

      // On the stack: 8-byte aligned, or 16 if its type is (as push_args)
      top = align_to(top, ty->align > 8 ? 16 : 8);
      var->offset = top;
      top += var->ty->size;
    }

    // Assign offsets to pass-by-register parameters and local variables.
    // Register variables need none.
    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->offset || var->reg)
        continue;

      // AMD64 System V ABI has a special alignment rule for an array of
      // length at least 16 bytes. We need to align such array to at least
      // 16-byte boundaries. See p.14 of
      // https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-draft.pdf.
      int align = (var->ty->kind == TY_ARRAY && var->ty->size >= 16)
        ? MAX(16, var->align) : var->align;

      // %rbp is only 16-byte aligned, so a local aligned above that gets
      // an 8-byte slot here, and the prologue carves its storage out of
      // the stack, aligned, and puts the address in the slot. (Not for a
      // parameter, whose value arrives in its slot.)
      if (align > 16 && var->ty->kind != TY_VLA && !is_param(fn, var)) {
        var->is_overaligned = true;
        align = 8;
        bottom += 8;
        bottom = align_to(bottom, align);
        var->offset = -bottom;
        continue;
      }

      bottom += var->ty->size;
      bottom = align_to(bottom, align);
      var->offset = -bottom;
    }

    // Slots to save the callee-saved registers the function uses.
    bottom = align_to(bottom, 8) + fn->nregs * 8;
    fn->regs_offset = -bottom;

    fn->stack_size = align_to(bottom, 16);
  }
}

static bool has_weak; // any weak declaration in this file

// A definition is weak if any declaration of that name was, as in
// `extern int x __attribute__((weak)); int x = 1;`.
static bool is_weak(Obj *prog, Obj *var) {
  if (var->is_weak)
    return true;
  if (!has_weak)
    return false;
  for (Obj *o = prog; o; o = o->next)
    if (o->is_weak && !strcmp(o->name, var->name))
      return true;
  return false;
}

// `.local`, `.globl` or `.weak`, and `.hidden` and so on
static void emit_binding(Obj *prog, Obj *var) {
  if (var->is_static)
    println("  .local %s", var->name);
  else if (is_weak(prog, var))
    println("  .weak %s", var->name);
  else
    println("  .globl %s", var->name);

  if (!var->is_static && var->visibility && strcmp(var->visibility, "default"))
    println("  .%s %s", var->visibility, var->name);
}

// Weak declarations that are used: a missing definition is then 0, not a
// link error.
static void emit_weak_refs(Obj *prog) {
  for (Obj *var = prog; var; var = var->next)
    if (var->is_weak && !var->is_definition && !var->alias_target && var->is_used)
      println("  .weak %s", var->name);
}

// alias("target") defines another name for target, which the parser
// checked is defined in this file.
static void emit_aliases(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (!var->alias_target)
      continue;
    emit_binding(prog, var);
    println("  .set %s, %s", var->name, var->alias_target);
  }
}

// A pointer to a constructor in .init_array (or a destructor in
// .fini_array), which the C library calls before (or after) main. With a
// priority N, it goes in .init_array.N, which the linker sorts first.
static void emit_init_entry(char *kind, int prio, char *name) {
  char *sec = prio < 0 ? format(".%s_array", kind) :
                         format(".%s_array.%05d", kind, prio);
  println("  .section %s,\"aw\",@%s_array", sec, kind);
  println("  .align 8");
  println("  .quad %s", name);
}

// The list has the newest declaration first. Emitting the oldest first
// runs those without a priority in the order they're declared, as gcc does.
static void emit_init_arrays(Obj *fn) {
  if (!fn)
    return;
  emit_init_arrays(fn->next);
  if (!fn->is_function || !fn->is_definition || !fn->is_live)
    return;
  if (fn->is_ctor)
    emit_init_entry("init", fn->ctor_prio, fn->name);
  if (fn->is_dtor)
    emit_init_entry("fini", fn->dtor_prio, fn->name);
}

// A string literal, or a const object with no addresses for the linker to
// fill in, goes in .rodata, as with gcc, so writing to it faults.
static bool is_readonly(Obj *var) {
  if (var->is_tls || var->section || var->rel)
    return false;
  Type *ty = var->ty;
  while (ty->kind == TY_ARRAY)
    ty = ty->base;
  return var->is_string || ty->is_const;
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function || !var->is_definition)
      continue;

    emit_binding(prog, var);
    bool ro = is_readonly(var);

    int align = (var->ty->kind == TY_ARRAY && var->ty->size >= 16)
      ? MAX(16, var->align) : var->align;

    // Common symbol (never for a weak or thread-local one or one with a
    // section, as with gcc)
    bool common = var->common ? var->common > 0 : opt_fcommon;
    if (common && var->is_tentative && !var->is_tls && !is_weak(prog, var) &&
        !var->section) {
      println("  .comm %s, %d, %d", var->name, var->ty->size, align);
      continue;
    }

    // .data, .rodata or .tdata, or its section("name")
    if (var->init_data) {
      if (var->is_tls)
        println("  .section .tdata,\"awT\",@progbits");
      else if (var->section)
        println("  .section %s,\"aw\",@progbits", var->section);
      else
        println(ro ? "  .section .rodata" : "  .data");

      int size = var->ty->size + var->flex_size;
      println("  .type %s, @object", var->name);
      println("  .size %s, %d", var->name, size);
      println("  .align %d", align);
      println("%s:", var->name);

      // Runs of zeros as .zero, other bytes 16 to a line: a big array
      // with a few values set is a few lines.
      Relocation *rel = var->rel;
      int pos = 0;
      while (pos < size) {
        if (rel && rel->offset == pos) {
          println("  .quad %s%+ld", *rel->label, rel->addend);
          rel = rel->next;
          pos += 8;
          continue;
        }

        int end = rel ? rel->offset : size;
        int n = 0;
        while (pos + n < end && !var->init_data[pos + n])
          n++;
        if (n >= 8) {
          println("  .zero %d", n);
          pos += n;
          continue;
        }

        char buf[16 * 5 + 1], *p = buf;
        n = MIN(16, end - pos);
        for (int i = 0; i < n; i++)
          p += sprintf(p, i ? ",%d" : "%d", var->init_data[pos + i]);
        println("  .byte %s", buf);
        pos += n;
      }
      continue;
    }

    // .bss, .rodata or .tbss, or its section("name"), which holds zeros
    // unless it is a .bss one
    if (var->is_tls)
      println("  .section .tbss,\"awT\",@nobits");
    else if (var->section)
      println("  .section %s,\"aw\",@%s", var->section,
              strncmp(var->section, ".bss", 4) ? "progbits" : "nobits");
    else
      println(ro ? "  .section .rodata" : "  .bss");

    println("  .align %d", align);
    println("%s:", var->name);
    println("  .zero %d", var->ty->size);
  }
}

//---------- Functions (the text section) ------------------------------------

static void store_fp(int r, int offset, int sz) {
  switch (sz) {
  case 4:
    println("  movss %%xmm%d, %d(%%rbp)", r, offset);
    return;
  case 8:
    println("  movsd %%xmm%d, %d(%%rbp)", r, offset);
    return;
  }
  unreachable();
}

static void store_gp(int r, int offset, int sz) {
  switch (sz) {
  case 1:
    println("  mov %s, %d(%%rbp)", argreg8[r], offset);
    return;
  case 2:
    println("  mov %s, %d(%%rbp)", argreg16[r], offset);
    return;
  case 4:
    println("  mov %s, %d(%%rbp)", argreg32[r], offset);
    return;
  case 8:
    println("  mov %s, %d(%%rbp)", argreg64[r], offset);
    return;
  default:
    for (int i = 0; i < sz; i++) {
      println("  mov %s, %d(%%rbp)", argreg8[r], offset + i);
      println("  shr $8, %s", argreg64[r]);
    }
    return;
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;

    // No code is emitted for "static inline" functions
    // if no one is referencing them.
    if (!fn->is_live)
      continue;

    emit_binding(prog, fn);
    if (fn->section)
      println("  .section %s,\"ax\",@progbits", fn->section);
    else
      println("  .text");
    println("  .type %s, @function", fn->name);
    println("%s:", fn->name);
    current_fn = fn;
    loc_line = 0;

    // The prologue is on the function's line, so the line table covers
    // its first instruction, where gdb's `break f` looks.
    if (fn->tok)
      emit_loc(fn->tok);

    // Prologue
    println("  push %%rbp");
    println("  mov %%rsp, %%rbp");
    println("  sub $%d, %%rsp", fn->stack_size);

    // Locals aligned above 16 (see assign_lvar_offsets())
    for (Obj *var = fn->locals; var; var = var->next) {
      if (!var->is_overaligned)
        continue;
      println("  sub $%d, %%rsp", var->ty->size);
      println("  and $%d, %%rsp", -var->align);
      println("  mov %%rsp, %d(%%rbp)", var->offset);
    }
    println("  mov %%rsp, %d(%%rbp)", fn->alloca_bottom->offset);
    for (int i = 0; i < fn->nregs; i++)
      println("  mov %s, %d(%%rbp)", regs64[i], fn->regs_offset + i * 8);

    // Save passed-by-register arguments to the stack, or move them to
    // their registers. (This leaves the argument registers as they were,
    // except the part of one holding a named argument, so a variadic
    // function saves them afterwards.)
    int gp = 0, fp = 0;
    int stack_end = 16; // where the named arguments on the stack end
    for (Obj *var = fn->params; var; var = var->next) {
      // Passed on the stack (only a register variable moves)
      if (var->offset > 0) {
        stack_end = MAX(stack_end, var->offset + align_to(var->ty->size, 8));
        if (var->reg)
          println("  mov %d(%%rbp), %s", var->offset, regs64[var->reg - 1]);
        continue;
      }

      if (var->reg) {
        println("  mov %s, %s", argreg64[gp++], regs64[var->reg - 1]);
        continue;
      }

      Type *ty = var->ty;

      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        assert(ty->size <= 16);
        if (is_vector16(ty)) {
          println("  movdqu %%xmm%d, %d(%%rbp)", fp++, var->offset);
          break;
        }
        if (has_flonum(ty, 0, 8, 0))
          store_fp(fp++, var->offset, MIN(8, ty->size));
        else
          store_gp(gp++, var->offset, MIN(8, ty->size));

        if (has_two_parts(ty)) {
          if (has_flonum(ty, 8, 16, 0))
            store_fp(fp++, var->offset + 8, ty->size - 8);
          else
            store_gp(gp++, var->offset + 8, ty->size - 8);
        }
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        store_fp(fp++, var->offset, ty->size);
        break;
      case TY_INT128:
        store_gp(gp++, var->offset, 8);
        store_gp(gp++, var->offset + 8, 8);
        break;
      case TY_VECTOR:
        println("  %s %%xmm%d, %d(%%rbp)", vec_move(ty->size), fp++, var->offset);
        break;
      default:
        store_gp(gp++, var->offset, ty->size);
      }
    }

    // A variadic function's va_list (__va_area__), which va_start copies,
    // and the register save area after it, laid out as the psABI says,
    // so a va_list can be passed to code built by other compilers:
    // 6 general-purpose registers, then 8 XMM registers of 16 bytes.
    if (fn->va_area) {
      int off = fn->va_area->offset;
      println("  movl $%d, %d(%%rbp)", gp * 8, off);          // gp_offset
      println("  movl $%d, %d(%%rbp)", 48 + fp * 16, off + 4); // fp_offset
      println("  lea %d(%%rbp), %%rax", stack_end);           // overflow_arg_area
      println("  mov %%rax, %d(%%rbp)", off + 8);
      println("  lea %d(%%rbp), %%rax", off + 24);             // reg_save_area
      println("  mov %%rax, %d(%%rbp)", off + 16);
      for (int i = 0; i < GP_MAX; i++)
        println("  mov %s, %d(%%rbp)", argreg64[i], off + 24 + i * 8);
      for (int i = 0; i < FP_MAX; i++) // whole, for vectors
        println("  movdqu %%xmm%d, %d(%%rbp)", i, off + 72 + i * 16);
    }

    // Emit code
    gen_stmt(fn->body);
    assert(depth == 0);

    // [https://www.sigbus.info/n1570#5.1.2.2.3p1] The C spec defines
    // a special rule for the main function. Reaching the end of the
    // main function is equivalent to returning 0, even though the
    // behavior is undefined for the other functions.
    if (strcmp(fn->name, "main") == 0)
      println("  mov $0, %%rax");

    // Epilogue
    println(".L.return.%s:", fn->name);
    for (int i = 0; i < fn->nregs; i++)
      println("  mov %d(%%rbp), %s", fn->regs_offset + i * 8, regs64[i]);
    println("  mov %%rbp, %%rsp");
    println("  pop %%rbp");
    println("  ret");
    if (opt_g)
      println(".L.end.%s:", fn->name); // for the debug info's code range
  }
}

//---------- Debug info (-g): variables and types ----------------------------
//
// With -g, a DWARF 4 .debug_info compile unit, as gcc -gdwarf-4 writes,
// so gdb can print variables: each function with its parameters and
// locals, the global variables, and their types, with where each lives
// (an offset from %rbp, a callee-saved register, an address). The line
// table comes from the .loc directives: the assembler (asm.c, or GNU as)
// writes it into the .debug_line named here. Numbers in LEB128 are
// encoded here and written as .byte lists. A block's variables are in a
// lexical block, which gdb shows them in only (see LexBlock). Typedef
// names and qualifiers aren't recorded: gdb shows a size_t as unsigned
// long.

// Abbreviation codes, for the table in emit_debug_abbrev()
enum {
  AB_CU = 1, AB_FUNC, AB_FUNC_VOID, AB_PARAM, AB_VAR, AB_GVAR, AB_GVAR_NOLOC,
  AB_BASE, AB_PTR, AB_PTR_VOID, AB_STRUCT, AB_STRUCT_ANON, AB_STRUCT_DECL,
  AB_UNION, AB_UNION_ANON, AB_UNION_DECL, AB_MEMBER, AB_MEMBER_ANON,
  AB_BITFIELD, AB_ARRAY, AB_SUBRANGE, AB_SUBRANGE_NOCOUNT, AB_ENUM,
  AB_ENUM_ANON, AB_ENUMERATOR, AB_SUBR, AB_SUBR_VOID, AB_SUBR_PARAM,
  AB_VARARGS, AB_BLOCK, AB_SUBRANGE_EXPR, AB_VECTOR,
};

// The DWARF numbers of regs64[]'s registers: %rbx, %r12 to %r15
static int dwarf_regs[] = {3, 12, 13, 14, 15};

// `val` in LEB128, as a .byte list
static char *leb128(int64_t val, bool is_signed) {
  char *s = "";
  for (;;) {
    int byte = val & 0x7f;
    val = is_signed ? val >> 7 : (int64_t)((uint64_t)val >> 7);
    bool done = is_signed ? (val == 0 && !(byte & 0x40)) || (val == -1 && (byte & 0x40))
                          : val == 0;
    s = format("%s%s%d", s, *s ? "," : "", done ? byte : byte | 0x80);
    if (done)
      return s;
  }
}

static int leb128_len(int64_t val, bool is_signed) {
  int n = 1;
  for (char *p = leb128(val, is_signed); *p; p++)
    n += *p == ',';
  return n;
}

static void dw_string(char *s) {
  char *esc = "";
  for (char *p = s; *p; p++)
    esc = format(*p == '"' || *p == '\\' ? "%s\\%c" : "%s%c", esc, *p);
  println("  .string \"%s\"", esc);
}

static void dw_udata(int64_t val) {
  println("  .byte %s", leb128(val, false));
}

static void dw_ref(char *label) {
  println("  .long %s - .L.dbg.info", label);
}

// Types get a DIE each, named by a label. One is asked for by type_die(),
// which only queues it: emit_type_dies() writes the queued ones after the
// functions, so a struct whose members point to it isn't written inside
// itself.
static HashMap type_labels;
static Type **type_queue;
static char **type_queue_labels;
static int type_queue_len, type_queue_cap;

// A name for `ty` that types gcc would describe with one DIE share.
static char *type_key(Type *ty) {
  switch (ty->kind) {
  case TY_PTR:
    return format("*%s", ty->base->kind == TY_VOID ? "void" : type_key(ty->base));
  case TY_VLA: // its length is read from its variables
    return format("vla%p", ty);
  case TY_ARRAY:
    return format("[%d]%s", ty->array_len, type_key(ty->base));
  case TY_VECTOR:
    return format("v%d%s", ty->array_len, type_key(ty->elem));
  case TY_FUNC: {
    char *s = format("(%s", ty->return_ty->kind == TY_VOID ? "void" : type_key(ty->return_ty));
    for (Type *p = ty->params; p; p = p->next)
      s = format("%s,%s", s, type_key(p));
    return format("%s%s)", s, ty->is_variadic ? ",..." : "");
  }
  case TY_STRUCT:
  case TY_UNION:
  case TY_ENUM:
    // Every qualified copy goes back to the type its declaration made.
    while (ty->origin)
      ty = ty->origin;
    return format("%p", ty);
  default:
    return format("%d%d%d", ty->kind, ty->is_unsigned, ty->is_distinct);
  }
}

// The label of `ty`'s DIE, or NULL for void
static char *type_die(Type *ty) {
  if (ty->kind == TY_VOID)
    return NULL;
  char *key = type_key(ty);
  char *label = hashmap_get(&type_labels, key);
  if (label)
    return label;

  label = format(".L.dbg.type.%d", type_labels.used);
  hashmap_put(&type_labels, key, label);
  if (type_queue_len == type_queue_cap) {
    type_queue_cap = type_queue_cap ? type_queue_cap * 2 : 32;
    type_queue = realloc(type_queue, sizeof(Type *) * type_queue_cap);
    type_queue_labels = realloc(type_queue_labels, sizeof(char *) * type_queue_cap);
  }
  type_queue[type_queue_len] = ty;
  type_queue_labels[type_queue_len++] = label;
  return label;
}

static char *base_type_name(Type *ty, int *encoding) {
  enum { ATE_BOOLEAN = 2, ATE_FLOAT = 4, ATE_SIGNED = 5, ATE_SIGNED_CHAR = 6,
         ATE_UNSIGNED = 7, ATE_UNSIGNED_CHAR = 8 };
  bool u = ty->is_unsigned;
  *encoding = u ? ATE_UNSIGNED : ATE_SIGNED;
  switch (ty->kind) {
  case TY_BOOL: *encoding = ATE_BOOLEAN; return "_Bool";
  case TY_CHAR:
    *encoding = u ? ATE_UNSIGNED_CHAR : ATE_SIGNED_CHAR;
    return u ? "unsigned char" : ty->is_distinct ? "signed char" : "char";
  case TY_SHORT: return u ? "unsigned short" : "short";
  case TY_INT: return u ? "unsigned int" : "int";
  case TY_LONG:
    if (ty->is_distinct)
      return u ? "unsigned long long" : "long long";
    return u ? "unsigned long" : "long";
  case TY_INT128: return u ? "__int128 unsigned" : "__int128"; // as gcc names them
  case TY_FLOAT: *encoding = ATE_FLOAT; return "float";
  case TY_DOUBLE: *encoding = ATE_FLOAT; return "double";
  case TY_LDOUBLE: *encoding = ATE_FLOAT; return "long double";
  }
  unreachable();
}

// DWARF expression operations, as a .byte list, that push the value of
// local `var`: from its callee-saved register, or its place in the frame.
static char *var_value_ops(Obj *var) {
  if (var->reg)
    return format("%d,0", 0x70 + dwarf_regs[var->reg - 1]); // DW_OP_bregN 0
  return format("0x91,%s,6", leb128(var->offset, true)); // DW_OP_fbreg, DW_OP_deref
}

// The number of bytes in a .byte list
static int byte_count(char *list) {
  int n = 1;
  for (char *p = list; *p; p++)
    n += *p == ',';
  return n;
}

static void emit_type_die(Type *ty, char *label) {
  println("%s:", label);

  // A complex number is a base type, named as gcc names it
  if (is_complex(ty)) {
    dw_udata(AB_BASE);
    dw_string(format("complex %s", type_name(complex_part(ty))));
    println("  .byte 3"); // DW_ATE_complex_float
    println("  .byte %d", ty->size);
    return;
  }

  switch (ty->kind) {
  case TY_VLA:
    // An array whose length is its size, a variable, over its element's
    // size (another variable, for a VLA of VLAs)
    dw_udata(AB_ARRAY);
    dw_ref(type_die(ty->base));
    if (ty->vla_size) {
      Type *base = ty->base;
      char *elem = base->kind == TY_VLA && base->vla_size
                     ? var_value_ops(base->vla_size)
                     : format("0x10,%s", leb128(base->size, false)); // DW_OP_constu
      char *ops = format("%s,%s,0x1b", var_value_ops(ty->vla_size), elem); // DW_OP_div
      dw_udata(AB_SUBRANGE_EXPR);
      println("  .byte %d,%s", byte_count(ops), ops);
    } else {
      dw_udata(AB_SUBRANGE_NOCOUNT);
    }
    println("  .byte 0");
    return;
  case TY_PTR: {
    char *base = type_die(ty->base);
    dw_udata(base ? AB_PTR : AB_PTR_VOID);
    println("  .byte 8");
    if (base)
      dw_ref(base);
    return;
  }
  case TY_VECTOR:
    dw_udata(AB_VECTOR);
    dw_ref(type_die(ty->elem));
    dw_udata(AB_SUBRANGE);
    dw_udata(ty->array_len);
    println("  .byte 0");
    return;
  case TY_ARRAY:
    dw_udata(AB_ARRAY);
    dw_ref(type_die(ty->base));
    if (ty->array_len >= 0) {
      dw_udata(AB_SUBRANGE);
      dw_udata(ty->array_len);
    } else {
      dw_udata(AB_SUBRANGE_NOCOUNT);
    }
    println("  .byte 0");
    return;
  case TY_FUNC: {
    char *ret = type_die(ty->return_ty);
    dw_udata(ret ? AB_SUBR : AB_SUBR_VOID);
    if (ret)
      dw_ref(ret);
    for (Type *p = ty->params; p; p = p->next) {
      dw_udata(AB_SUBR_PARAM);
      dw_ref(type_die(p));
    }
    if (ty->is_variadic)
      dw_udata(AB_VARARGS);
    println("  .byte 0");
    return;
  }
  case TY_STRUCT:
  case TY_UNION: {
    bool is_struct = ty->kind == TY_STRUCT;
    char *name = ty->tag ? strndup(ty->tag->loc, ty->tag->len) : NULL;
    if (ty->size < 0) {
      // Declared, never defined here: `struct FILE_internal *`
      dw_udata(is_struct ? AB_STRUCT_DECL : AB_UNION_DECL);
      dw_string(name ? name : "");
      return;
    }
    if (name) {
      dw_udata(is_struct ? AB_STRUCT : AB_UNION);
      dw_string(name);
    } else {
      dw_udata(is_struct ? AB_STRUCT_ANON : AB_UNION_ANON);
    }
    dw_udata(ty->size);
    for (Member *mem = ty->members; mem; mem = mem->next) {
      if (mem->is_bitfield) {
        if (!mem->name)
          continue; // padding, as `int : 3`
        dw_udata(AB_BITFIELD);
        dw_string(strndup(mem->name->loc, mem->name->len));
        dw_ref(type_die(mem->ty));
        dw_udata(mem->bit_width);
        dw_udata(mem->offset * 8 + mem->bit_offset);
        continue;
      }
      if (mem->name) {
        dw_udata(AB_MEMBER);
        dw_string(strndup(mem->name->loc, mem->name->len));
      } else {
        dw_udata(AB_MEMBER_ANON);
      }
      dw_ref(type_die(mem->ty));
      dw_udata(mem->offset);
    }
    println("  .byte 0");
    return;
  }
  case TY_ENUM:
    if (ty->tag) {
      dw_udata(AB_ENUM);
      dw_string(strndup(ty->tag->loc, ty->tag->len));
    } else {
      dw_udata(AB_ENUM_ANON);
    }
    println("  .byte %d", MAX(ty->size, 1));
    for (EnumConst *e = ty->enum_consts; e; e = e->next) {
      dw_udata(AB_ENUMERATOR);
      dw_string(strndup(e->name->loc, e->name->len));
      println("  .byte %s", leb128(e->val, true));
    }
    println("  .byte 0");
    return;
  default: {
    int encoding;
    char *name = base_type_name(ty, &encoding);
    dw_udata(AB_BASE);
    dw_string(name);
    println("  .byte %d", encoding);
    println("  .byte %d", ty->size);
  }
  }
}

static void emit_type_dies(void) {
  for (int i = 0; i < type_queue_len; i++)
    emit_type_die(type_queue[i], type_queue_labels[i]);
}

// The abbreviation table: for each code, its tag, whether it has
// children, and its attributes with their forms. All these numbers fit
// in one LEB128 byte.
static void emit_debug_abbrev(void) {
  enum {
    // Tags
    COMPILE_UNIT = 0x11, SUBPROGRAM = 0x2e, FORMAL_PARAMETER = 0x05,
    VARIABLE = 0x34, BASE_TYPE = 0x24, POINTER_TYPE = 0x0f,
    STRUCTURE_TYPE = 0x13, UNION_TYPE = 0x17, MEMBER = 0x0d,
    ARRAY_TYPE = 0x01, SUBRANGE_TYPE = 0x21, ENUMERATION_TYPE = 0x04,
    ENUMERATOR = 0x28, SUBROUTINE_TYPE = 0x15, UNSPECIFIED_PARAMETERS = 0x18,
    LEXICAL_BLOCK = 0x0b,
    // Attributes
    NAME = 0x03, BYTE_SIZE = 0x0b, BIT_SIZE = 0x0d, STMT_LIST = 0x10,
    LOW_PC = 0x11, HIGH_PC = 0x12, LANGUAGE = 0x13, COMP_DIR = 0x1b,
    CONST_VALUE = 0x1c, PRODUCER = 0x25, PROTOTYPED = 0x27, COUNT = 0x37,
    DATA_MEMBER_LOCATION = 0x38, DECL_FILE = 0x3a, DECL_LINE = 0x3b,
    DECLARATION = 0x3c, ENCODING = 0x3e, EXTERNAL = 0x3f, FRAME_BASE = 0x40,
    LOCATION = 0x02, TYPE = 0x49, DATA_BIT_OFFSET = 0x6b,
    // Forms
    ADDR = 0x01, DATA2 = 0x05, DATA8 = 0x07, STRING = 0x08, DATA1 = 0x0b,
    FLAG = 0x0c, SDATA = 0x0d, UDATA = 0x0f, REF4 = 0x13, SEC_OFFSET = 0x17,
    EXPRLOC = 0x18, FLAG_PRESENT = 0x19,
  };

  static int table[] = {
    AB_CU, COMPILE_UNIT, 1, PRODUCER, STRING, LANGUAGE, DATA2, NAME, STRING,
      COMP_DIR, STRING, LOW_PC, ADDR, HIGH_PC, DATA8, STMT_LIST, SEC_OFFSET, 0, 0,
    AB_FUNC, SUBPROGRAM, 1, EXTERNAL, FLAG, NAME, STRING, DECL_FILE, UDATA,
      DECL_LINE, UDATA, PROTOTYPED, FLAG_PRESENT, TYPE, REF4, LOW_PC, ADDR,
      HIGH_PC, DATA8, FRAME_BASE, EXPRLOC, 0, 0,
    AB_FUNC_VOID, SUBPROGRAM, 1, EXTERNAL, FLAG, NAME, STRING, DECL_FILE, UDATA,
      DECL_LINE, UDATA, PROTOTYPED, FLAG_PRESENT, LOW_PC, ADDR, HIGH_PC, DATA8,
      FRAME_BASE, EXPRLOC, 0, 0,
    AB_PARAM, FORMAL_PARAMETER, 0, NAME, STRING, DECL_FILE, UDATA, DECL_LINE,
      UDATA, TYPE, REF4, LOCATION, EXPRLOC, 0, 0,
    AB_VAR, VARIABLE, 0, NAME, STRING, DECL_FILE, UDATA, DECL_LINE, UDATA,
      TYPE, REF4, LOCATION, EXPRLOC, 0, 0,
    AB_GVAR, VARIABLE, 0, NAME, STRING, DECL_FILE, UDATA, DECL_LINE, UDATA,
      TYPE, REF4, EXTERNAL, FLAG, LOCATION, EXPRLOC, 0, 0,
    AB_GVAR_NOLOC, VARIABLE, 0, NAME, STRING, DECL_FILE, UDATA, DECL_LINE,
      UDATA, TYPE, REF4, EXTERNAL, FLAG, 0, 0,
    AB_BASE, BASE_TYPE, 0, NAME, STRING, ENCODING, DATA1, BYTE_SIZE, DATA1, 0, 0,
    AB_PTR, POINTER_TYPE, 0, BYTE_SIZE, DATA1, TYPE, REF4, 0, 0,
    AB_PTR_VOID, POINTER_TYPE, 0, BYTE_SIZE, DATA1, 0, 0,
    AB_STRUCT, STRUCTURE_TYPE, 1, NAME, STRING, BYTE_SIZE, UDATA, 0, 0,
    AB_STRUCT_ANON, STRUCTURE_TYPE, 1, BYTE_SIZE, UDATA, 0, 0,
    AB_STRUCT_DECL, STRUCTURE_TYPE, 0, NAME, STRING, DECLARATION, FLAG_PRESENT, 0, 0,
    AB_UNION, UNION_TYPE, 1, NAME, STRING, BYTE_SIZE, UDATA, 0, 0,
    AB_UNION_ANON, UNION_TYPE, 1, BYTE_SIZE, UDATA, 0, 0,
    AB_UNION_DECL, UNION_TYPE, 0, NAME, STRING, DECLARATION, FLAG_PRESENT, 0, 0,
    AB_MEMBER, MEMBER, 0, NAME, STRING, TYPE, REF4, DATA_MEMBER_LOCATION, UDATA, 0, 0,
    AB_MEMBER_ANON, MEMBER, 0, TYPE, REF4, DATA_MEMBER_LOCATION, UDATA, 0, 0,
    AB_BITFIELD, MEMBER, 0, NAME, STRING, TYPE, REF4, BIT_SIZE, UDATA,
      DATA_BIT_OFFSET, UDATA, 0, 0,
    AB_ARRAY, ARRAY_TYPE, 1, TYPE, REF4, 0, 0,
    AB_SUBRANGE, SUBRANGE_TYPE, 0, COUNT, UDATA, 0, 0,
    AB_SUBRANGE_NOCOUNT, SUBRANGE_TYPE, 0, 0, 0,
    AB_ENUM, ENUMERATION_TYPE, 1, NAME, STRING, BYTE_SIZE, DATA1, 0, 0,
    AB_ENUM_ANON, ENUMERATION_TYPE, 1, BYTE_SIZE, DATA1, 0, 0,
    AB_ENUMERATOR, ENUMERATOR, 0, NAME, STRING, CONST_VALUE, SDATA, 0, 0,
    AB_SUBR, SUBROUTINE_TYPE, 1, PROTOTYPED, FLAG_PRESENT, TYPE, REF4, 0, 0,
    AB_SUBR_VOID, SUBROUTINE_TYPE, 1, PROTOTYPED, FLAG_PRESENT, 0, 0,
    AB_SUBR_PARAM, FORMAL_PARAMETER, 0, TYPE, REF4, 0, 0,
    AB_VARARGS, UNSPECIFIED_PARAMETERS, 0, 0, 0,
    AB_BLOCK, LEXICAL_BLOCK, 1, LOW_PC, ADDR, HIGH_PC, DATA8, 0, 0,
    AB_SUBRANGE_EXPR, SUBRANGE_TYPE, 0, COUNT, EXPRLOC, 0, 0,
    // DW_AT_GNU_vector (0x2107, two bytes in LEB128): an array that is a vector
    AB_VECTOR, ARRAY_TYPE, 1, 0x87, 0x42, FLAG_PRESENT, TYPE, REF4, 0, 0,
  };

  println("  .section .debug_abbrev,\"\",@progbits");
  println(".L.dbg.abbrev:");
  int n = sizeof(table) / sizeof(*table);
  for (int i = 0; i < n; i++)
    println("  .byte %d", table[i]);
  println("  .byte 0");
}

// A variable's location: a callee-saved register, or a place in the
// frame, at an offset from %rbp (the frame base), which for a variable
// aligned above 16 holds its address.
static void emit_var_location(Obj *var) {
  // A VLA's variable holds the array's address.
  if (var->ty->kind == TY_VLA) {
    char *ops = var_value_ops(var);
    println("  .byte %d,%s", byte_count(ops), ops);
    return;
  }
  if (var->reg) {
    println("  .byte 1,%d", 0x50 + dwarf_regs[var->reg - 1]); // DW_OP_regN
    return;
  }
  int len = 1 + leb128_len(var->offset, true) + var->is_overaligned;
  println("  .byte %d,0x91,%s%s", len, leb128(var->offset, true),  // DW_OP_fbreg
          var->is_overaligned ? ",6" : "");                         // DW_OP_deref
}

// Name, file and line, which the DIEs of functions and variables start with
static void emit_decl(char *name, Token *tok) {
  dw_string(name);
  dw_udata(file_number(tok));
  dw_udata(tok->line_no);
}

static bool is_user_var(Obj *var) {
  return var->tok && var->name[0] && strncmp(var->name, "__", 2) &&
         strncmp(var->name, ".L", 2);
}

// The block whose lexical block DIE `b`'s variables go in: b, or the
// nearest one around it whose code was written (see LexBlock). NULL is
// the function's own scope.
static LexBlock *written_block(LexBlock *b) {
  while (b && !b->emitted)
    b = b->parent;
  return b;
}

// The DIEs of the variables in `scope` (NULL: the function's), then of the
// lexical blocks directly in it, each with its own, in declaration order.
static void emit_scope_dies(Obj **vars, int n, LexBlock *scope) {
  for (int i = 0; i < n; i++) {
    Obj *var = vars[i];
    if (written_block(var->block) != scope)
      continue;
    dw_udata(AB_VAR);
    emit_decl(var->name, var->tok);
    dw_ref(type_die(var->ty));
    emit_var_location(var);
  }

  for (int i = 0; i < n; i++) {
    // The block in `scope` that vars[i] is in, if one is
    LexBlock *b = written_block(vars[i]->block);
    while (b && written_block(b->parent) != scope)
      b = written_block(b->parent);
    if (!b || b == scope || b->in_dwarf)
      continue;
    b->in_dwarf = true;
    dw_udata(AB_BLOCK);
    println("  .quad .L.block.%d.begin", b->id);
    println("  .quad .L.block.%d.end - .L.block.%d.begin", b->id, b->id);
    emit_scope_dies(vars, n, b);
    println("  .byte 0");
  }
}

static void emit_function_die(Obj *fn) {
  char *ret = type_die(fn->ty->return_ty);
  dw_udata(ret ? AB_FUNC : AB_FUNC_VOID);
  println("  .byte %d", !fn->is_static);
  emit_decl(fn->name, fn->tok);
  if (ret)
    dw_ref(ret);
  println("  .quad %s", fn->name);
  println("  .quad .L.end.%s - %s", fn->name, fn->name);
  println("  .byte 2,0x76,0"); // frame base: DW_OP_breg6 (%rbp) + 0

  // Parameters have no token of their own: they're shown at the
  // function's line.
  for (Obj *var = fn->params; var; var = var->next) {
    if (!var->name[0])
      continue;
    dw_udata(AB_PARAM);
    emit_decl(var->name, fn->tok);
    dw_ref(type_die(var->ty));
    emit_var_location(var);
  }

  // Locals, in the order they're declared (the list is newest first)
  int n = 0;
  for (Obj *var = fn->locals; var; var = var->next)
    if (is_user_var(var) && !is_param(fn, var))
      n++;
  Obj **vars = calloc(n, sizeof(Obj *));
  int i = n;
  for (Obj *var = fn->locals; var; var = var->next)
    if (is_user_var(var) && !is_param(fn, var))
      vars[--i] = var;
  emit_scope_dies(vars, n, NULL);
  println("  .byte 0");
}

static void emit_debug_info(Obj *prog) {
  emit_debug_abbrev();

  char cwd[4096];
  if (!getcwd(cwd, sizeof(cwd)))
    strcpy(cwd, ".");

  println("  .section .debug_info,\"\",@progbits");
  println(".L.dbg.info:");
  println("  .long .L.dbg.info.end - .L.dbg.info.start");
  println(".L.dbg.info.start:");
  println("  .value 4");
  println("  .long .L.dbg.abbrev");
  println("  .byte 8");

  dw_udata(AB_CU);
  dw_string("mucc");
  println("  .value 0x1d"); // DW_LANG_C11
  dw_string(base_file);
  dw_string(cwd);
  println("  .quad .L.text.start");
  println("  .quad .L.text.end - .L.text.start");
  println("  .long .L.dbg.line");

  // Functions in the order they're defined (the list is newest first)
  int n = 0;
  for (Obj *var = prog; var; var = var->next)
    n++;
  Obj **objs = calloc(n, sizeof(Obj *));
  int i = n;
  for (Obj *var = prog; var; var = var->next)
    objs[--i] = var;

  for (i = 0; i < n; i++) {
    Obj *fn = objs[i];
    if (fn->is_function && fn->is_definition && fn->is_live && fn->tok)
      emit_function_die(fn);
  }

  for (i = 0; i < n; i++) {
    Obj *var = objs[i];
    if (var->is_function || !var->is_definition || var->is_string ||
        !is_user_var(var))
      continue;
    // A thread-local variable's place depends on the thread; it's
    // described without one.
    dw_udata(var->is_tls ? AB_GVAR_NOLOC : AB_GVAR);
    emit_decl(var->name, var->tok);
    dw_ref(type_die(var->ty));
    println("  .byte %d", !var->is_static);
    if (!var->is_tls) {
      println("  .byte 9,3"); // DW_OP_addr
      println("  .quad %s", var->name);
    }
  }

  emit_type_dies();
  println("  .byte 0"); // the end of the compile unit's children
  println(".L.dbg.info.end:");

  // The assembler puts the line table here.
  println("  .section .debug_line,\"\",@progbits");
  println(".L.dbg.line:");
}

//---------- Entry point -----------------------------------------------------

void codegen(Obj *prog, FILE *out) {
  // Names the object's source file in its symbol table. Without it, ld
  // uses the temporary .o's random name, and no two builds are identical.
  println("  .file \"%s\"", base_file);

  File **files = get_input_files();
  for (int i = 0; files[i]; i++)
    println("  .file %d \"%s\"", files[i]->file_no, files[i]->name);

  for (Obj *fn = prog; fn; fn = fn->next)
    if (fn->is_function && fn->is_definition)
      assign_registers(fn);
  assign_lvar_offsets(prog);
  for (Obj *var = prog; var; var = var->next)
    has_weak |= var->is_weak;
  emit_data(prog);
  if (opt_g) {
    println("  .text");
    println(".L.text.start:");
  }
  emit_text(prog);
  emit_int128_routines();
  if (opt_g) {
    println("  .text");
    println(".L.text.end:");
    emit_debug_info(prog);
  }
  emit_init_arrays(prog);
  emit_aliases(prog);
  emit_weak_refs(prog);

  // asm("...") at file scope, as it is
  for (int i = 0; i < toplevel_asm.len; i++) {
    has_inline_asm = true;
    println("%s", toplevel_asm.data[i]);
  }

  // Mark the stack as not executable, as gcc does. The built-in assembler
  // would add this anyway, but GNU `as` (used for asm() it can't handle)
  // needs to be told, or ld warns that the stack is executable.
  println("  .section .note.GNU-stack,\"\",@progbits");

  fwrite(out_buf, 1, out_len, out);
}
