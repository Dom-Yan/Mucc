//============================================================================
// cgen_arm64.c - STAGE 4 of 6, FOR ARM64: CODEGEN
//
// Walks the AST and prints ARM64 (AArch64) assembly, as cgen.c does for
// x86-64, and in the same way: a stack machine. Each expression leaves
// its value in x0 (w0 for 32 bits or less: an integer, a pointer or a
// struct's address), in v0 (s0, d0 or q0: a float, double, long double
// or vector) or in x0:x1 (an __int128, its low half in x0), and partial
// results wait on the stack, 16 bytes each, since sp must stay 16-byte
// aligned. Calls follow the AAPCS64, so mucc's objects link with gcc's.
//
// A value of 32 bits or less is extended to 32 bits as its type says (a
// char's w0 holds -1 or 255, never 0x1ff); the upper half of x0 may be
// anything until a cast to a 64-bit type.
//
// ARM64 has no instructions for long double, IEEE quadruple precision,
// so its arithmetic calls the routines gcc's runtime library has
// (__addtf3 and the rest), as gcc does, and so do __int128 division and
// conversions. With --libc=mucc, runtime/ has them.
//
// What both code generators share (constant folding, register variables,
// data, debug info and asm templates) is in cgen.c, whose codegen() calls
// assign_frames_arm64() and emit_text_arm64() here.
//============================================================================

#include "mucc.h"

//---------- Codegen state and registers -------------------------------------
//
// Scratch registers: x0-x1 and v0-v1 hold values, x9-x15 hold addresses
// and temporaries, x16 an address too large for an instruction's offset
// (see mem()), x17 a large constant (see add_imm()). x19-x28 hold
// register variables (see "Register variables" in cgen.c), x29 is the
// frame pointer.

static Obj *current_fn;
static int depth; // values on the stack, 16 bytes each

static char *xreg[] = {
  "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
  "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21", "x22",
  "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30",
};

static char *wreg[] = {
  "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "w10", "w11",
  "w12", "w13", "w14", "w15", "w16", "w17", "w18", "w19", "w20", "w21", "w22",
  "w23", "w24", "w25", "w26", "w27", "w28", "w29", "w30",
};

static void gen_expr(Node *node);
static void gen_discard(Node *node);
static void gen_stmt(Node *node);
static void cast(Type *from, Type *to);

// A memory operand (see "Memory operands")
typedef struct {
  char *base; // "x0", "x29" or a register variable's register
  int64_t off;
} MemOp;

static MemOp gen_memop(Node *node);
static bool free_memop(Node *node, MemOp *m);
static bool is_simple_load(Type *ty);

// Register `n` for a value of `size` bytes: "x5" for 8, "w5" for less
static char *reg(int n, int size) {
  return size == 8 ? xreg[n] : wreg[n];
}

// The register a register variable is in, x19 and up, sized for its type
static char *var_reg(Obj *var) {
  return reg(18 + var->reg, var->ty->size);
}

// The letter of an FP/SIMD register holding `size` bytes: s0, d0, q0
static char fp_letter(int size) {
  return size == 2 ? 'h' : size == 4 ? 's' : size == 8 ? 'd' : 'q';
}

// Whether an integer, enum or pointer value takes all of x0. (An array or
// a function is its address.)
static bool is_64bit(Type *ty) {
  return ty->size == 8 || ty->base || ty->kind == TY_FUNC;
}

// The register a value of type `ty` is in: x0 or w0
static char *x0_or_w0(Type *ty) {
  return is_64bit(ty) ? "x0" : "w0";
}

//---------- Stack, immediates and addresses ---------------------------------

// Pushes x0. It's written only if the next instruction isn't the pop
// that would read it back (see pop() and cgen.c's println()).
static void push(void) {
  flush_push();
  pending_push = "  str x0, [sp, -16]!\n";
  depth++;
}

// Pops the value on top of the stack into register `r` ("x1").
static void pop(char *r) {
  if (pending_push) {
    pending_push = NULL;
    if (strcmp(r, "x0"))
      println("  mov %s, x0", r);
  } else {
    println("  ldr %s, [sp], 16", r);
  }
  depth--;
}

// A float, double, long double or vector, as all of q0
static void push_fp(void) {
  println("  str q0, [sp, -16]!");
  depth++;
}

static void pop_fp(int r) {
  println("  ldr q%d, [sp], 16", r);
  depth--;
}

static void push128(void) {
  println("  stp x0, x1, [sp, -16]!");
  depth++;
}

static void pop128(int lo, int hi) {
  println("  ldp x%d, x%d, [sp], 16", lo, hi);
  depth--;
}

// Sets register `r` (an x or a w) to `val`: a mov, if one can, else a
// movz (or movn, if most of it is one bits) and a movk for each other
// 16 bits.
static void load_imm(char *r, int64_t val) {
  bool is32 = r[0] == 'w';
  uint64_t u = is32 ? (uint32_t)val : (uint64_t)val;
  uint64_t inv = is32 ? (uint32_t)~u : ~u;
  if (u < 0x10000 || inv < 0x10000) {
    println("  mov %s, %ld", r, is32 ? (long)(int32_t)u : (long)u);
    return;
  }

  int bits = is32 ? 32 : 64;
  int zeros = 0, ones = 0;
  for (int s = 0; s < bits; s += 16) {
    zeros += ((u >> s) & 0xffff) == 0;
    ones += ((u >> s) & 0xffff) == 0xffff;
  }
  bool neg = ones > zeros;
  bool first = true;
  for (int s = 0; s < bits; s += 16) {
    unsigned chunk = (u >> s) & 0xffff;
    if (chunk == (neg ? 0xffff : 0))
      continue;
    if (first)
      println("  %s %s, %u, lsl %d", neg ? "movn" : "movz", r,
              neg ? ~chunk & 0xffff : chunk, s);
    else
      println("  movk %s, %u, lsl %d", r, chunk, s);
    first = false;
  }
}

// dst = src + val, for any val: an add or sub of up to 24 bits, or one
// through x17. Either register may be sp.
static void add_imm(char *dst, char *src, int64_t val) {
  char *op = val < 0 ? "sub" : "add";
  uint64_t u = val < 0 ? -(uint64_t)val : (uint64_t)val;
  if (u >= (1 << 24)) {
    load_imm("x17", val);
    println("  add %s, %s, x17", dst, src);
    return;
  }
  if (u >> 12) {
    println("  %s %s, %s, %lu, lsl 12", op, dst, src, u >> 12);
    src = dst;
  }
  if ((u & 0xfff) || strcmp(dst, src))
    println("  %s %s, %s, %lu", op, dst, src, u & 0xfff);
}

// The memory operand of a `size`-byte load or store at `base` + `off`:
// "[x29, 16]". A load or store takes an offset from -256 to 255, or a
// multiple of its size up to 4095 times it; another is added into x16
// first. The string is overwritten by the fourth call after.
static char *mem(char *base, int64_t off, int size) {
  static char bufs[4][32];
  static int n;
  char *buf = bufs[n++ % 4];
  if ((off < -256 || off > 255) && (off < 0 || off % size || off / size > 4095)) {
    add_imm("x16", base, off);
    base = "x16";
    off = 0;
  }
  if (off)
    snprintf(buf, 32, "[%s, %ld]", base, off);
  else
    snprintf(buf, 32, "[%s]", base);
  return buf;
}

// Copies `size` bytes from where register `src` points to where `dst`
// does (both stay as they are), 8 bytes at a time while it can. A large
// copy is a loop, counting down in x11.
static void copy_bytes(char *dst, char *src, int64_t size) {
  int64_t i = 0;
  if (size > 256) {
    i = size / 8 * 8;
    load_imm("x11", i);
    println("1:");
    println("  sub x11, x11, 8");
    println("  ldr x10, [%s, x11]", src);
    println("  str x10, [%s, x11]", dst);
    println("  cbnz x11, 1b");
  }
  for (; i + 8 <= size; i += 8) {
    println("  ldr x10, %s", mem(src, i, 8));
    println("  str x10, %s", mem(dst, i, 8));
  }
  for (; i + 4 <= size; i += 4) {
    println("  ldr w10, %s", mem(src, i, 4));
    println("  str w10, %s", mem(dst, i, 4));
  }
  for (; i < size; i++) {
    println("  ldrb w10, %s", mem(src, i, 1));
    println("  strb w10, %s", mem(dst, i, 1));
  }
}

// Zeroes `size` bytes at `base` + `off`.
static void zero_bytes(char *base, int64_t off, int64_t size) {
  int64_t i = 0;
  if (size > 256) {
    add_imm("x10", base, off);
    i = size / 8 * 8;
    load_imm("x11", i);
    println("1:");
    println("  sub x11, x11, 8");
    println("  str xzr, [x10, x11]");
    println("  cbnz x11, 1b");
  }
  for (; i + 8 <= size; i += 8)
    println("  str xzr, %s", mem(base, off + i, 8));
  for (; i + 4 <= size; i += 4)
    println("  str wzr, %s", mem(base, off + i, 4));
  for (; i < size; i++)
    println("  strb wzr, %s", mem(base, off + i, 1));
}

// Stores the low `n` bytes (1 to 8) of x`r` at `base` + `off`: in one
// store if it can, else a byte at a time, shifting x`r` down.
static void store_reg_bytes(int r, char *base, int64_t off, int n) {
  switch (n) {
  case 1: println("  strb %s, %s", wreg[r], mem(base, off, 1)); return;
  case 2: println("  strh %s, %s", wreg[r], mem(base, off, 2)); return;
  case 4: println("  str %s, %s", wreg[r], mem(base, off, 4)); return;
  case 8: println("  str %s, %s", xreg[r], mem(base, off, 8)); return;
  }
  for (int i = 0; i < n; i++) {
    println("  strb %s, %s", wreg[r], mem(base, off + i, 1));
    println("  lsr %s, %s, 8", xreg[r], xreg[r]);
  }
}

// Loads the `n` bytes (1 to 8) at `base` + `off` into x`r`, zero-extended:
// in one load if it can, else a byte at a time, without reading past
// them. `base` may be x`r`, but not x14 or x15.
static void load_reg_bytes(int r, char *base, int64_t off, int n) {
  switch (n) {
  case 1: println("  ldrb %s, %s", wreg[r], mem(base, off, 1)); return;
  case 2: println("  ldrh %s, %s", wreg[r], mem(base, off, 2)); return;
  case 4: println("  ldr %s, %s", wreg[r], mem(base, off, 4)); return;
  case 8: println("  ldr %s, %s", xreg[r], mem(base, off, 8)); return;
  }
  println("  mov x15, 0");
  for (int i = n - 1; i >= 0; i--) {
    println("  ldrb w14, %s", mem(base, off + i, 1));
    println("  orr x15, x14, x15, lsl 8");
  }
  println("  mov %s, x15", xreg[r]);
}

//---------- Addresses, loads and stores -------------------------------------

// Puts the address of local `var` in register `r`. One aligned above 16
// is away from the frame, and its slot holds its address (see
// assign_frames_arm64()).
static void addr_of_local(Obj *var, char *r) {
  if (var->is_overaligned)
    println("  ldr %s, %s", r, mem("x29", var->offset, 8));
  else
    add_imm(r, "x29", var->offset);
}

// Puts the address of global `var` in x0: pc-relative, from the page
// adrp finds and the offset in it; through the GOT with -fpic, unless it's
// static, and without it, if it's defined in another file, as gcc does;
// or a thread-local one's, from the thread pointer.
static void addr_of_global(Obj *var) {
  char *name = var->name;
  if (var->is_tls) {
    if (opt_fpic) {
      // A TLS descriptor call, which keeps every register but x0
      println("  adrp x0, :tlsdesc:%s", name);
      println("  ldr x1, [x0, :tlsdesc_lo12:%s]", name);
      println("  add x0, x0, :tlsdesc_lo12:%s", name);
      println("  .tlsdesccall %s", name);
      println("  blr x1");
      println("  mrs x1, tpidr_el0");
      println("  add x0, x1, x0");
      return;
    }
    println("  mrs x0, tpidr_el0");
    println("  add x0, x0, :tprel_hi12:%s, lsl 12", name);
    println("  add x0, x0, :tprel_lo12_nc:%s", name);
    return;
  }
  // One defined in another file may be in a shared library, which adrp
  // can't reach from a PIE (as gcc links by default on ARM64), and a weak
  // declaration may have no definition anywhere and be 0: ld would give
  // that the address of adrp's own page.
  if (opt_fpic ? !var->is_static : !var->is_definition) {
    println("  adrp x0, :got:%s", name);
    println("  ldr x0, [x0, :got_lo12:%s]", name);
    return;
  }
  println("  adrp x0, %s", name);
  println("  add x0, x0, :lo12:%s", name);
}

