//============================================================================
// cgen.c - STAGE 4 of 4: CODEGEN
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
static void gen_stmt(Node *node);

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
      println("  lea %d(%%rbp), %%rax", node->var->offset);
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
    gen_expr(node->lhs);
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
  if (is_aggregate(ty))
    return;
  load_from(ty, "(%rax)");
}

// Store the scalar in %rax (or %xmm0, or the x87 stack) to memory
// operand `addr`, e.g. "(%rdi)".
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

static void cmp_zero(Type *ty) {
  switch (ty->kind) {
  case TY_FLOAT:
    println("  xorps %%xmm1, %%xmm1");
    println("  ucomiss %%xmm1, %%xmm0");
    return;
  case TY_DOUBLE:
    println("  xorpd %%xmm1, %%xmm1");
    println("  ucomisd %%xmm1, %%xmm0");
    return;
  case TY_LDOUBLE:
    println("  fldz");
    println("  fucomip");
    println("  fstp %%st(0)");
    return;
  }

  if (is_integer(ty) && ty->size <= 4)
    println("  cmp $0, %%eax");
  else
    println("  cmp $0, %%rax");
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

static char u64f32[] = "cvtsi2ssq %rax, %xmm0";
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
static char f32u64[] = "cvttss2siq %xmm0, %rax";
static char f32f64[] = "cvtss2sd %xmm0, %xmm0";
static char f32f80[] = "movss %xmm0, -4(%rsp); flds -4(%rsp)";

static char f64i8[] = "cvttsd2sil %xmm0, %eax; movsbl %al, %eax";
static char f64u8[] = "cvttsd2sil %xmm0, %eax; movzbl %al, %eax";
static char f64i16[] = "cvttsd2sil %xmm0, %eax; movswl %ax, %eax";
static char f64u16[] = "cvttsd2sil %xmm0, %eax; movzwl %ax, %eax";
static char f64i32[] = "cvttsd2sil %xmm0, %eax";
static char f64u32[] = "cvttsd2siq %xmm0, %rax";
static char f64i64[] = "cvttsd2siq %xmm0, %rax";
static char f64u64[] = "cvttsd2siq %xmm0, %rax";
static char f64f32[] = "cvtsd2ss %xmm0, %xmm0";
static char f64f80[] = "movsd %xmm0, -8(%rsp); fldl -8(%rsp)";

#define FROM_F80_1                                           \
  "fnstcw -10(%rsp); movzwl -10(%rsp), %eax; or $12, %ah; " \
  "mov %ax, -12(%rsp); fldcw -12(%rsp); "

#define FROM_F80_2 " -24(%rsp); fldcw -10(%rsp); "

static char f80i8[] = FROM_F80_1 "fistps" FROM_F80_2 "movsbl -24(%rsp), %eax";
static char f80u8[] = FROM_F80_1 "fistps" FROM_F80_2 "movzbl -24(%rsp), %eax";
static char f80i16[] = FROM_F80_1 "fistps" FROM_F80_2 "movzbl -24(%rsp), %eax";
static char f80u16[] = FROM_F80_1 "fistpl" FROM_F80_2 "movswl -24(%rsp), %eax";
static char f80i32[] = FROM_F80_1 "fistpl" FROM_F80_2 "mov -24(%rsp), %eax";
static char f80u32[] = FROM_F80_1 "fistpl" FROM_F80_2 "mov -24(%rsp), %eax";
static char f80i64[] = FROM_F80_1 "fistpq" FROM_F80_2 "mov -24(%rsp), %rax";
static char f80u64[] = FROM_F80_1 "fistpq" FROM_F80_2 "mov -24(%rsp), %rax";
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
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  setne %%al");
    println("  movzx %%al, %%eax");
    return;
  }

  int t1 = getTypeId(from);
  int t2 = getTypeId(to);
  if (cast_table[t1][t2])
    println("  %s", cast_table[t1][t2]);
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
  if (!is_integer(node->ty) && !is_ptr_cast)
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

static bool is_int_or_ptr(Type *ty) {
  return (is_integer(ty) && ty->kind != TY_BOOL) || ty->kind == TY_PTR;
}