// Computes the address of `node`, an lvalue, into x0.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    // A register variable has no address; one whose address is taken
    // is never put in a register.
    if (node->var->reg)
      unreachable();
    // A variable-length array's slot holds its address.
    if (node->var->ty->kind == TY_VLA) {
      println("  ldr x0, %s", mem("x29", node->var->offset, 8));
      return;
    }
    if (node->var->is_local)
      addr_of_local(node->var, "x0");
    else
      addr_of_global(node->var);
    return;
  case ND_DEREF:
  case ND_MEMBER: {
    // In one add from its base: s.a.b, or p->a.b with p in a register.
    MemOp m = gen_memop(node);
    add_imm("x0", m.base, m.off);
    return;
  }
  case ND_COMMA:
    gen_discard(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_FUNCALL:
    if (node->ret_buffer) {
      gen_expr(node);
      return;
    }
    break;
  case ND_ASSIGN:
  case ND_COND:
  case ND_STMT_EXPR: // `({ struct S t = ...; t; }).x`
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
      gen_expr(node);
      return;
    }
    break;
  case ND_VLA_PTR:
    add_imm("x0", "x29", node->var->offset);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Loads a `ty` at `base` + `off` into x0 (w0), v0 or x0:x1.
static void load_from(Type *ty, char *base, int64_t off) {
  switch (ty->kind) {
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
  case TY_VECTOR:
    println("  ldr %c0, %s", fp_letter(ty->size), mem(base, off, ty->size));
    return;
  case TY_INT128:
    // The high half first: `base` may be x0.
    println("  ldr x1, %s", mem(base, off + 8, 8));
    println("  ldr x0, %s", mem(base, off, 8));
    return;
  }

  switch (ty->size) {
  case 1:
    println("  %s w0, %s", ty->is_unsigned ? "ldrb" : "ldrsb", mem(base, off, 1));
    return;
  case 2:
    println("  %s w0, %s", ty->is_unsigned ? "ldrh" : "ldrsh", mem(base, off, 2));
    return;
  case 4:
    println("  ldr w0, %s", mem(base, off, 4));
    return;
  }
  println("  ldr x0, %s", mem(base, off, 8));
}

// Load a value from where x0 points to. An array, a struct or a function
// isn't loaded: its value is its address.
static void load(Type *ty) {
  if (is_aggregate(ty) || ty->kind == TY_VOID) // `*p` on a void *p reads nothing
    return;
  load_from(ty, "x0", 0);
}

// Stores the scalar in x0, v0 or x0:x1 to `base` + `off`.
static void store_to(Type *ty, char *base, int64_t off) {
  switch (ty->kind) {
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
  case TY_VECTOR:
    println("  str %c0, %s", fp_letter(ty->size), mem(base, off, ty->size));
    return;
  case TY_INT128:
    println("  str x0, %s", mem(base, off, 8));
    println("  str x1, %s", mem(base, off + 8, 8));
    return;
  }
  store_reg_bytes(0, base, off, ty->size);
}

// Local scalar `var`'s value, from its register or its slot
static void load_var(Obj *var) {
  if (var->reg)
    println("  mov %s, %s", x0_or_w0(var->ty), var_reg(var));
  else
    load_from(var->ty, "x29", var->offset);
}

static void store_var(Obj *var) {
  if (var->reg)
    println("  mov %s, %s", var_reg(var), x0_or_w0(var->ty));
  else
    store_to(var->ty, "x29", var->offset);
}

// Stores the value to the address on top of the stack. A struct's value
// is its address, and its bytes are copied.
static void store(Type *ty) {
  pop("x9");
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    copy_bytes("x9", "x0", ty->size);
  else
    store_to(ty, "x9", 0);
}

//---------- Bit-fields ------------------------------------------------------
//
// A bit-field is loaded and stored as its unit, the mem->unit bytes at
// its offset, in x0, or in x0:x1 for a wide one: an __int128 one, or a
// packed one over 8 bytes (a long at an odd bit).

static bool is_wide(Member *mem) {
  return is_int128(mem->ty) || mem->unit > 8;
}

static void load_unit(Member *mem, char *base, int64_t off) {
  if (mem->unit == 16) {
    load_from(ty_int128, base, off);
  } else if (is_wide(mem)) {
    load_reg_bytes(1, base, off + 8, mem->unit - 8);
    load_reg_bytes(0, base, off, 8);
  } else {
    load_reg_bytes(0, base, off, mem->unit);
  }
}

// Stores the unit in x0 (or x0:x1) to where x9 points.
static void store_unit(Member *mem) {
  store_reg_bytes(0, "x9", 0, MIN(mem->unit, 8));
  if (is_wide(mem))
    store_reg_bytes(1, "x9", 8, mem->unit - 8);
}

// Shifts x0:x1 by a constant. extr takes 64 bits from across the two.
static void shl128(int n) {
  if (n >= 64) {
    println("  lsl x1, x0, %d", n - 64);
    println("  mov x0, 0");
  } else if (n > 0) {
    println("  extr x1, x1, x0, %d", 64 - n);
    println("  lsl x0, x0, %d", n);
  }
}

static void shr128(int n, bool is_signed) {
  char *shr = is_signed ? "asr" : "lsr";
  if (n >= 64) {
    println("  %s x0, x1, %d", shr, n - 64);
    if (is_signed)
      println("  asr x1, x1, 63");
    else
      println("  mov x1, 0");
  } else if (n > 0) {
    println("  extr x0, x1, x0, %d", n);
    println("  %s x1, x1, %d", shr, n);
  }
}

// Leaves a bit-field's value, from its unit, extended as its type says.
static void extract_bitfield(Member *mem) {
  bool is_signed = !mem->ty->is_unsigned && mem->ty->kind != TY_BOOL; // a _Bool is 0 or 1
  if (is_wide(mem)) {
    shl128(128 - mem->bit_width - mem->bit_offset);
    shr128(128 - mem->bit_width, is_signed);
    return;
  }
  println("  %s x0, x0, %d, %d", is_signed ? "sbfx" : "ubfx", mem->bit_offset,
          mem->bit_width);
}

// Stores the value in x0 (x0:x1 for an __int128) to bit-field `mem` at
// the address on top of the stack, and leaves the value the field now
// holds.
static void store_bitfield(Member *mem) {
  int w = mem->bit_width;
  bool is_signed = !mem->ty->is_unsigned && mem->ty->kind != TY_BOOL;
  pop("x9");

  if (is_wide(mem)) {
    // Cut to its width and moved to its place, in x2:x3
    println("  mov x12, x0");
    println("  mov x13, x1");
    shl128(128 - w);
    shr128(128 - w - mem->bit_offset, false);
    println("  mov x2, x0");
    println("  mov x3, x1");
    load_unit(mem, "x9", 0);
    unsigned __int128 mask = ~(unsigned __int128)0 >> (128 - w) << mem->bit_offset;
    load_imm("x10", (int64_t)~(uint64_t)mask);
    println("  and x0, x0, x10");
    load_imm("x10", (int64_t)~(uint64_t)(mask >> 64));
    println("  and x1, x1, x10");
    println("  orr x0, x0, x2");
    println("  orr x1, x1, x3");
    store_unit(mem);
    println("  mov x0, x12");
    println("  mov x1, x13");
    shl128(128 - w);
    shr128(128 - w, is_signed);
    return;
  }

  uint64_t bits = w == 64 ? -1 : (1UL << w) - 1;
  println("  mov x13, x0");
  load_imm("x10", bits);
  println("  and x11, x0, x10");
  println("  lsl x11, x11, %d", mem->bit_offset);
  load_unit(mem, "x9", 0);
  load_imm("x10", ~(bits << mem->bit_offset));
  println("  and x0, x0, x10");
  println("  orr x0, x0, x11");
  store_unit(mem);

  // The assignment's value: the new value cut to the field's width
  println("  %s x0, x13, 0, %d", is_signed ? "sbfx" : "ubfx", w);
}

//---------- Comparing with zero and casts -----------------------------------

// Calls one of gcc's runtime routines (see the top of this file).
static void call_rt(char *name) {
  println("  bl %s", name);
}

// Compares the value with 0: afterwards the flags say ne if it isn't. A
// NaN isn't zero: it compares unordered, which is ne too.
static void cmp_zero(Type *ty) {
  // A complex number, whose address is in x0: nonzero if either part is.
  // The address and the first part's result wait on the stack, since
  // comparing a long double calls a routine.
  if (is_complex(ty)) {
    Type *part = complex_part(ty);
    println("  str x0, [sp, -16]!");
    load_from(part, "x0", 0);
    cmp_zero(part);
    println("  cset w0, ne");
    println("  ldr x9, [sp]");
    println("  str x0, [sp]");
    load_from(part, "x9", part->size);
    cmp_zero(part);
    println("  cset w0, ne");
    println("  ldr x9, [sp], 16");
    println("  orr w0, w0, w9");
    println("  cmp w0, 0");
    return;
  }

  switch (ty->kind) {
  case TY_FLOAT:
    println("  fcmp s0, 0.0");
    return;
  case TY_DOUBLE:
    println("  fcmp d0, 0.0");
    return;
  case TY_LDOUBLE:
    println("  fmov d1, xzr"); // also zeroes v1's upper half
    call_rt("__netf2");
    println("  cmp w0, 0");
    return;
  case TY_INT128:
    println("  orr x9, x0, x1");
    println("  cmp x9, 0");
    return;
  }
  println("  cmp %s, 0", x0_or_w0(ty));
}

enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, F128 };

static int type_id(Type *ty) {
  switch (ty->kind) {
  case TY_FLOAT:
    return F32;
  case TY_DOUBLE:
    return F64;
  case TY_LDOUBLE:
    return F128;
  case TY_BOOL:
    return U8;
  }
  if (!is_integer(ty))
    return U64; // a pointer
  int i = ty->size == 1 ? I8 : ty->size == 2 ? I16 : ty->size == 4 ? I32 : I64;
  return ty->is_unsigned ? i + U8 : i;
}

// An integer in w0 (or x0) to integer type `to`
static void cast_int(int from, int to) {
  switch (to) {
  case I8: println("  sxtb w0, w0"); return;
  case U8: println("  uxtb w0, w0"); return;
  case I16: println("  sxth w0, w0"); return;
  case U16: println("  uxth w0, w0"); return;
  case I64:
  case U64:
    if (from <= I32)
      println("  sxtw x0, w0");
    else if (from < U64 && from != I64)
      println("  mov w0, w0");
    return;
  }
}

static void cast_int128(Type *from, Type *to);
static void cast_vector(Type *from, Type *to);

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  cset w0, ne");
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

  int t1 = type_id(from);
  int t2 = type_id(to);
  if (t1 == t2)
    return;

  // Integer to integer
  if (t1 <= U64 && t2 <= U64) {
    cast_int(t1, t2);
    return;
  }

  bool from_64 = t1 == I64 || t1 == U64;
  bool is_unsigned = t1 >= U8 && t1 <= U64;

  // Integer to floating point
  if (t1 <= U64) {
    if (t2 == F128) {
      call_rt(from_64 ? (is_unsigned ? "__floatunditf" : "__floatditf")
                      : (is_unsigned ? "__floatunsitf" : "__floatsitf"));
      return;
    }
    println("  %s %s, %s", is_unsigned ? "ucvtf" : "scvtf", t2 == F32 ? "s0" : "d0",
            from_64 ? "x0" : "w0");
    return;
  }

  // Floating point to integer: through an int for the narrower ones
  if (t2 <= U64) {
    if (t1 == F128) {
      call_rt(t2 == I64 ? "__fixtfdi" : t2 == U64 ? "__fixunstfdi" :
              t2 == U32 ? "__fixunstfsi" : "__fixtfsi");
    } else {
      char *src = t1 == F32 ? "s0" : "d0";
      if (t2 == I64 || t2 == U64)
        println("  %s x0, %s", t2 == I64 ? "fcvtzs" : "fcvtzu", src);
      else
        println("  %s w0, %s", t2 == U32 ? "fcvtzu" : "fcvtzs", src);
    }
    if (t2 == I8 || t2 == I16 || t2 == U8 || t2 == U16)
      cast_int(I32, t2);
    return;
  }

  // Floating point to floating point
  if (t1 == F128)
    call_rt(t2 == F32 ? "__trunctfsf2" : "__trunctfdf2");
  else if (t2 == F128)
    call_rt(t1 == F32 ? "__extendsftf2" : "__extenddftf2");
  else
    println("  fcvt %s, %s", t2 == F32 ? "s0" : "d0", t1 == F32 ? "s0" : "d0");
}

//---------- 128-bit integers ------------------------------------------------
//
// An __int128 value is in x0:x1, the high half in x1, where the AAPCS64
// returns one. Most operators are a few instructions on the halves;
// division and conversions to and from floating point call gcc's
// routines (__divti3 and so on).

static void cast_int128(Type *from, Type *to) {
  if (is_int128(from) && is_int128(to))
    return;

  if (is_int128(to)) {
    if (is_flonum(from)) {
      char *t = from->kind == TY_FLOAT ? "sf" : from->kind == TY_DOUBLE ? "df" : "tf";
      call_rt(format("__fix%s%sti", to->is_unsigned ? "uns" : "", t));
      return;
    }
    // An integer or a pointer: as 64 bits, extended
    cast(from, from->is_unsigned ? ty_ulong : ty_long);
    if (from->is_unsigned)
      println("  mov x1, 0");
    else
      println("  asr x1, x0, 63");
    return;
  }

  if (is_flonum(to)) {
    char *t = to->kind == TY_FLOAT ? "sf" : to->kind == TY_DOUBLE ? "df" : "tf";
    call_rt(format("__float%sti%s", from->is_unsigned ? "un" : "", t));
    return;
  }

  // To a narrower integer or a pointer: the low half, cut down as a long
  cast(from->is_unsigned ? ty_ulong : ty_long, to);
}

// Binary operator `node` on __int128 operands: the left in x0:x1, the
// right in x2:x3. A shift's count may be any integer; it goes in x2.
static void gen_int128_binary(Node *node) {
  bool u = node->lhs->ty->is_unsigned;

  if (node->kind == ND_SHL || node->kind == ND_SHR) {
    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("x2");
    // By n < 64, the bits that cross halves are the other half shifted
    // by 64 - n, done as 1 then 63 - n (~n, mod 64), which is right for
    // 0 too. By 64 or more, a 64-bit shift takes n mod 64, n - 64.
    if (node->kind == ND_SHL) {
      println("  lsl x1, x1, x2");
      println("  lsr x4, x0, 1");
      println("  mvn w5, w2");
      println("  lsr x4, x4, x5");
      println("  orr x1, x1, x4");
      println("  lsl x0, x0, x2");
      println("  tst x2, 64");
      println("  b.eq 1f");
      println("  mov x1, x0");
      println("  mov x0, 0");
    } else {
      println("  lsr x0, x0, x2");
      println("  lsl x4, x1, 1");
      println("  mvn w5, w2");
      println("  lsl x4, x4, x5");
      println("  orr x0, x0, x4");
      println("  %s x1, x1, x2", u ? "lsr" : "asr");
      println("  tst x2, 64");
      println("  b.eq 1f");
      println("  mov x0, x1");
      if (u)
        println("  mov x1, 0");
      else
        println("  asr x1, x1, 63");
    }
    println("1:");
    return;
  }

  gen_expr(node->rhs);
  push128();
  gen_expr(node->lhs);
  pop128(2, 3);

  switch (node->kind) {
  case ND_ADD:
    println("  adds x0, x0, x2");
    println("  adc x1, x1, x3");
    return;
  case ND_SUB:
    println("  subs x0, x0, x2");
    println("  sbc x1, x1, x3");
    return;
  case ND_MUL:
    // The low halves' full product, with the cross products added to
    // its high half
    println("  umulh x4, x0, x2");
    println("  madd x4, x0, x3, x4");
    println("  madd x1, x1, x2, x4");
    println("  mul x0, x0, x2");
    return;
  case ND_DIV:
    call_rt(u ? "__udivti3" : "__divti3");
    return;
  case ND_MOD:
    call_rt(u ? "__umodti3" : "__modti3");
    return;
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR: {
    char *op = node->kind == ND_BITAND ? "and" : node->kind == ND_BITOR ? "orr" : "eor";
    println("  %s x0, x0, x2", op);
    println("  %s x1, x1, x3", op);
    return;
  }
  case ND_EQ:
  case ND_NE:
    println("  eor x0, x0, x2");
    println("  eor x1, x1, x3");
    println("  orr x0, x0, x1");
    println("  cmp x0, 0");
    println("  cset w0, %s", node->kind == ND_EQ ? "eq" : "ne");
    return;
  case ND_LT:
    // The flags of left - right, in 128 bits
    println("  cmp x0, x2");
    println("  sbcs xzr, x1, x3");
    println("  cset w0, %s", u ? "lo" : "lt");
    return;
  case ND_LE:
    // right - left >= 0
    println("  cmp x2, x0");
    println("  sbcs xzr, x3, x1");
    println("  cset w0, %s", u ? "hs" : "ge");
    return;
  }
  error_tok(node->tok, "invalid expression");
}

//---------- Vectors ---------------------------------------------------------
//
// [GNU] A vector's value (of 4, 8 or 16 bytes) is in v0, its elements from
// the low end, as s0, d0 or q0, where the AAPCS64 passes one. Operators
// are done an element at a time (see vector_by_element()), which needs no
// NEON instructions but loads and stores.

// A cast to or from a vector keeps the bits (see check_vector_cast() in
// parser.c), but for a number to a larger vector, which the parser makes
// only of a number of the element type: every element is it, as in `v + 1`.
static void cast_vector(Type *from, Type *to) {
  if (is_vector(from) && is_vector(to))
    return;

  if (is_vector(to)) {
    if (from->size < to->size) {
      println("  sub sp, sp, 16");
      for (int i = 0; i < to->array_len; i++)
        store_to(from, "sp", i * from->size);
      println("  ldr %c0, [sp]", fp_letter(to->size));
      println("  add sp, sp, 16");
    } else if (is_int128(from)) {
      println("  fmov d0, x0");
      println("  mov v0.d[1], x1");
    } else {
      println(from->size == 8 ? "  fmov d0, x0" : "  fmov s0, w0");
    }
    return;
  }

  if (is_int128(to)) {
    println("  mov x0, v0.d[0]");
    println("  mov x1, v0.d[1]");
  } else {
    println(to->size == 8 ? "  fmov x0, d0" : "  fmov w0, s0");
  }
}

// The condition under which comparison `kind` is true, for integers of
// signedness `u`, or for floating point
static char *int_cond(NodeKind kind, bool u) {
  switch (kind) {
  case ND_EQ: return "eq";
  case ND_NE: return "ne";
  case ND_LT: return u ? "lo" : "lt";
  case ND_LE: return u ? "ls" : "le";
  }
  unreachable();
}

// After fcmp: < and <= are false for a NaN (unordered), != is true.
static char *fp_cond(NodeKind kind) {
  switch (kind) {
  case ND_EQ: return "eq";
  case ND_NE: return "ne";
  case ND_LT: return "mi";
  case ND_LE: return "ls";
  }
  unreachable();
}

// Operator `node` on vectors v0 and v1 (on v0 alone, for - and ~), an
// element at a time: both are put on the stack, each pair of elements is
// computed in x0 and x1 (as 64-bit values, extended from their type) or
// in s0 and s1 (d0 and d1), and the result goes in place of the left
// side's element. A comparison makes an element of all one bits if true.
static void vector_by_element(Node *node, Type *vec) {
  Type *elem = vec->elem;
  int sz = elem->size;
  bool u = elem->is_unsigned;
  bool unary = node->kind == ND_NEG || node->kind == ND_BITNOT;
  char v = fp_letter(vec->size);

  println("  sub sp, sp, 32");
  println("  str %c0, [sp]", v);
  if (!unary)
    println("  str %c1, [sp, 16]", v);

  for (int i = 0; i < vec->array_len; i++) {
    int off = i * sz;

    if (is_flonum(elem)) {
      char r = fp_letter(sz);
      println("  ldr %c0, [sp, %d]", r, off);
      if (!unary)
        println("  ldr %c1, [sp, %d]", r, off + 16);
      switch (node->kind) {
      case ND_ADD: println("  fadd %c0, %c0, %c1", r, r, r); break;
      case ND_SUB: println("  fsub %c0, %c0, %c1", r, r, r); break;
      case ND_MUL: println("  fmul %c0, %c0, %c1", r, r, r); break;
      case ND_DIV: println("  fdiv %c0, %c0, %c1", r, r, r); break;
      case ND_NEG: println("  fneg %c0, %c0", r, r); break;
      case ND_EQ:
      case ND_NE:
      case ND_LT:
      case ND_LE:
        println("  %s %c0, %c1", node->kind == ND_LT || node->kind == ND_LE ? "fcmpe" : "fcmp",
                r, r);
        println("  csetm %s, %s", reg(0, sz), fp_cond(node->kind));
        println("  str %s, [sp, %d]", reg(0, sz), off);
        continue;
      case ND_BITNOT: // the bits of a float, as cgen.c's ~ on an XMM register
        println("  fmov %s, %c0", reg(0, sz), r);
        println("  mvn %s, %s", reg(0, sz), reg(0, sz));
        println("  str %s, [sp, %d]", reg(0, sz), off);
        continue;
      default:
        error_tok(node->tok, "invalid expression");
      }
      println("  str %c0, [sp, %d]", r, off);
      continue;
    }

    // (A load into a w register zero-extends it to 64 bits.)
    char *ld = sz == 1 ? (u ? "ldrb" : "ldrsb") : sz == 2 ? (u ? "ldrh" : "ldrsh") :
               sz == 4 ? (u ? "ldr" : "ldrsw") : "ldr";
    char r = u && sz <= 4 ? 'w' : 'x';
    println("  %s %c0, [sp, %d]", ld, r, off);
    if (!unary)
      println("  %s %c1, [sp, %d]", ld, r, off + 16);

    switch (node->kind) {
    case ND_ADD: println("  add x0, x0, x1"); break;
    case ND_SUB: println("  sub x0, x0, x1"); break;
    case ND_MUL: println("  mul x0, x0, x1"); break;
    case ND_BITAND: println("  and x0, x0, x1"); break;
    case ND_BITOR: println("  orr x0, x0, x1"); break;
    case ND_BITXOR: println("  eor x0, x0, x1"); break;
    case ND_NEG: println("  neg x0, x0"); break;
    case ND_BITNOT: println("  mvn x0, x0"); break;
    case ND_SHL: println("  lsl x0, x0, x1"); break;
    case ND_SHR: println("  %s x0, x0, x1", u ? "lsr" : "asr"); break;
    case ND_DIV:
    case ND_MOD:
      println("  %s x2, x0, x1", u ? "udiv" : "sdiv");
      if (node->kind == ND_DIV)
        println("  mov x0, x2");
      else
        println("  msub x0, x2, x1, x0");
      break;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      println("  cmp x0, x1");
      println("  csetm x0, %s", int_cond(node->kind, u));
      break;
    default:
      error_tok(node->tok, "invalid expression");
    }
    store_reg_bytes(0, "sp", off, sz);
  }

  println("  ldr %c0, [sp]", v);
  println("  add sp, sp, 32");
}