// A local scalar variable, whose value can be read straight from the
// stack frame. Returns NULL for anything else.
static Obj *local_scalar(Node *node) {
  if (node->kind == ND_VAR && node->var->is_local && !is_aggregate(node->ty))
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

  return offset < lo || hi <= offset || ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

static bool has_flonum1(Type *ty) {
  return has_flonum(ty, 0, 8, 0);
}

static bool has_flonum2(Type *ty) {
  return has_flonum(ty, 8, 16, 0);
}

// Counts the registers a struct or union of 16 bytes or less needs: one
// per 8-byte half, an XMM register if that half holds only floating-point
// values, otherwise a general-purpose one.
static void struct_regs(Type *ty, int *ngp, int *nfp) {
  *ngp = *nfp = 0;
  if (has_flonum1(ty))
    (*nfp)++;
  else
    (*ngp)++;

  if (ty->size > 8) {
    if (has_flonum2(ty))
      (*nfp)++;
    else
      (*ngp)++;
  }
}

// Is a struct or union passed in registers, when `gp` general-purpose and
// `fp` XMM registers are already taken? It goes either entirely in
// registers or entirely on the stack. The caller and the callee must
// both decide this the same way, so both use this function.
static bool struct_in_regs(Type *ty, int gp, int fp) {
  if (ty->size > 16)
    return false;
  int ngp, nfp;
  struct_regs(ty, &ngp, &nfp);
  return gp + ngp <= GP_MAX && fp + nfp <= FP_MAX;
}

static void push_struct(Type *ty) {
  int sz = align_to(ty->size, 8);
  println("  sub $%d, %%rsp", sz);
  depth += sz / 8;

  for (int i = 0; i < ty->size; i++) {
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
  default:
    push();
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
  if (node->ret_buffer && node->ty->size > 16)
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
        arg->pass_by_stack = true;
        stack += align_to(ty->size, 8) / 8;
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
      stack += 2;
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
  if (node->ret_buffer && node->ty->size > 16) {
    println("  lea %d(%%rbp), %%rax", node->ret_buffer->offset);
    push();
  }

  return stack;
}

static void copy_ret_buffer(Obj *var) {
  Type *ty = var->ty;
  int gp = 0, fp = 0;

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

  if (ty->size > 8) {
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

  if (ty->size > 8) {
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

  for (int i = 0; i < ty->size; i++) {
    println("  mov %d(%%rax), %%dl", i);
    println("  mov %%dl, %d(%%rdi)", i);
  }
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
    if (is_flonum(cond->lhs->ty))
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
// popped off the x87 stack.)
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
}

//---------- Expressions -----------------------------------------------------

// Tells the assembler which source line the next instructions come
// from, for debuggers. Most expressions share a line with the statement
// around them, so only emit a .loc when the line actually changes.
static void emit_loc(Token *tok) {
  if (tok->file->file_no == loc_file && tok->line_no == loc_line)
    return;
  loc_file = tok->file->file_no;
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

  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM: {
    switch (node->ty->kind) {
    case TY_FLOAT: {
      union { float f32; uint32_t u32; } u = { node->fval };
      println("  mov $%u, %%eax  # float %Lf", u.u32, node->fval);
      println("  movq %%rax, %%xmm0");
      return;
    }
    case TY_DOUBLE: {
      union { double f64; uint64_t u64; } u = { node->fval };
      println("  mov $%lu, %%rax  # double %Lf", u.u64, node->fval);
      println("  movq %%rax, %%xmm0");
      return;
    }
    case TY_LDOUBLE: {
      union { long double f80; uint64_t u64[2]; } u;
      memset(&u, 0, sizeof(u));
      u.f80 = node->fval;
      println("  mov $%lu, %%rax  # long double %Lf", u.u64[0], node->fval);
      println("  mov %%rax, -16(%%rsp)");
      println("  mov $%lu, %%rax", u.u64[1]);
      println("  mov %%rax, -8(%%rsp)");
      println("  fldt -16(%%rsp)");
      return;
    }
    }

    println("  mov $%ld, %%rax", node->val);
    return;
  }
  case ND_NEG:
    gen_expr(node->lhs);

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
      if (base->kind == ND_VAR && base->var->is_local && base->ty->kind != TY_VLA) {
        snprintf(addr, sizeof(addr), "%d(%%rbp)", base->var->offset + mem->offset);
      } else {
        gen_addr(base);
        snprintf(addr, sizeof(addr), "%d(%%rax)", mem->offset);
      }
      load_from(node->ty, addr);
    }

    if (mem->is_bitfield) {
      println("  shl $%d, %%rax", 64 - mem->bit_width - mem->bit_offset);
      if (mem->ty->is_unsigned)
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
      println("  mov %%rax, %%rdi");
      println("  mov $%ld, %%r9", (1L << mem->bit_width) - 1);
      println("  and %%r9, %%rdi");
      println("  shl $%d, %%rdi", mem->bit_offset);

      println("  mov (%%rsp), %%rax");
      load(mem->ty);

      long mask = ((1L << mem->bit_width) - 1) << mem->bit_offset;
      println("  mov $%ld, %%r9", ~mask);
      println("  and %%r9, %%rax");
      println("  or %%rdi, %%rax");
      store(node->ty);
      println("  mov %%r8, %%rax");
      return;
    }

    store(node->ty);
    return;
  case ND_STMT_EXPR:
    // The value of the last statement, if it's an expression, is the
    // result, so it's computed with gen_expr, not discarded.
    for (Node *n = node->body; n; n = n->next) {
      if (!n->next && n->kind == ND_EXPR_STMT)
        gen_expr(n->lhs);
      else
        gen_stmt(n);
    }
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
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
    println("  lea %d(%%rbp), %%rdi", node->var->offset);
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
    println("  not %%rax");
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
    if (node->ret_buffer && node->ty->size > 16)
      pop(argreg64[gp++]);

    for (Node *arg = node->args; arg; arg = arg->next) {
      Type *ty = arg->ty;

      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (arg->pass_by_stack)
          continue;

        if (has_flonum1(ty))
          popf(fp++);
        else
          pop(argreg64[gp++]);

        if (ty->size > 8) {
          if (has_flonum2(ty))
            popf(fp++);
          else
            pop(argreg64[gp++]);
        }
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        if (fp < FP_MAX)
          popf(fp++);
        break;
      case TY_LDOUBLE:
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
    if (node->ret_buffer && node->ty->size <= 16) {
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
  case ND_CAS: {
    gen_expr(node->cas_addr);
    push();
    gen_expr(node->cas_new);
    push();
    gen_expr(node->cas_old);
    println("  mov %%rax, %%r8");
    load(node->cas_old->ty->base);
    pop("%rdx"); // new
    pop("%rdi"); // addr

    int sz = node->cas_addr->ty->base->size;
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
      println("  ucomi%s %%xmm0, %%xmm1", sz);

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
    gen_expr(node->lhs);
    gen_expr(node->rhs);

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
      println("  fcomip");
      println("  fstp %%st(0)");

      if (node->kind == ND_EQ)
        println("  sete %%al");
      else if (node->kind == ND_NE)
        println("  setne %%al");
      else if (node->kind == ND_LT)
        println("  seta %%al");
      else
        println("  setae %%al");

      println("  movzb %%al, %%rax");
      return;
    }

    error_tok(node->tok, "invalid expression");
  }
  }

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

//---------- Statements ------------------------------------------------------

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

    for (Node *n = node->case_next; n; n = n->case_next) {
      char *ax = (node->cond->ty->size == 8) ? "%rax" : "%eax";
      char *di = (node->cond->ty->size == 8) ? "%rdi" : "%edi";

      if (n->begin == n->end) {
        println("  cmp $%ld, %s", n->begin, ax);
        println("  je %s", n->label);
        continue;
      }

      // [GNU] Case ranges
      println("  mov %s, %s", ax, di);
      println("  sub $%ld, %s", n->begin, di);
      println("  cmp $%ld, %s", n->end - n->begin, di);
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
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_GOTO:
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
        if (ty->size <= 16)
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
  case ND_ASM:
    has_inline_asm = true;
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
  case ND_ADDR:
    if (node->lhs->kind == ND_VAR)
      node->lhs->var->is_addr_taken = true;
    break;
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
  scan_uses(node->cond, weight);
  scan_uses(node->then, weight);
  scan_uses(node->els, weight);
  scan_uses(node->init, weight);
  scan_uses(node->inc, weight);
  scan_uses(node->cas_addr, weight);
  scan_uses(node->cas_old, weight);
  scan_uses(node->cas_new, weight);
  for (Node *n = node->body; n; n = n->next)
    scan_uses(n, weight);
  for (Node *n = node->args; n; n = n->next)
    scan_uses(n, weight);
}

static bool can_be_in_register(Obj *fn, Obj *var) {
  Type *ty = var->ty;
  return !var->is_addr_taken && var != fn->alloca_bottom && !ty->is_atomic &&
         (is_integer(ty) || ty->kind == TY_PTR);
}

// Gives the most used eligible variables of `fn` a register each.
static void assign_registers(Obj *fn) {
  no_register_vars = false;
  scan_uses(fn->body, 1);
  if (no_register_vars)
    return;

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
        if (fp++ < FP_MAX)
          continue;
        break;
      case TY_LDOUBLE:
        break;
      default:
        if (gp++ < GP_MAX)
          continue;
      }

      top = align_to(top, 8);
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

// `.local`, `.globl` or `.weak`
static void emit_binding(Obj *prog, Obj *var) {
  if (var->is_static)
    println("  .local %s", var->name);
  else if (is_weak(prog, var))
    println("  .weak %s", var->name);
  else
    println("  .globl %s", var->name);
}

// Weak declarations that are used: a missing definition is then 0, not a
// link error.
static void emit_weak_refs(Obj *prog) {
  for (Obj *var = prog; var; var = var->next)
    if (var->is_weak && !var->is_definition && var->is_used)
      println("  .weak %s", var->name);
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

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function || !var->is_definition)
      continue;

    emit_binding(prog, var);

    int align = (var->ty->kind == TY_ARRAY && var->ty->size >= 16)
      ? MAX(16, var->align) : var->align;

    // Common symbol (never for a weak one, as with gcc)
    if (opt_fcommon && var->is_tentative && !is_weak(prog, var)) {
      println("  .comm %s, %d, %d", var->name, var->ty->size, align);
      continue;
    }

    // .data or .tdata
    if (var->init_data) {
      if (var->is_tls)
        println("  .section .tdata,\"awT\",@progbits");
      else
        println("  .data");

      println("  .type %s, @object", var->name);
      println("  .size %s, %d", var->name, var->ty->size);
      println("  .align %d", align);
      println("%s:", var->name);

      Relocation *rel = var->rel;
      int pos = 0;
      while (pos < var->ty->size) {
        if (rel && rel->offset == pos) {
          println("  .quad %s%+ld", *rel->label, rel->addend);
          rel = rel->next;
          pos += 8;
        } else {
          println("  .byte %d", var->init_data[pos++]);
        }
      }
      continue;
    }

    // .bss or .tbss
    if (var->is_tls)
      println("  .section .tbss,\"awT\",@nobits");
    else
      println("  .bss");

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
    println("  .text");
    println("  .type %s, @function", fn->name);
    println("%s:", fn->name);
    current_fn = fn;
    loc_line = 0;

    // Prologue
    println("  push %%rbp");
    println("  mov %%rsp, %%rbp");
    println("  sub $%d, %%rsp", fn->stack_size);
    println("  mov %%rsp, %d(%%rbp)", fn->alloca_bottom->offset);
    for (int i = 0; i < fn->nregs; i++)
      println("  mov %s, %d(%%rbp)", regs64[i], fn->regs_offset + i * 8);

    // Save arg registers if function is variadic
    if (fn->va_area) {
      int gp = 0, fp = 0;
      for (Obj *var = fn->params; var; var = var->next) {
        if (is_flonum(var->ty))
          fp++;
        else
          gp++;
      }

      int off = fn->va_area->offset;

      // va_elem
      println("  movl $%d, %d(%%rbp)", gp * 8, off);          // gp_offset
      println("  movl $%d, %d(%%rbp)", fp * 8 + 48, off + 4); // fp_offset
      println("  movq %%rbp, %d(%%rbp)", off + 8);            // overflow_arg_area
      println("  addq $16, %d(%%rbp)", off + 8);
      println("  movq %%rbp, %d(%%rbp)", off + 16);           // reg_save_area
      println("  addq $%d, %d(%%rbp)", off + 24, off + 16);

      // __reg_save_area__
      println("  movq %%rdi, %d(%%rbp)", off + 24);
      println("  movq %%rsi, %d(%%rbp)", off + 32);
      println("  movq %%rdx, %d(%%rbp)", off + 40);
      println("  movq %%rcx, %d(%%rbp)", off + 48);
      println("  movq %%r8, %d(%%rbp)", off + 56);
      println("  movq %%r9, %d(%%rbp)", off + 64);
      println("  movsd %%xmm0, %d(%%rbp)", off + 72);
      println("  movsd %%xmm1, %d(%%rbp)", off + 80);
      println("  movsd %%xmm2, %d(%%rbp)", off + 88);
      println("  movsd %%xmm3, %d(%%rbp)", off + 96);
      println("  movsd %%xmm4, %d(%%rbp)", off + 104);
      println("  movsd %%xmm5, %d(%%rbp)", off + 112);
      println("  movsd %%xmm6, %d(%%rbp)", off + 120);
      println("  movsd %%xmm7, %d(%%rbp)", off + 128);
    }

    // Save passed-by-register arguments to the stack, or move them to
    // their registers.
    int gp = 0, fp = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      // Passed on the stack (only a register variable moves)
      if (var->offset > 0) {
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
        if (has_flonum(ty, 0, 8, 0))
          store_fp(fp++, var->offset, MIN(8, ty->size));
        else
          store_gp(gp++, var->offset, MIN(8, ty->size));

        if (ty->size > 8) {
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
      default:
        store_gp(gp++, var->offset, ty->size);
      }
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
  }
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
  emit_text(prog);
  emit_init_arrays(prog);
  emit_weak_refs(prog);

  // Mark the stack as not executable, as gcc does. The built-in assembler
  // would add this anyway, but GNU `as` (used for asm() it can't handle)
  // needs to be told, or ld warns that the stack is executable.
  println("  .section .note.GNU-stack,\"\",@progbits");

  fwrite(out_buf, 1, out_len, out);
}