static void gen_vector_binary(Node *node) {
  gen_expr(node->rhs);
  push_fp();
  gen_expr(node->lhs);
  pop_fp(1);
  vector_by_element(node, node->lhs->ty);
}

//---------- Simple operands -------------------------------------------------

// A cast that leaves the register as it is: between integers or pointers
// of 64 bits, or to 32 bits from as many or more (a 32-bit instruction
// reads only w0).
static bool is_noop_cast(Type *from, Type *to) {
  return is_int_or_ptr(from) && is_int_or_ptr(to) &&
         ((to->size == 4 && from->size >= 4) || (to->size == 8 && from->size == 8));
}

// Some operands can be loaded into x1 with a single instruction, last: an
// integer constant, or an integer or pointer local variable. For `a + b`
// or `a < n`, mucc then computes `a` and loads the right side straight
// into x1, instead of computing the right side, pushing it, computing
// the left side and popping it back. Neither kind of operand has side
// effects, so evaluating it last is safe. Returns the operand (past casts
// that change nothing), or NULL.
static Node *simple_operand(Node *node) {
  if (node->kind == ND_CAST && is_noop_cast(node->lhs->ty, node->ty))
    node = node->lhs;
  if (is_foldable(node))
    return node;
  Obj *var = local_scalar(node);
  if (var && is_int_or_ptr(var->ty))
    return node;
  return NULL;
}

// Loads simple operand `node` into x`r`, or w`r` if `ty` is 32 bits or
// less.
static void load_simple_to(Node *node, Type *ty, int r) {
  int size = is_64bit(ty) ? 8 : 4;
  if (is_foldable(node)) {
    load_imm(reg(r, size), fold(node));
    return;
  }
  Obj *var = node->var;
  if (var->reg) {
    println("  mov %s, %s", reg(r, size), reg(18 + var->reg, size));
    return;
  }
  char *insn = var->ty->size == 1 ? (var->ty->is_unsigned ? "ldrb" : "ldrsb") :
               var->ty->size == 2 ? (var->ty->is_unsigned ? "ldrh" : "ldrsh") : "ldr";
  int n = MIN(var->ty->size, size);
  println("  %s %s, %s", insn, reg(r, n), mem("x29", var->offset, n));
}

static void load_simple(Node *node, Type *ty) {
  load_simple_to(node, ty, 1);
}

//---------- Memory operands -------------------------------------------------
//
// An object in memory is loaded or stored from a base register and an
// offset, as load_from() and store_to() take them: p->x, with p in x19,
// is `ldr x0, [x19, 8]`, and s.a.b, with s on the stack, is
// `ldr x0, [x29, 40]`, instead of the address computed into x0 first.

// `p` without casts that emit no code: from one pointer type to another,
// or from an array to a pointer to its first element (in `a + 1`)
static Node *skip_noop_casts(Node *p) {
  while (p->kind == ND_CAST &&
         ((is_int_or_ptr(p->ty) && is_int_or_ptr(p->lhs->ty) &&
           is_noop_cast(p->lhs->ty, p->ty)) ||
          (p->ty->kind == TY_PTR && p->lhs->ty->kind == TY_ARRAY)))
    p = p->lhs;
  return p;
}

// For *(p + n) with a constant n, which p[n] is too: p, and n (scaled to
// bytes) in *off. Else `p` itself, with *off 0.
static Node *split_offset(Node *p, int64_t *off) {
  *off = 0;
  p = skip_noop_casts(p);
  if ((p->kind == ND_ADD || p->kind == ND_SUB) && p->lhs->ty->base &&
      p->lhs->ty->base->kind != TY_VLA && is_foldable(p->rhs)) {
    *off = p->kind == ND_SUB ? -fold(p->rhs) : fold(p->rhs);
    return skip_noop_casts(p->lhs);
  }
  return p;
}

// A local variable addressed from x29 (see gen_addr)
static bool is_frame_var(Node *node) {
  return node->kind == ND_VAR && node->var->is_local && !node->var->reg &&
         node->var->ty->kind != TY_VLA && !node->var->is_overaligned;
}

// The operand of the object `node` if it needs no code at all: a local
// variable, a member of one, or what a register variable points to (at
// a constant offset).
static bool free_memop(Node *node, MemOp *m) {
  switch (node->kind) {
  case ND_VAR:
    if (!is_frame_var(node))
      return false;
    *m = (MemOp){"x29", node->var->offset};
    return true;
  case ND_MEMBER:
    if (!free_memop(node->lhs, m))
      return false;
    m->off += node->member->offset;
    return true;
  case ND_DEREF: {
    int64_t off;
    Node *p = split_offset(node->lhs, &off);
    // An element of an array that is itself such an operand, as a[2] or
    // p->a[2]
    if (p->ty->kind == TY_ARRAY) {
      if (!free_memop(p, m))
        return false;
      m->off += off;
      return true;
    }
    Obj *var = local_scalar(p);
    if (!var || !var->reg || var->ty->kind != TY_PTR)
      return false;
    *m = (MemOp){xreg[18 + var->reg], off};
    return true;
  }
  }
  return false;
}

// The operand of the object `node`, computing into x0 what it needs.
static MemOp gen_memop(Node *node) {
  MemOp m;
  if (free_memop(node, &m))
    return m;

  switch (node->kind) {
  case ND_MEMBER:
    m = gen_memop(node->lhs);
    m.off += node->member->offset;
    return m;
  case ND_DEREF: {
    int64_t off;
    Node *p = split_offset(node->lhs, &off);
    if (p->ty->kind == TY_ARRAY) {
      m = gen_memop(p);
      m.off += off;
      return m;
    }
    gen_expr(p);
    return (MemOp){"x0", off};
  }
  }

  gen_addr(node);
  return (MemOp){"x0", 0};
}

// The types loaded and stored as they are in one instruction (with
// load_from() and store_to())
static bool is_simple_load(Type *ty) {
  return (is_integer(ty) && !is_int128(ty)) || ty->kind == TY_PTR ||
         ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

// The value of `node` if it's a constant that add, sub and cmp take as
// they are, from 0 to 4095; else -1.
static int64_t imm12(Node *node) {
  if (node->kind == ND_CAST && is_noop_cast(node->lhs->ty, node->ty))
    node = node->lhs;
  if (!is_foldable(node))
    return -1;
  int64_t val = fold(node);
  return 0 <= val && val < 4096 ? val : -1;
}

static bool is_commutative(Node *node) {
  switch (node->kind) {
  case ND_ADD: case ND_MUL: case ND_BITAND: case ND_BITOR: case ND_BITXOR:
  case ND_EQ: case ND_NE:
    return true;
  }
  return false;
}

// Computes a binary operator's lhs into x0 and rhs into x1 (or, for
// `a + b` where only a is simple, b into x0 and a into x1).
static void gen_operands(Node *node) {
  Node *l = simple_operand(node->lhs);
  Node *r = simple_operand(node->rhs);
  if (!r && l && is_commutative(node)) {
    gen_expr(node->rhs);
    load_simple(l, node->lhs->ty);
    return;
  }

  if (r) {
    gen_expr(node->lhs);
    load_simple(r, node->lhs->ty);
  } else {
    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("x1");
  }
}

//---------- Function calls (AAPCS64) ----------------------------------------
//
// As the AAPCS64 says (with Linux's rules for variadic functions, which
// pass their arguments as any other):
//
// - Integers and pointers go in x0-x7, an __int128 in an even and odd
//   pair of them; floats, doubles, long doubles and vectors in v0-v7.
// - A struct or union of 1 to 4 members of one floating-point (or
//   vector) type, an HFA, goes in as many v registers, one member each.
// - Any other of up to 16 bytes goes in one or two x registers (an even
//   pair if it's aligned to 16); one larger is copied by the caller, and
//   a pointer to the copy is passed instead.
// - Once one kind of register runs out, those arguments go on the stack,
//   each in 8 bytes or a multiple (16-byte aligned, if its type is).
// - A struct or union returned in memory is written where x8 points.

typedef enum { ARG_NONE, ARG_GP, ARG_FP, ARG_STACK } ArgKind;

typedef struct {
  ArgKind kind;    // ARG_NONE: an empty struct (GNU), which takes nothing
  int reg;         // ARG_GP and ARG_FP: the first register
  int nregs;       // and how many
  int64_t offset;  // ARG_STACK: its place in the arguments on the stack
  bool by_ref;     // a pointer to a copy is passed instead
  Type *part;      // ARG_FP: the type of each register's part
} ArgLoc;

// The registers and stack the arguments before took
typedef struct {
  int gp, fp;
  int64_t stack;
} ArgState;

// Where the next argument, of type `ty`, goes. The caller, the callee and
// va_arg must all agree, so all use this.
static ArgLoc classify_arg(ArgState *s, Type *ty) {
  ArgLoc loc = {ARG_NONE};
  bool is_struct = ty->kind == TY_STRUCT || ty->kind == TY_UNION;
  if (is_struct && ty->size == 0)
    return loc;

  int n = 0;
  Type *part = ty;
  if (is_flonum(ty) || is_vector(ty))
    n = 1;
  else if (is_struct)
    n = hfa_count(ty, &part);

  int64_t size = ty->size;
  int align = ty->align;
  if (n) {
    if (s->fp + n <= 8) {
      loc = (ArgLoc){ARG_FP, s->fp, n, .part = part};
      s->fp += n;
      return loc;
    }
    s->fp = 8;
  } else {
    // (An array or a function, as an argument to a function without a
    // prototype, is its address.)
    loc.by_ref = is_struct && ty->size > 16;
    if (loc.by_ref || (is_aggregate(ty) && !is_struct))
      size = align = 8;
    n = (size + 7) / 8;
    if (align >= 16)
      s->gp = align_to(s->gp, 2);
    if (s->gp + n <= 8) {
      loc.kind = ARG_GP;
      loc.reg = s->gp;
      loc.nregs = n;
      s->gp += n;
      return loc;
    }
    s->gp = 8;
  }

  loc.kind = ARG_STACK;
  s->stack = align_to(s->stack, align >= 16 ? 16 : 8);
  loc.offset = s->stack;
  s->stack += align_to(size, 8);
  return loc;
}

// While a call's arguments are computed, each waits on the stack: its
// value in a 16-byte slot, or a struct's bytes, rounded up to 16.
static int64_t arg_slot(Type *ty) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    return align_to(ty->size, 16);
  return 16;
}

static void push_arg(Node *arg) {
  gen_expr(arg);
  switch (arg->ty->kind) {
  case TY_STRUCT:
  case TY_UNION: {
    int64_t sz = arg_slot(arg->ty);
    if (!sz)
      return;
    add_imm("sp", "sp", -sz);
    println("  mov x9, sp");
    copy_bytes("x9", "x0", arg->ty->size);
    depth += sz / 16;
    return;
  }
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
  case TY_VECTOR:
    push_fp();
    return;
  case TY_INT128:
    push128();
    return;
  }
  push();
}

// Copies a returned struct or union, which came back in registers, to
// its buffer `var`.
static void copy_ret_buffer(Obj *var) {
  Type *ty = var->ty;
  if (!ty->size) // an empty struct (GNU) comes back in no register
    return;
  addr_of_local(var, "x9");
  Type *part;
  int n = hfa_count(ty, &part);
  for (int i = 0; i < n; i++)
    println("  str %c%d, %s", fp_letter(part->size), i,
            mem("x9", i * part->size, part->size));
  if (n)
    return;
  store_reg_bytes(0, "x9", 0, MIN(8, ty->size));
  if (ty->size > 8)
    store_reg_bytes(1, "x9", 8, ty->size - 8);
}

// alloca(): the size in w0 (alloca takes an int), rounded up to 16, is
// taken from the stack below the area alloca_bottom says, by moving the
// temporaries below it (from sp up) down that far. Returns the new
// space's address.
static void builtin_alloca(void) {
  int64_t bottom = current_fn->alloca_bottom->offset;
  println("  add w0, w0, 15");
  println("  and w0, w0, -16");
  println("  ldr x9, %s", mem("x29", bottom, 8));
  println("  mov x11, sp");
  println("  sub x10, x9, x11");
  println("  sub sp, sp, x0");
  println("  mov x12, sp");
  println("1:");
  println("  cbz x10, 2f");
  println("  ldr x13, [x11], 8");
  println("  str x13, [x12], 8");
  println("  sub x10, x10, 8");
  println("  b 1b");
  println("2:");
  println("  sub x0, x9, x0");
  println("  str x0, %s", mem("x29", bottom, 8));
}

static void gen_funcall(Node *node) {
  if (node->lhs->kind == ND_VAR && !strcmp(node->lhs->var->name, "alloca")) {
    gen_expr(node->args);
    builtin_alloca();
    return;
  }

  int nargs = 0;
  for (Node *arg = node->args; arg; arg = arg->next)
    nargs++;
  Node **args = calloc(nargs + 1, sizeof(Node *));
  ArgLoc *locs = calloc(nargs + 1, sizeof(ArgLoc));
  int64_t *pos = calloc(nargs + 1, sizeof(int64_t));
  bool *late = calloc(nargs + 1, sizeof(bool));

  ArgState s = {};
  int i = 0;
  for (Node *arg = node->args; arg; arg = arg->next, i++) {
    args[i] = arg;
    locs[i] = classify_arg(&s, arg->ty);

    // An integer or pointer argument that goes in a register, and is a
    // constant or a local variable, isn't pushed: it's loaded into its
    // register last, just before the call (see simple_operand()).
    late[i] = locs[i].kind == ARG_GP && !locs[i].by_ref && locs[i].nregs == 1 &&
              is_int_or_ptr(arg->ty) && simple_operand(arg);
  }

  // Computed right to left, as on x86-64, the first ends at sp. pos[i] is
  // where argument i is, from sp.
  for (i = nargs - 1; i >= 0; i--)
    if (!late[i])
      push_arg(args[i]);
  for (i = 1; i <= nargs; i++)
    pos[i] = pos[i - 1] + (late[i - 1] ? 0 : arg_slot(args[i - 1]->ty));
  int64_t temps = pos[nargs];

  // A function called through a pointer: its address goes in x15.
  bool direct = node->lhs->kind == ND_VAR && node->lhs->ty->kind == TY_FUNC;
  if (!direct) {
    gen_expr(node->lhs);
    println("  mov x15, x0");
  }

  // The arguments that go on the stack, below the ones computed
  int64_t out = align_to(s.stack, 16);
  add_imm("sp", "sp", -out);
  depth += out / 16;
  for (i = 0; i < nargs; i++) {
    ArgLoc *loc = &locs[i];
    Type *ty = args[i]->ty;
    int64_t src = out + pos[i];
    if (loc->kind != ARG_STACK)
      continue;
    if (loc->by_ref) {
      add_imm("x9", "sp", src);
      println("  str x9, %s", mem("sp", loc->offset, 8));
      continue;
    }
    int64_t n = ty->kind == TY_STRUCT || ty->kind == TY_UNION ? align_to(ty->size, 8) :
                ty->size > 8 ? 16 : 8;
    for (int64_t j = 0; j < n; j += 8) {
      println("  ldr x9, %s", mem("sp", src + j, 8));
      println("  str x9, %s", mem("sp", loc->offset + j, 8));
    }
  }

  // And those that go in registers
  for (i = 0; i < nargs; i++) {
    ArgLoc *loc = &locs[i];
    int64_t src = out + pos[i];
    if (loc->kind == ARG_GP) {
      if (late[i]) {
        load_simple_to(simple_operand(args[i]), args[i]->ty, loc->reg);
        continue;
      }
      if (loc->by_ref) {
        add_imm(xreg[loc->reg], "sp", src);
        continue;
      }
      for (int j = 0; j < loc->nregs; j++)
        println("  ldr %s, %s", xreg[loc->reg + j], mem("sp", src + j * 8, 8));
    } else if (loc->kind == ARG_FP) {
      int sz = loc->part->size;
      for (int j = 0; j < loc->nregs; j++)
        println("  ldr %c%d, %s", fp_letter(sz), loc->reg + j, mem("sp", src + j * sz, sz));
    }
  }

  if (node->ret_buffer && is_ret_in_memory(node->ty))
    addr_of_local(node->ret_buffer, "x8");

  if (direct)
    println("  bl %s", node->lhs->var->name);
  else
    println("  blr x15");
  add_imm("sp", "sp", out + temps);
  depth -= (out + temps) / 16;
  free(args);
  free(locs);
  free(pos);
  free(late);

  // The bits of x0 above a char, short or _Bool may be anything.
  switch (node->ty->kind) {
  case TY_BOOL:
    println("  uxtb w0, w0");
    return;
  case TY_CHAR:
    println("  %s w0, w0", node->ty->is_unsigned ? "uxtb" : "sxtb");
    return;
  case TY_SHORT:
    println("  %s w0, w0", node->ty->is_unsigned ? "uxth" : "sxth");
    return;
  }

  // A struct or union comes back in its buffer, or in registers, which
  // are copied to its buffer. Its value is the buffer's address.
  if (node->ret_buffer) {
    if (!is_ret_in_memory(node->ty))
      copy_ret_buffer(node->ret_buffer);
    addr_of_local(node->ret_buffer, "x0");
  }
}

// Loads the struct or union to return, whose address is in x0, into the
// registers it goes back in.
static void copy_struct_reg(void) {
  Type *ty = current_fn->ty->return_ty;
  if (!ty->size)
    return;
  Type *part;
  int n = hfa_count(ty, &part);
  for (int i = 0; i < n; i++)
    println("  ldr %c%d, %s", fp_letter(part->size), i,
            mem("x0", i * part->size, part->size));
  if (n)
    return;
  if (ty->size > 8)
    load_reg_bytes(1, "x0", 8, ty->size - 8);
  load_reg_bytes(0, "x0", 0, MIN(8, ty->size));
}

// Copies the struct or union to return, whose address is in x0, to where
// the caller's x8 pointed (the hidden first parameter has it).
static void copy_struct_mem(void) {
  Obj *var = current_fn->params;
  if (var->reg)
    println("  mov x9, %s", var_reg(var));
  else
    println("  ldr x9, %s", mem("x29", var->offset, 8));
  copy_bytes("x9", "x0", current_fn->ty->return_ty->size);
  println("  mov x0, x9");
}

// va_arg(ap, ty): the address of the next variadic argument, found the
// way the caller passed it (see classify_arg()). The prologue saved
// x0-x7 and q0-q7 in the save areas the va_list points into: one taken
// from those is at gr_top + gr_offs, or vr_top + vr_offs, and the offset
// goes up as each is taken, from minus the area's size to 0. One on the
// stack is at __stack. An HFA's members, in v registers, were saved 16
// bytes apart, and are copied back together into node->var.
static void gen_va_arg(Node *node) {
  Type *ty = node->ty->base;
  int c = count();
  gen_expr(node->lhs);
  println("  mov x9, x0"); // the va_list

  ArgState s = {};
  ArgLoc loc = classify_arg(&s, ty);
  if (loc.kind == ARG_FP || loc.kind == ARG_GP) {
    bool fp = loc.kind == ARG_FP;
    int offs = fp ? 28 : 24; // __vr_offs or __gr_offs
    println("  ldrsw x10, [x9, %d]", offs);
    println("  cmp x10, 0");
    println("  b.ge .L.va_stack.%d", c);
    if (!fp && !loc.by_ref && ty->align >= 16) {
      println("  add x10, x10, 15");
      println("  and x10, x10, -16");
    }
    println("  add x11, x10, %d", loc.nregs * (fp ? 16 : 8));
    println("  str w11, [x9, %d]", offs);
    println("  cmp x11, 0");
    println("  b.gt .L.va_stack.%d", c);
    println("  ldr x12, [x9, %d]", fp ? 16 : 8); // __vr_top or __gr_top
    println("  add x0, x12, x10");
    if (loc.by_ref)
      println("  ldr x0, [x0]");
    if (fp && loc.nregs > 1) {
      int sz = loc.part->size;
      addr_of_local(node->var, "x13");
      for (int i = 0; i < loc.nregs; i++)
        for (int j = 0; j < sz; j += 4) {
          println("  ldr w14, [x0, %d]", i * 16 + j);
          println("  str w14, [x13, %d]", i * sz + j);
        }
      println("  mov x0, x13");
    }
    println("  b .L.va_end.%d", c);
  }

  // On the stack: aligned to 16 if its type is, taking a multiple of 8
  // bytes
  println(".L.va_stack.%d:", c);
  println("  ldr x0, [x9]");
  if (!loc.by_ref && ty->align > 8) {
    println("  add x0, x0, 15");
    println("  and x0, x0, -16");
  }
  println("  add x11, x0, %ld", align_to(loc.by_ref ? 8 : ty->size, 8));
  println("  str x11, [x9]");
  if (loc.by_ref)
    println("  ldr x0, [x0]");
  println(".L.va_end.%d:", c);
}

//---------- Branches and unused values --------------------------------------

// The condition (as in b.cond) under which comparison `node` is true, or
// false if `when` is false.
static char *condition(Node *node, bool when) {
  bool u = node->lhs->ty->is_unsigned;
  switch (node->kind) {
  case ND_EQ: return when ? "eq" : "ne";
  case ND_NE: return when ? "ne" : "eq";
  case ND_LT: return when ? (u ? "lo" : "lt") : (u ? "hs" : "ge");
  case ND_LE: return when ? (u ? "ls" : "le") : (u ? "hi" : "gt");
  }
  unreachable();
}

// Compares integer or pointer operands `node->lhs` and `node->rhs`,
// setting the flags. A right side that is a constant from 0 to 4095 is
// cmp's immediate.
static void gen_compare(Node *node) {
  char *r0 = x0_or_w0(node->lhs->ty);
  int64_t imm = imm12(node->rhs);
  if (imm >= 0) {
    gen_expr(node->lhs);
    println("  cmp %s, %ld", r0, imm);
    return;
  }
  gen_operands(node);
  println("  cmp %s, %s", r0, is_64bit(node->lhs->ty) ? "x1" : "w1");
}

// Jumps to `label` if `cond` is true (or, if `when` is false, if it's
// false). Comparisons, !, && and || jump on the flags directly, instead
// of making a 0 or 1 in x0 and testing that.
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
    gen_compare(cond);
    println("  b.%s %s", condition(cond, when), label);
    return;
  }

  gen_expr(cond);
  cmp_zero(cond->ty);
  println("  b.%s %s", when ? "ne" : "eq", label);
}

// Computes `node` only for its side effects, as in an expression
// statement. Casts, and adding a constant, change nothing else, so they
// are skipped: `x++;` becomes just `x += 1`, without computing x's old
// value.
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

// ARM64 orders memory accesses less than x86-64 does. A load of an
// _Atomic object is followed by a full barrier, which makes it an
// acquire; a store has one before and after it, as gcc's
// sequentially consistent ones are ordered.
static void atomic_fence(Type *ty) {
  if (ty->is_atomic)
    println("  dmb ish");
}

// Atomic compare-and-swap: if *addr is *old, it becomes new, and the
// result is 1; else *old becomes *addr, and the result is 0. The values
// are compared as bits: a float or a double goes through w0 or x0, and a
// long double through x0:x1, compared and stored as a pair.
static void gen_cas(Node *node) {
  Type *ty = node->cas_addr->ty->base;
  int sz = ty->size;
  gen_expr(node->cas_addr);
  push();
  gen_expr(node->cas_new);

  if (sz == 16) {
    println("  mov x0, v0.d[0]");
    println("  mov x1, v0.d[1]");
    push128();
    gen_expr(node->cas_old);
    println("  mov x9, x0");
    pop128(10, 11); // new
    pop("x12");     // addr
    println("  dmb ish");
    println("  ldp x13, x14, [x9]");
    println("1:");
    println("  ldaxp x15, x17, [x12]");
    println("  cmp x15, x13");
    println("  b.ne 2f");
    println("  cmp x17, x14");
    println("  b.ne 2f");
    println("  stlxp w8, x10, x11, [x12]");
    println("  cbnz w8, 1b");
    println("2:");
    println("  cset w0, eq");
    println("  b.eq 3f");
    println("  stp x15, x17, [x9]");
    println("3:");
    println("  dmb ish");
    return;
  }

  if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE)
    println("  fmov %s, %c0", reg(0, sz), fp_letter(sz));
  push();
  gen_expr(node->cas_old);
  println("  mov x9, x0");
  pop("x10"); // new
  pop("x12"); // addr

  char *sfx = sz == 1 ? "b" : sz == 2 ? "h" : "";
  char *r = sz == 8 ? "x" : "w";
  println("  dmb ish");
  load_reg_bytes(13, "x9", 0, sz);
  println("1:");
  println("  ldaxr%s %s15, [x12]", sfx, r);
  println("  cmp %s15, %s13", r, r);
  println("  b.ne 2f");
  println("  stlxr%s w8, %s10, [x12]", sfx, r);
  println("  cbnz w8, 1b");
  println("2:");
  println("  cset w0, eq");
  println("  b.eq 3f");
  store_reg_bytes(15, "x9", 0, sz);
  println("3:");
  println("  dmb ish");
}

// Atomic exchange: *lhs becomes rhs, and the result is its old value.
static void gen_exch(Node *node) {
  Type *ty = node->lhs->ty->base;
  int sz = ty->size;
  gen_expr(node->lhs);
  push();
  gen_expr(node->rhs);
  if (is_flonum(ty))
    println("  fmov %s, %c0", reg(0, sz), fp_letter(sz));
  pop("x9");

  char *sfx = sz == 1 ? "b" : sz == 2 ? "h" : "";
  char *r = sz == 8 ? "x" : "w";
  println("  dmb ish");
  println("1:");
  println("  ldaxr%s %s10, [x9]", sfx, r);
  println("  stlxr%s w8, %s0, [x9]", sfx, r);
  println("  cbnz w8, 1b");
  println("  dmb ish");
  if (is_flonum(ty))
    println("  fmov %c0, %s10", fp_letter(sz), r);
  else
    println("  mov %s0, %s10", r, r);
}

// __builtin_add_overflow and the like: a and b are 64-bit (see
// gnu_builtin()), so a op b is computed exactly in 128 bits, in x0:x1. It
// fits *res's type if that type's value of its low bits, extended back to
// 128 bits, is the same. (Every exact result is less than 2^128 and at
// least -2^127, so equal bits mean equal values.)
static void gen_overflow(Node *node) {
  bool signed_a = !node->lhs->ty->is_unsigned;
  bool signed_b = !node->rhs->ty->is_unsigned;
  gen_expr(node->cas_addr);
  push();
  gen_expr(node->lhs);
  push();
  gen_expr(node->rhs);
  println("  mov x11, x0"); // b
  pop("x10");               // a

  if (node->val == ND_MUL) {
    // The unsigned product, less 2^64 * b for a negative a and 2^64 * a
    // for a negative b
    println("  mul x0, x10, x11");
    println("  umulh x1, x10, x11");
    if (signed_a) {
      println("  cmp x10, 0");
      println("  csel x12, x11, xzr, lt");
      println("  sub x1, x1, x12");
    }
    if (signed_b) {
      println("  cmp x11, 0");
      println("  csel x12, x10, xzr, lt");
      println("  sub x1, x1, x12");
    }
  } else {
    // Each operand's high 64 bits: copies of its sign, or 0
    println(signed_a ? "  asr x12, x10, 63" : "  mov x12, 0");
    println(signed_b ? "  asr x13, x11, 63" : "  mov x13, 0");
    if (node->val == ND_ADD) {
      println("  adds x0, x10, x11");
      println("  adc x1, x12, x13");
    } else {
      println("  subs x0, x10, x11");
      println("  sbc x1, x12, x13");
    }
  }

  Type *ty = node->cas_addr->ty->base;
  pop("x9");
  store_reg_bytes(0, "x9", 0, ty->size);

  // *res's value, extended to 128 bits in x13:x12
  bool u = ty->is_unsigned;
  switch (ty->size) {
  case 1: println(u ? "  uxtb w12, w0" : "  sxtb x12, w0"); break;
  case 2: println(u ? "  uxth w12, w0" : "  sxth x12, w0"); break;
  case 4: println(u ? "  mov w12, w0" : "  sxtw x12, w0"); break;
  default: println("  mov x12, x0");
  }
  println(u ? "  mov x13, 0" : "  asr x13, x12, 63");
  println("  eor x12, x12, x0");
  println("  eor x13, x13, x1");
  println("  orr x12, x12, x13");
  println("  cmp x12, 0");
  println("  cset w0, ne");
}

// Generates code for expression `node`, which leaves its value in x0 (an
// integer, a pointer, or a struct's address), v0 (a float, double, long
// double or vector) or x0:x1 (an __int128).
static void gen_expr(Node *node) {
  emit_loc(node->tok);

  if (node->kind != ND_NUM && is_foldable(node)) {
    load_imm(x0_or_w0(node->ty), fold(node));
    return;
  }

  // gen_expr() has three parts, in this order: the node kinds that aren't
  // binary operators (this switch: values, memory, ?:, && and ||, calls,
  // builtins and atomics), then binary operators on floating-point
  // operands, then binary operators on integers and pointers.
  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
    switch (node->ty->kind) {
    case TY_FLOAT: {
      union { float f32; uint32_t u32; } u = { *node->fval };
      load_imm("w0", u.u32);
      println("  fmov s0, w0");
      return;
    }
    case TY_DOUBLE: {
      union { double f64; uint64_t u64; } u = { *node->fval };
      load_imm("x0", u.u64);
      println("  fmov d0, x0");
      return;
    }
    case TY_LDOUBLE: {
      uint64_t u64[2];
      write_ldouble((char *)u64, *node->fval);
      load_imm("x0", u64[0]);
      load_imm("x1", u64[1]);
      println("  fmov d0, x0");
      println("  mov v0.d[1], x1");
      return;
    }
    case TY_INT128:
      load_imm("x0", node->val);
      println("  asr x1, x0, 63");
      return;
    }
    load_imm(x0_or_w0(node->ty), node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    switch (node->ty->kind) {
    case TY_VECTOR:
      vector_by_element(node, node->ty);
      return;
    case TY_FLOAT:
    case TY_DOUBLE:
      println("  fneg %c0, %c0", fp_letter(node->ty->size), fp_letter(node->ty->size));
      return;
    case TY_LDOUBLE: // the sign is the top bit of the high half
      println("  mov x9, v0.d[1]");
      println("  eor x9, x9, 0x8000000000000000");
      println("  mov v0.d[1], x9");
      return;
    case TY_INT128:
      println("  negs x0, x0");
      println("  ngc x1, x1");
      return;
    }
    println("  neg %s, %s", x0_or_w0(node->ty), x0_or_w0(node->ty));
    return;
  case ND_VAR:
    if (local_scalar(node)) {
      load_var(node->var);
      return;
    }
    gen_addr(node);
    load(node->ty);
    atomic_fence(node->ty);
    return;
  case ND_MEMBER: {
    Member *mem = node->member;

    // A scalar member is loaded from base + offset in one instruction:
    // `ldr x0, [x0, 8]` for p->x, or `ldr x0, [x29, 24]` for s.x when s is
    // a local struct.
    if (is_aggregate(node->ty)) {
      gen_addr(node);
      return;
    }
    MemOp m = gen_memop(node);
    // A bit-field's whole unit, not its promoted type (see add_type())
    if (mem->is_bitfield) {
      load_unit(mem, m.base, m.off);
      extract_bitfield(mem);
    } else {
      load_from(node->ty, m.base, m.off);
    }
    atomic_fence(node->ty);
    return;
  }
  case ND_DEREF:
    if (is_simple_load(node->ty)) {
      MemOp m = gen_memop(node);
      load_from(node->ty, m.base, m.off);
    } else {
      gen_expr(node->lhs);
      load(node->ty);
    }
    atomic_fence(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    // Storing to a local scalar needs no address computation.
    if (local_scalar(node->lhs)) {
      gen_expr(node->rhs);
      atomic_fence(node->lhs->ty);
      store_var(node->lhs->var);
      atomic_fence(node->lhs->ty);
      return;
    }

    // Nor does a store to an operand that needs no code, as p->x with p
    // in a register.
    MemOp m;
    if (is_simple_load(node->ty) &&
        !(node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) &&
        free_memop(node->lhs, &m)) {
      gen_expr(node->rhs);
      atomic_fence(node->lhs->ty);
      store_to(node->ty, m.base, m.off);
      atomic_fence(node->lhs->ty);
      return;
    }

    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
      store_bitfield(node->lhs->member);
      return;
    }
    atomic_fence(node->lhs->ty);
    store(node->ty);
    atomic_fence(node->lhs->ty);
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
    cast(node->lhs->ty, node->ty);
    return;
  case ND_MEMZERO: {
    Obj *var = node->var;
    if (var->reg) {
      println("  mov %s, 0", var_reg(var));
    } else if (var->is_overaligned) {
      addr_of_local(var, "x9");
      zero_bytes("x9", 0, var->ty->size);
    } else {
      zero_bytes("x29", var->offset, var->ty->size);
    }
    return;
  }
  case ND_COND: {
    int c = count();
    gen_branch(node->cond, false, format(".L.else.%d", c));
    gen_expr(node->then);
    println("  b .L.end.%d", c);
    println(".L.else.%d:", c);
    gen_expr(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  cset w0, eq");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    if (is_vector(node->ty)) {
      vector_by_element(node, node->ty);
      return;
    }
    if (is_int128(node->ty)) {
      println("  mvn x0, x0");
      println("  mvn x1, x1");
      return;
    }
    println("  mvn %s, %s", x0_or_w0(node->ty), x0_or_w0(node->ty));
    return;
  case ND_LOGAND:
  case ND_LOGOR: {
    // `a && b` is 0 as soon as one is 0; `a || b` is 1 as soon as one isn't.
    int c = count();
    bool is_and = node->kind == ND_LOGAND;
    gen_branch(node->lhs, !is_and, format(".L.done.%d", c));
    gen_branch(node->rhs, !is_and, format(".L.done.%d", c));
    println("  mov w0, %d", is_and);
    println("  b .L.end.%d", c);
    println(".L.done.%d:", c);
    println("  mov w0, %d", !is_and);
    println(".L.end.%d:", c);
    return;
  }
  case ND_FUNCALL:
    gen_funcall(node);
    return;
  case ND_LABEL_VAL:
    println("  adr x0, %s", node->unique_label);
    return;
  case ND_UNREACHABLE:
    // Nothing, as gcc and clang have it without optimization: code that
    // reaches it carries on.
    return;
  case ND_TRAP:
    println("  brk 1000");
    return;
  case ND_CLZ:
    // (As with gcc, the result for 0 is undefined; it's the width here.)
    gen_expr(node->lhs);
    println("  clz %s, %s", reg(0, node->lhs->ty->size), reg(0, node->lhs->ty->size));
    return;
  case ND_CTZ:
    // The zeros at the top of the bits reversed
    gen_expr(node->lhs);
    println("  rbit %s, %s", reg(0, node->lhs->ty->size), reg(0, node->lhs->ty->size));
    println("  clz %s, %s", reg(0, node->lhs->ty->size), reg(0, node->lhs->ty->size));
    return;
  case ND_POPCOUNT:
    // Counted in parallel within 2-, 4- and 8-bit fields, then summed by
    // a multiply, without NEON.
    gen_expr(node->lhs);
    if (node->lhs->ty->size < 8)
      println("  mov w0, w0");
    println("  lsr x9, x0, 1");
    println("  and x9, x9, 0x5555555555555555");
    println("  sub x0, x0, x9");
    println("  and x9, x0, 0x3333333333333333");
    println("  lsr x0, x0, 2");
    println("  and x0, x0, 0x3333333333333333");
    println("  add x0, x0, x9");
    println("  add x0, x0, x0, lsr 4");
    println("  and x0, x0, 0x0f0f0f0f0f0f0f0f");
    println("  mov x9, 0x0101010101010101");
    println("  mul x0, x0, x9");
    println("  lsr x0, x0, 56");
    return;
  case ND_BSWAP:
    gen_expr(node->lhs);
    if (node->ty->size == 8) {
      println("  rev x0, x0");
    } else if (node->ty->size == 4) {
      println("  rev w0, w0");
    } else {
      println("  rev16 w0, w0");
      println("  uxth w0, w0");
    }
    return;
  case ND_FENCE:
    println("  dmb ish");
    return;
  case ND_OVERFLOW:
    gen_overflow(node);
    return;
  case ND_FRAME_ADDR:
    // Every function's frame record, the caller's x29 then the return
    // address, is where its x29 points (see the prologue), so frames are
    // followed up the chain.
    println("  mov x0, x29");
    for (int i = 0; i < node->val; i++)
      println("  ldr x0, [x0]");
    return;
  case ND_VA_ARG:
    gen_va_arg(node);
    return;
  case ND_CAS:
    gen_cas(node);
    return;
  case ND_EXCH:
    gen_exch(node);
    return;
  }

  if (is_vector(node->lhs->ty)) {
    gen_vector_binary(node);
    return;
  }

  // Binary operators on floating-point operands: the right side in v1,
  // the left in v0
  if (is_flonum(node->lhs->ty)) {
    gen_expr(node->rhs);
    push_fp();
    gen_expr(node->lhs);
    pop_fp(1);

    // A long double's operators are calls; a comparison returns an int
    // that compares with 0 as the operands do (and doesn't for a NaN).
    if (node->lhs->ty->kind == TY_LDOUBLE) {
      switch (node->kind) {
      case ND_ADD: call_rt("__addtf3"); return;
      case ND_SUB: call_rt("__subtf3"); return;
      case ND_MUL: call_rt("__multf3"); return;
      case ND_DIV: call_rt("__divtf3"); return;
      case ND_EQ:
      case ND_NE:
      case ND_LT:
      case ND_LE: {
        char *name = node->kind == ND_EQ ? "__eqtf2" : node->kind == ND_NE ? "__netf2" :
                     node->kind == ND_LT ? "__lttf2" : "__letf2";
        call_rt(name);
        println("  cmp w0, 0");
        println("  cset w0, %s", int_cond(node->kind, false));
        return;
      }
      }
      error_tok(node->tok, "invalid expression");
    }

    char r = fp_letter(node->lhs->ty->size);
    switch (node->kind) {
    case ND_ADD: println("  fadd %c0, %c0, %c1", r, r, r); return;
    case ND_SUB: println("  fsub %c0, %c0, %c1", r, r, r); return;
    case ND_MUL: println("  fmul %c0, %c0, %c1", r, r, r); return;
    case ND_DIV: println("  fdiv %c0, %c0, %c1", r, r, r); return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      // As gcc does: == and != compare quietly; < and <= raise "invalid"
      // for a NaN.
      println("  %s %c0, %c1",
              node->kind == ND_LT || node->kind == ND_LE ? "fcmpe" : "fcmp", r, r);
      println("  cset w0, %s", fp_cond(node->kind));
      return;
    }
    error_tok(node->tok, "invalid expression");
  }

  if (is_int128(node->lhs->ty)) {
    gen_int128_binary(node);
    return;
  }

  // Binary operators on integers and pointers. A constant right side is
  // an immediate where it can be.
  char *r0 = x0_or_w0(node->lhs->ty);
  char *r1 = is_64bit(node->lhs->ty) ? "x1" : "w1";
  bool u = node->lhs->ty->is_unsigned;
  int64_t imm = imm12(node->rhs);

  switch (node->kind) {
  case ND_ADD:
  case ND_SUB:
    if (imm < 0)
      break;
    gen_expr(node->lhs);
    println("  %s %s, %s, %ld", node->kind == ND_ADD ? "add" : "sub", r0, r0, imm);
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    gen_compare(node);
    println("  cset w0, %s", int_cond(node->kind, u));
    return;
  case ND_SHL:
  case ND_SHR:
    if (is_foldable(node->rhs) && fold(node->rhs) >= 0 &&
        fold(node->rhs) < node->lhs->ty->size * 8 && node->lhs->ty->size >= 4) {
      gen_expr(node->lhs);
      println("  %s %s, %s, %ld", node->kind == ND_SHL ? "lsl" : u ? "lsr" : "asr", r0, r0,
              fold(node->rhs));
      return;
    }
    break;
  }

  gen_operands(node);

  switch (node->kind) {
  case ND_ADD:
    println("  add %s, %s, %s", r0, r0, r1);
    return;
  case ND_SUB:
    println("  sub %s, %s, %s", r0, r0, r1);
    return;
  case ND_MUL:
    println("  mul %s, %s, %s", r0, r0, r1);
    return;
  case ND_DIV:
    println("  %s %s, %s, %s", u ? "udiv" : "sdiv", r0, r0, r1);
    return;
  case ND_MOD: {
    // x - x / y * y
    char *r2 = is_64bit(node->lhs->ty) ? "x2" : "w2";
    println("  %s %s, %s, %s", u ? "udiv" : "sdiv", r2, r0, r1);
    println("  msub %s, %s, %s, %s", r0, r2, r1, r0);
    return;
  }
  case ND_BITAND:
    println("  and %s, %s, %s", r0, r0, r1);
    return;
  case ND_BITOR:
    println("  orr %s, %s, %s", r0, r0, r1);
    return;
  case ND_BITXOR:
    println("  eor %s, %s, %s", r0, r0, r1);
    return;
  case ND_SHL:
    println("  lsl %s, %s, %s", r0, r0, r1);
    return;
  case ND_SHR:
    println("  %s %s, %s, %s", u ? "lsr" : "asr", r0, r0, r1);
    return;
  }

  error_tok(node->tok, "invalid expression");
}

//---------- asm statements with operands ------------------------------------
//
// As in cgen.c: the parser has put each operand in a temporary local and
// picked its register (see asm_stmt() in parser.c). Here they are loaded,
// the template is written with the operands filled in, and outputs are
// stored back. A general register operand is x or w as its type is 8
// bytes or less, or as modifier x or w says; an FP/SIMD one (constraint
// w) is v0, or s0, d0, q0 ... with a modifier.

static char *asm_operand_text(Node *node, AsmOperand *op, char mod) {
  if (mod && !strchr("wxbhsdqcna", mod))
    error_tok(node->tok, "asm operand modifier '%c' is not supported", mod);

  if (op->kind == 'r') {
    if (mod == 'a')
      return format("[%s]", xreg[op->reg]);
    return reg(op->reg, mod == 'x' || (mod != 'w' && op->ty->size == 8) ? 8 : 4);
  }
  if (op->kind == 'x')
    return format("%c%d", mod && strchr("bhsdq", mod) ? mod : 'v', op->reg);
  if (op->kind == 'm')
    return format("[%s]", xreg[op->reg]);

  // A constant: `5`, or with n negated; 0 with w or x is wzr or xzr.
  int64_t val = (mod == 'n') ? -op->val : op->val;
  if (!op->label) {
    if (!val && (mod == 'w' || mod == 'x'))
      return mod == 'w' ? "wzr" : "xzr";
    return format("%ld", val);
  }
  if (!val)
    return *op->label;
  return format("%s%+ld", *op->label, val);
}

// The letter of the register view that holds an FP/SIMD operand's type
static char asm_fp_letter(Type *ty) {
  return fp_letter(ty->size);
}

// Loads operand `op`'s value, of type `ty` at `base` + `off`, into its
// register: zero-extended, for a general one.
static void asm_load(AsmOperand *op, char *base, int64_t off) {
  if (op->kind == 'x')
    println("  ldr %c%d, %s", asm_fp_letter(op->ty), op->reg, mem(base, off, op->ty->size));
  else
    load_reg_bytes(op->reg, base, off, op->ty->size);
}

// Stores the outputs in registers through their addresses.
static void store_asm_outputs(Node *node) {
  char *scratch = xreg[node->asm_scratch];
  for (int i = 0; i < node->asm_nops; i++) {
    AsmOperand *op = &node->asm_ops[i];
    if (!op->is_output || (op->kind != 'r' && op->kind != 'x'))
      continue;
    println("  ldr %s, %s", scratch, mem("x29", op->addr->offset, 8));
    if (op->kind == 'x')
      println("  str %c%d, [%s]", asm_fp_letter(op->ty), op->reg, scratch);
    else
      store_reg_bytes(op->reg, scratch, 0, op->ty->size);
  }
}

static void gen_asm(Node *node) {
  // The operands' values and addresses, into their temporaries
  for (Node *n = node->body; n; n = n->next)
    gen_stmt(n);

  // FP/SIMD operands first, while x9 is free to hold an address
  for (int i = 0; i < node->asm_nops; i++) {
    AsmOperand *op = &node->asm_ops[i];
    if (op->kind == 'x' && op->is_rw) {
      println("  ldr x9, %s", mem("x29", op->addr->offset, 8));
      asm_load(op, "x9", 0);
    } else if (op->kind == 'x' && op->value) {
      asm_load(op, "x29", op->value->offset);
    }
  }

  for (int i = 0; i < node->asm_nops; i++) {
    AsmOperand *op = &node->asm_ops[i];
    if ((op->kind == 'm' && op->addr) || (op->kind == 'r' && op->is_rw))
      println("  ldr %s, %s", xreg[op->reg], mem("x29", op->addr->offset, 8));
    if (op->kind == 'r' && op->is_rw)
      asm_load(op, xreg[op->reg], 0);
    else if (op->kind == 'r' && op->value)
      asm_load(op, "x29", op->value->offset);
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

  println("  %s", asm_template(node, targets, asm_operand_text));
  store_asm_outputs(node);
  if (stubs) {
    println("  b .L.asm_goto.%d", c);
    i = 0;
    for (Node *g = node->asm_labels; g; g = g->next, i++) {
      if (targets[i] == g->unique_label)
        continue;
      println("%s:", targets[i]);
      store_asm_outputs(node);
      if (g->lhs)
        gen_stmt(g->lhs);
      println("  b %s", g->unique_label);
    }
    println(".L.asm_goto.%d:", c);
  }
  free(targets);
}

//---------- Statements ------------------------------------------------------

// Compares `r` (x0 or w0, or another) with a switch's case value `val`:
// as cmp's or cmn's immediate if it fits, or through x9.
static void cmp_case(char *r, int64_t val) {
  bool is64 = r[0] == 'x';
  if (!is64)
    val = (int32_t)val;
  if (0 <= val && val < 4096) {
    println("  cmp %s, %ld", r, val);
  } else if (-4096 < val && val < 0) {
    println("  cmn %s, %ld", r, -val);
  } else {
    load_imm(is64 ? "x9" : "w9", val);
    println("  cmp %s, %s", r, is64 ? "x9" : "w9");
  }
}

static void gen_switch(Node *node) {
  gen_expr(node->cond);

  // On __int128 (in x0:x1), both halves are compared.
  if (is_int128(node->cond->ty)) {
    for (Node *n = node->case_next; n; n = n->case_next) {
      int c = count();
      if (n->begin == n->end && n->begin_hi == n->end_hi) {
        cmp_case("x0", n->begin);
        println("  b.ne .L.case.%d", c);
        cmp_case("x1", n->begin_hi);
        println("  b.eq %s", n->label);
        println(".L.case.%d:", c);
        continue;
      }

      // [GNU] A range: is x - begin <= end - begin, unsigned?
      unsigned __int128 b = (unsigned __int128)(uint64_t)n->begin_hi << 64 | (uint64_t)n->begin;
      unsigned __int128 e = (unsigned __int128)(uint64_t)n->end_hi << 64 | (uint64_t)n->end;
      unsigned __int128 d = e - b;
      load_imm("x9", n->begin);
      load_imm("x10", n->begin_hi);
      println("  subs x11, x0, x9");
      println("  sbc x12, x1, x10");
      cmp_case("x12", (int64_t)(d >> 64));
      println("  b.lo %s", n->label);
      println("  b.ne .L.case.%d", c);
      load_imm("x9", (int64_t)d);
      println("  cmp x11, x9");
      println("  b.ls %s", n->label);
      println(".L.case.%d:", c);
    }
  }

  for (Node *n = node->case_next; n && !is_int128(node->cond->ty); n = n->case_next) {
    bool is64 = node->cond->ty->size == 8;
    if (n->begin == n->end) {
      cmp_case(is64 ? "x0" : "w0", n->begin);
      println("  b.eq %s", n->label);
      continue;
    }

    // [GNU] Case ranges
    load_imm(is64 ? "x9" : "w9", n->begin);
    println("  sub %s, %s, %s", is64 ? "x10" : "w10", is64 ? "x0" : "w0", is64 ? "x9" : "w9");
    cmp_case(is64 ? "x10" : "w10", n->end - n->begin);
    println("  b.ls %s", n->label);
  }

  if (node->default_case)
    println("  b %s", node->default_case->label);

  println("  b %s", node->brk_label);
  gen_stmt(node->then);
  println("%s:", node->brk_label);
}

static void gen_stmt(Node *node) {
  emit_loc(node->tok);

  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_branch(node->cond, false, format(".L.else.%d", c));
    gen_stmt(node->then);
    println("  b .L.end.%d", c);
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
    println("  b .L.begin.%d", c);
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
    gen_switch(node);
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
    println("  b %s", node->unique_label);
    return;
  case ND_GOTO_EXPR:
    gen_expr(node->lhs);
    println("  br x0");
    return;
  case ND_LABEL:
    println("%s:", node->unique_label);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      Type *ty = node->lhs->ty;
      if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        if (is_ret_in_memory(ty))
          copy_struct_mem();
        else
          copy_struct_reg();
      }
    }
    println("  b .L.return.%s", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_discard(node->lhs);
    return;
  case ND_VLA_FREE:
    // With no temporaries on the stack, sp is the stack bottom, and both
    // go back up to where they were before the VLAs were allocated. With
    // some, in a statement expression, the VLAs stay until the function
    // returns (moving the temporaries isn't worth it).
    if (depth)
      return;
    gen_expr(node->lhs);
    println("  str x0, %s", mem("x29", current_fn->alloca_bottom->offset, 8));
    println("  mov sp, x0");
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

//---------- Stack frames ----------------------------------------------------
//
// A function's frame, from x29 (= sp after the prologue) up:
//
//   x29 + 0     the caller's x29 and the return address (the frame record)
//   x29 + 16    local variables, and parameters that came in registers
//   ...         the callee-saved registers it uses (regs_offset)
//   x29 + F     the parameters that came on the stack (F = stack_size)
//
// Below x29 is what alloca() takes, then the stack machine's temporaries.

// Is the function's return value written where x8 points? Its first
// parameter is that address then (see function() in parser.c).
static bool has_ret_buffer(Obj *fn) {
  Type *ty = fn->ty->return_ty;
  return (ty->kind == TY_STRUCT || ty->kind == TY_UNION) && is_ret_in_memory(ty);
}

// Gives each local variable its offset from x29, and each function the
// size of its frame and where its callee-saved registers go.
void assign_frames_arm64(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    // A parameter passed on the stack stays there, above the frame. It's
    // marked with offset 1 until the frame's size is known.
    ArgState s = {};
    for (Obj *var = fn->params; var; var = var->next) {
      if (var == fn->params && has_ret_buffer(fn))
        continue;
      ArgLoc loc = classify_arg(&s, var->ty);
      if (loc.kind == ARG_STACK && !loc.by_ref)
        var->offset = 1;
    }

    // The locals go down from the top of their area, newest first, as on
    // x86-64 (so a later one is at a higher address): `bottom` is how far
    // below the top each starts, a multiple of its alignment, and so is
    // the area's size, a multiple of 16 above x29 + 16. For now, a
    // local's offset is minus its `bottom`.
    int64_t bottom = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->offset || var->reg)
        continue;

      // x29 is only 16-byte aligned, so a local aligned above that gets
      // an 8-byte slot here, and the prologue carves its storage out of
      // the stack, aligned, and puts the address in the slot. (Not for a
      // parameter, whose value arrives in its slot.)
      // An array of 16 bytes or more is 16-byte aligned, as on x86-64 (and
      // as globals are, see emit_data()).
      int align = (var->ty->kind == TY_ARRAY && var->ty->size >= 16)
        ? MAX(16, var->align) : var->align;
      if (align > 16 && var->ty->kind != TY_VLA && !is_param(fn, var)) {
        var->is_overaligned = true;
        bottom = align_to(bottom + 8, 8);
      } else {
        bottom = align_to(bottom + var->ty->size, align);
      }
      var->offset = -bottom;
    }
    int64_t top = 16 + align_to(bottom, 16);
    for (Obj *var = fn->locals; var; var = var->next)
      if (!var->reg && var->offset <= 0)
        var->offset += top;

    // A frame's offsets are 32 bits, and a stack is megabytes anyway.
    for (Obj *var = fn->locals; var; var = var->next)
      if (var->ty->size > (1L << 30) && var->tok)
        error_tok(var->tok, "a local variable of more than 1 GiB is not supported "
                  "(a static or malloc() one can be larger)");
    if (top > (1L << 30))
      error_tok(fn->tok, "local variables of more than 1 GiB are not supported");

    top = align_to(top, 8);
    fn->regs_offset = top;
    fn->stack_size = align_to(top + fn->nregs * 8, 16);

    s = (ArgState){};
    for (Obj *var = fn->params; var; var = var->next) {
      if (var == fn->params && has_ret_buffer(fn))
        continue;
      ArgLoc loc = classify_arg(&s, var->ty);
      if (loc.kind == ARG_STACK && !loc.by_ref)
        var->offset = fn->stack_size + loc.offset;
    }
  }
}

//---------- Functions (the text section) ------------------------------------

// Moves the value of type `ty` in x`r` to register variable `var`,
// extended as its type says (a caller leaves a char's upper bits as
// they are).
static void move_to_var(Obj *var, int r) {
  Type *ty = var->ty;
  char *dst = var_reg(var);
  switch (ty->size) {
  case 1: println("  %s %s, %s", ty->is_unsigned ? "uxtb" : "sxtb", dst, wreg[r]); return;
  case 2: println("  %s %s, %s", ty->is_unsigned ? "uxth" : "sxth", dst, wreg[r]); return;
  }
  println("  mov %s, %s", dst, reg(r, ty->size));
}

// Stores the parameters that came in registers to their slots, or moves
// them to their registers, and copies the structs passed by a pointer.
// x0-x7 and v0-v7 are left as they were, but for the part of one holding
// a named parameter, so a variadic function saves them afterwards.
// Returns where the named parameters end, for va_start.
static ArgState store_params(Obj *fn) {
  ArgState s = {};
  for (Obj *var = fn->params; var; var = var->next) {
    Type *ty = var->ty;
    int64_t off = var->offset;

    // The address to return a struct at comes in x8.
    if (var == fn->params && has_ret_buffer(fn)) {
      if (var->reg)
        println("  mov %s, x8", var_reg(var));
      else
        println("  str x8, %s", mem("x29", off, 8));
      continue;
    }

    ArgLoc loc = classify_arg(&s, ty);
    switch (loc.kind) {
    case ARG_NONE:
      break;
    case ARG_STACK:
      if (loc.by_ref) {
        println("  ldr x12, %s", mem("x29", fn->stack_size + loc.offset, 8));
        add_imm("x13", "x29", off);
        copy_bytes("x13", "x12", ty->size);
      } else if (var->reg) {
        load_reg_bytes(9, "x29", off, ty->size);
        move_to_var(var, 9);
      }
      break;
    case ARG_FP:
      for (int i = 0; i < loc.nregs; i++)
        println("  str %c%d, %s", fp_letter(loc.part->size), loc.reg + i,
                mem("x29", off + i * loc.part->size, loc.part->size));
      break;
    case ARG_GP:
      if (loc.by_ref) {
        add_imm("x13", "x29", off);
        copy_bytes("x13", xreg[loc.reg], ty->size);
      } else if (var->reg) {
        move_to_var(var, loc.reg);
      } else {
        store_reg_bytes(loc.reg, "x29", off, MIN(8, ty->size));
        if (ty->size > 8)
          store_reg_bytes(loc.reg + 1, "x29", off + 8, ty->size - 8);
      }
      break;
    }
  }
  return s;
}

// A variadic function's va_list (__va_area__), which va_start copies,
// and the register save areas after it, laid out as the AAPCS64 says, so
// a va_list can be passed to code built by other compilers: __stack,
// __gr_top, __vr_top, __gr_offs and __vr_offs, then x0-x7, then q0-q7.
// gr_offs and vr_offs count up to 0 from minus the size of the saved
// registers that hold no named parameter.
static void save_va_area(Obj *fn, ArgState *s) {
  int64_t a = fn->va_area->offset;
  for (int i = 0; i < 8; i++)
    println("  str x%d, %s", i, mem("x29", a + 32 + i * 8, 8));
  for (int i = 0; i < 8; i++)
    println("  str q%d, %s", i, mem("x29", a + 96 + i * 16, 16));
  add_imm("x9", "x29", fn->stack_size + s->stack);
  println("  str x9, %s", mem("x29", a, 8));
  add_imm("x9", "x29", a + 96);
  println("  str x9, %s", mem("x29", a + 8, 8));
  add_imm("x9", "x29", a + 224);
  println("  str x9, %s", mem("x29", a + 16, 8));
  load_imm("w9", -(8 - MIN(s->gp, 8)) * 8);
  println("  str w9, %s", mem("x29", a + 24, 4));
  load_imm("w9", -(8 - MIN(s->fp, 8)) * 16);
  println("  str w9, %s", mem("x29", a + 28, 4));
}

void emit_text_arm64(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    // No code is emitted for "static inline" functions if no one is
    // referencing them.
    if (!fn->is_function || !fn->is_definition || !fn->is_live)
      continue;

    emit_binding(prog, fn);
    if (fn->section)
      println("  .section %s,\"ax\",@progbits", fn->section);
    else
      println("  .text");
    println("  .balign 4");
    println("  .type %s, @function", fn->name);
    println("%s:", fn->name);
    current_fn = fn;
    emit_function_loc(fn);

    // Prologue: the frame record at the frame's bottom, x29 pointing to it
    int64_t size = fn->stack_size;
    if (size <= 504) {
      println("  stp x29, x30, [sp, -%ld]!", size);
    } else {
      add_imm("sp", "sp", -size);
      println("  stp x29, x30, [sp]");
    }
    println("  mov x29, sp");

    // Locals aligned above 16 (see assign_frames_arm64())
    for (Obj *var = fn->locals; var; var = var->next) {
      if (!var->is_overaligned)
        continue;
      add_imm("x9", "sp", -var->ty->size);
      println("  and sp, x9, %d", -var->align);
      println("  mov x9, sp");
      println("  str x9, %s", mem("x29", var->offset, 8));
    }
    println("  mov x9, sp");
    println("  str x9, %s", mem("x29", fn->alloca_bottom->offset, 8));
    for (int i = 0; i < fn->nregs; i++)
      println("  str x%d, %s", 19 + i, mem("x29", fn->regs_offset + i * 8, 8));

    ArgState s = store_params(fn);
    if (fn->va_area)
      save_va_area(fn, &s);

    gen_stmt(fn->body);
    assert(depth == 0);

    // [https://www.sigbus.info/n1570#5.1.2.2.3p1] Reaching the end of
    // main is returning 0.
    if (strcmp(fn->name, "main") == 0)
      println("  mov w0, 0");

    // Epilogue
    println(".L.return.%s:", fn->name);
    for (int i = 0; i < fn->nregs; i++)
      println("  ldr x%d, %s", 19 + i, mem("x29", fn->regs_offset + i * 8, 8));
    println("  mov sp, x29");
    if (size <= 504) {
      println("  ldp x29, x30, [sp], %ld", size);
    } else {
      println("  ldp x29, x30, [sp]");
      add_imm("sp", "sp", size);
    }
    println("  ret");
    println("  .size %s, .-%s", fn->name, fn->name); // as in cgen.c
    if (opt_g)
      println(".L.end.%s:", fn->name); // for the debug info's code range
  }
}
