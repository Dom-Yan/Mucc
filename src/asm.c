//============================================================================
// asm.c - STAGE 5: ASSEMBLE
//
// Turns the assembly text from cgen.c into an ELF object file (.o), so
// mucc doesn't need to run the system assembler. It knows the
// instructions and directives cgen.c writes, and encodes them exactly as
// GNU as does: test/asm.sh checks that the two produce the same objects.
// Anything else, say an unusual instruction in an asm("...") statement,
// makes assemble_text() return false, and the driver runs `as` instead.
//
// It works in three passes:
//   1. Read each line and encode it into its section's bytes. Jumps and
//      references to symbols are left as holes, since addresses aren't
//      known yet.
//   2. Lay out each section: pick short (2-byte) or long jumps, then give
//      every label its address.
//   3. Fill in the holes, turning those only the linker can fill into
//      relocations, and write the ELF file, with DWARF line numbers (from
//      the .loc directives) for debuggers.
//============================================================================

#include "mucc.h"
#include <elf.h>

//---------- Sections, symbols and holes -------------------------------------

typedef struct Section Section;

typedef struct {
  unsigned char *data;
  int len;
  int cap;
} Bytes;

typedef struct {
  char *name;
  Section *sec;       // where it's defined, or NULL
  int pos;            // offset in sec->bytes where it's defined
  int njumps;         // number of jumps in sec before it
  uint64_t value;     // its address in sec, after layout
  bool is_global;     // .globl, or undefined
  bool is_local;      // .local
  bool is_tls;        // in a TLS section, or used as TLS
  int type;           // STT_NOTYPE, STT_FUNC or STT_OBJECT (.type)
  uint64_t size;      // .size
  int common_align;   // for .comm symbols; 0 otherwise
  bool keep;          // named by a relocation, so it must be in .symtab
  int index;          // its index in .symtab
} Sym;

// A jump to a label. It's short (2 bytes) or long (5 or 6); layout()
// decides, and finish_section() writes its bytes at `pos`.
typedef struct {
  int pos;
  int cc;             // condition code (4 for je, ...), or -1 for jmp
  Sym *target;
  bool is_long;
  uint64_t field;     // after layout: final offset of a long jump's rel32
} Jump;

// A 4- or 8-byte hole at `pos` for the address of `sym` + `addend`, made
// relative to the hole itself for pc-relative types.
typedef struct {
  int pos;
  int njumps;         // jumps before it
  int type;           // R_X86_64_*
  Sym *sym;
  int64_t addend;
} Hole;

// A hole the linker fills in: against `sym`, or against section `sec`.
typedef struct {
  uint64_t offset;
  int type;
  Sym *sym;
  Section *sec;
  int64_t addend;
} Reloc;

struct Section {
  char *name;
  int type;           // SHT_PROGBITS or SHT_NOBITS
  int flags;          // SHF_*
  int align;
  Bytes bytes;        // contents, without jumps (SHT_NOBITS: len only)
  Jump *jumps;
  int njumps, capjumps;
  Hole *holes;
  int nholes, capholes;
  Reloc *relocs;
  int nrelocs, caprelocs;
  int *jumps_before;  // after layout: total size of jumps 0..i-1
  uint64_t size;      // after layout
  int index;          // in the ELF section table
  bool needs_secsym;  // a relocation is relative to this section
  int secsym;         // its section symbol's index in .symtab
};

// A .loc directive: source position of the code that follows.
typedef struct {
  Section *sec;
  int pos;
  int njumps;
  int file;
  int line;
} Loc;

static Section **sections;
static int nsections, capsections;
static Section *cur;        // the current section
static Section *text;

static HashMap symbols;
static Sym **symlist;       // every symbol, in order of creation
static int nsyms, capsyms;

static char *file_name;     // from `.file "name"`
static StringArray dwarf_files; // from `.file N "name"`: [N - 1]
static Loc *locs;
static int nlocs, caplocs;

static int numeric_labels[100]; // how many times `N:` was defined

static jmp_buf *fail_jmp;
static char *fail_msg;
static char *stmt;          // the statement being assembled, for errors
static char *p;             // the parse position in it

// Gives up on the whole file; see assemble_text().
__attribute__((format(printf, 1, 2)))
static noreturn void fail(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fail_msg = format("%s, in '%s'", vformat(fmt, ap), stmt ? stmt : "");
  va_end(ap);
  longjmp(*fail_jmp, 1);
}

// Makes room for one more element in a growing array.
static void *grow(void *arr, int n, int *cap, int elem_size) {
  if (n < *cap)
    return arr;
  *cap = *cap ? *cap * 2 : 64;
  return realloc(arr, (size_t)*cap * elem_size);
}

static void put(Bytes *b, int byte) {
  if (b->len == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 1024;
    b->data = realloc(b->data, b->cap);
  }
  b->data[b->len++] = byte;
}

// Appends `n` bytes of `val`, little-endian.
static void put_n(Bytes *b, int n, uint64_t val) {
  for (int i = 0; i < n; i++)
    put(b, (val >> (i * 8)) & 0xff);
}

static void put_str(Bytes *b, char *s) {
  do {
    put(b, *s);
  } while (*s++);
}

static void put_uleb(Bytes *b, uint64_t val) {
  do {
    int byte = val & 0x7f;
    val >>= 7;
    put(b, val ? byte | 0x80 : byte);
  } while (val);
}

static void put_sleb(Bytes *b, int64_t val) {
  for (;;) {
    int byte = val & 0x7f;
    val >>= 7;
    bool done = (val == 0 && !(byte & 0x40)) || (val == -1 && (byte & 0x40));
    put(b, done ? byte : byte | 0x80);
    if (done)
      return;
  }
}

static Section *find_section(char *name) {
  for (int i = 0; i < nsections; i++)
    if (!strcmp(sections[i]->name, name))
      return sections[i];
  return NULL;
}

static Section *new_section(char *name, int type, int flags) {
  Section *sec = calloc(1, sizeof(Section));
  sec->name = name;
  sec->type = type;
  sec->flags = flags;
  sec->align = 1;
  sections = grow(sections, nsections, &capsections, sizeof(Section *));
  sections[nsections++] = sec;
  return sec;
}

static bool is_dot_l(char *name) {
  return name[0] == '.' && name[1] == 'L';
}

static Sym *find_sym(char *name, int len) {
  Sym *sym = hashmap_get2(&symbols, name, len);
  if (sym)
    return sym;

  sym = calloc(1, sizeof(Sym));
  sym->name = strndup(name, len);
  hashmap_put2(&symbols, sym->name, len, sym);
  symlist = grow(symlist, nsyms, &capsyms, sizeof(Sym *));
  symlist[nsyms++] = sym;
  return sym;
}

static void define_sym(Sym *sym) {
  if (sym->sec)
    fail("'%s' is already defined", sym->name);
  sym->sec = cur;
  sym->pos = cur->bytes.len;
  sym->njumps = cur->njumps;
  if (cur->flags & SHF_TLS)
    sym->is_tls = true;
}

// Writing into the current section.

static void out(int byte) {
  if (cur->type == SHT_NOBITS)
    fail("data in a section without contents");
  put(&cur->bytes, byte);
}

static void out_n(int n, uint64_t val) {
  for (int i = 0; i < n; i++)
    out((val >> (i * 8)) & 0xff);
}

static void out_zeros(int n) {
  if (cur->type == SHT_NOBITS)
    cur->bytes.len += n;
  else
    while (n-- > 0)
      out(0);
}

// Leaves a `size`-byte hole for the linker or for pass 3.
static void hole(int type, Sym *sym, int64_t addend, int size) {
  cur->holes = grow(cur->holes, cur->nholes, &cur->capholes, sizeof(Hole));
  cur->holes[cur->nholes++] = (Hole){cur->bytes.len, cur->njumps, type, sym, addend};
  out_n(size, 0);
}

//---------- Reading operands ------------------------------------------------

typedef enum { GP, XMM, ST, RIP, SEG } RegKind;

typedef struct {
  char *name;
  RegKind kind;
  int num;            // 0-15 (%rax is 0, %r15 is 15)
  int size;           // bytes, for GP registers
  bool needs_rex;     // %spl %bpl %sil %dil: only reachable with REX
  bool is_high;       // %ah %ch %dh %bh: not reachable with REX
} Reg;

static Reg regs[] = {
  {"rax", GP, 0, 8}, {"rcx", GP, 1, 8}, {"rdx", GP, 2, 8}, {"rbx", GP, 3, 8},
  {"rsp", GP, 4, 8}, {"rbp", GP, 5, 8}, {"rsi", GP, 6, 8}, {"rdi", GP, 7, 8},
  {"r8", GP, 8, 8}, {"r9", GP, 9, 8}, {"r10", GP, 10, 8}, {"r11", GP, 11, 8},
  {"r12", GP, 12, 8}, {"r13", GP, 13, 8}, {"r14", GP, 14, 8}, {"r15", GP, 15, 8},
  {"eax", GP, 0, 4}, {"ecx", GP, 1, 4}, {"edx", GP, 2, 4}, {"ebx", GP, 3, 4},
  {"esp", GP, 4, 4}, {"ebp", GP, 5, 4}, {"esi", GP, 6, 4}, {"edi", GP, 7, 4},
  {"r8d", GP, 8, 4}, {"r9d", GP, 9, 4}, {"r10d", GP, 10, 4}, {"r11d", GP, 11, 4},
  {"r12d", GP, 12, 4}, {"r13d", GP, 13, 4}, {"r14d", GP, 14, 4}, {"r15d", GP, 15, 4},
  {"ax", GP, 0, 2}, {"cx", GP, 1, 2}, {"dx", GP, 2, 2}, {"bx", GP, 3, 2},
  {"sp", GP, 4, 2}, {"bp", GP, 5, 2}, {"si", GP, 6, 2}, {"di", GP, 7, 2},
  {"r8w", GP, 8, 2}, {"r9w", GP, 9, 2}, {"r10w", GP, 10, 2}, {"r11w", GP, 11, 2},
  {"r12w", GP, 12, 2}, {"r13w", GP, 13, 2}, {"r14w", GP, 14, 2}, {"r15w", GP, 15, 2},
  {"al", GP, 0, 1}, {"cl", GP, 1, 1}, {"dl", GP, 2, 1}, {"bl", GP, 3, 1},
  {"spl", GP, 4, 1, true}, {"bpl", GP, 5, 1, true},
  {"sil", GP, 6, 1, true}, {"dil", GP, 7, 1, true},
  {"r8b", GP, 8, 1}, {"r9b", GP, 9, 1}, {"r10b", GP, 10, 1}, {"r11b", GP, 11, 1},
  {"r12b", GP, 12, 1}, {"r13b", GP, 13, 1}, {"r14b", GP, 14, 1}, {"r15b", GP, 15, 1},
  {"ah", GP, 4, 1, false, true}, {"ch", GP, 5, 1, false, true},
  {"dh", GP, 6, 1, false, true}, {"bh", GP, 7, 1, false, true},
  {"xmm0", XMM, 0}, {"xmm1", XMM, 1}, {"xmm2", XMM, 2}, {"xmm3", XMM, 3},
  {"xmm4", XMM, 4}, {"xmm5", XMM, 5}, {"xmm6", XMM, 6}, {"xmm7", XMM, 7},
  {"xmm8", XMM, 8}, {"xmm9", XMM, 9}, {"xmm10", XMM, 10}, {"xmm11", XMM, 11},
  {"xmm12", XMM, 12}, {"xmm13", XMM, 13}, {"xmm14", XMM, 14}, {"xmm15", XMM, 15},
  {"st", ST, 0},
  {"rip", RIP, 0},
  {"fs", SEG, 0x64},
};

// Relocation suffixes, as in `foo@PLT`.
typedef enum { NO_SUFFIX, AT_PLT, AT_GOTPCREL, AT_TLSGD, AT_TPOFF } Suffix;

typedef enum { OP_REG, OP_IMM, OP_MEM, OP_SYM } OpKind;

// An operand: %reg, $imm, disp(%base), %fs:disp, or a label/symbol.
typedef struct {
  OpKind kind;
  bool indirect;      // `*%rax`, as in `jmp *%rax`
  Reg *reg;           // OP_REG; for OP_MEM, the base (NULL: absolute)
  int64_t val;        // immediate or displacement
  Sym *sym;           // symbol in the immediate or displacement
  Suffix suffix;
  Reg *seg;           // segment override, as in %fs:0
} Operand;

static HashMap reg_map;

static void skip_space(void) {
  while (*p == ' ' || *p == '\t')
    p++;
}

// Symbol names may also contain UTF-8, as C identifiers can.
static bool is_symchar(char c) {
  return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$' ||
         (unsigned char)c >= 0x80;
}

// Reads a number. Big positive ones like 18446744073709551615 wrap
// around to their 64-bit pattern, as GNU as does (here, -1).
static int64_t parse_number(char **rest, char *s) {
  if (*s == '-')
    return strtoll(s, rest, 0);
  return (int64_t)strtoull(s, rest, 0);
}

static Reg *read_reg(void) {
  p++; // '%'
  char *start = p;
  while (isalnum((unsigned char)*p))
    p++;
  Reg *reg = hashmap_get2(&reg_map, start, p - start);
  if (!reg)
    fail("unknown register '%%%.*s'", (int)(p - start), start);

  // %st(0) .. %st(7)
  if (reg->kind == ST && *p == '(') {
    static Reg st[8];
    int n = p[1] - '0';
    if (n < 0 || n > 7 || p[2] != ')')
      fail("bad x87 register");
    p += 3;
    st[n] = (Reg){"st", ST, n};
    return &st[n];
  }
  return reg;
}

// Reads a label name, with `1f`/`1b` numeric labels turned into
// internal names.
static Sym *read_sym(void) {
  char *start = p;
  if (isdigit((unsigned char)*p)) {
    int n = strtol(p, &p, 10);
    if (n >= 100 || (*p != 'f' && *p != 'b'))
      fail("bad numeric label");
    int count = numeric_labels[n] + (*p == 'f');
    p++;
    char *name = format(".Lnum.%d.%d", n, count);
    return find_sym(name, strlen(name));
  }

  while (is_symchar(*p))
    p++;
  if (p == start)
    fail("expected a symbol");
  return find_sym(start, p - start);
}

// Reads `123`, `sym`, `sym+8`, `sym@SUFFIX` or `1f` into op.
static void read_value(Operand *op) {
  if (isdigit((unsigned char)*p) || *p == '-') {
    // `1f` and `1b` are numeric labels, not numbers.
    char *q = p;
    while (isdigit((unsigned char)*q))
      q++;
    if (q != p && (*q == 'f' || *q == 'b') && !is_symchar(q[1])) {
      op->sym = read_sym();
      return;
    }
    op->val = parse_number(&p, p);
    return;
  }

  op->sym = read_sym();
  if (*p == '+' || *p == '-')
    op->val = strtoll(p, &p, 0);

  if (*p == '@') {
    p++;
    char *start = p;
    while (isalpha((unsigned char)*p))
      p++;
    int len = p - start;
    if (len == 3 && !strncmp(start, "PLT", 3))
      op->suffix = AT_PLT;
    else if (len == 8 && !strncmp(start, "GOTPCREL", 8))
      op->suffix = AT_GOTPCREL;
    else if (len == 5 && !strncmp(start, "tlsgd", 5))
      op->suffix = AT_TLSGD;
    else if (len == 5 && !strncmp(start, "tpoff", 5))
      op->suffix = AT_TPOFF;
    else
      fail("unknown relocation '@%.*s'", len, start);

    // GNU as adds this undefined symbol for any GOT or TLS relocation.
    if (op->suffix != AT_PLT)
      find_sym("_GLOBAL_OFFSET_TABLE_", 21)->keep = true;
  }
}

static void read_operand(Operand *op) {
  *op = (Operand){0};
  skip_space();

  if (*p == '$') {
    p++;
    op->kind = OP_IMM;
    read_value(op);
    return;
  }

  if (*p == '*') {
    p++;
    op->indirect = true;
  }

  if (*p == '%') {
    Reg *reg = read_reg();
    if (*p != ':') {
      op->kind = OP_REG;
      op->reg = reg;
      return;
    }
    if (reg->kind != SEG)
      fail("bad segment register");
    op->seg = reg;
    p++;
  }

  if (*p != '(')
    read_value(op);

  if (*p != '(') {
    op->kind = op->seg ? OP_MEM : OP_SYM; // %fs:0 is an absolute address
    return;
  }

  p++;
  op->kind = OP_MEM;
  op->reg = read_reg();
  if (*p != ')')
    fail("unsupported addressing mode");
  p++;
  if (op->reg->kind != GP && op->reg->kind != RIP)
    fail("bad base register");
  if (op->reg->kind == GP && op->reg->size != 8)
    fail("32-bit addressing is not supported");
  if (op->sym && op->reg->kind != RIP)
    fail("a symbol needs %%rip as base");
}

//---------- Encoding: prefixes, ModRM and immediates ------------------------

static bool wrote_rex; // whether the current instruction has a REX prefix

// An operand's register number for the ModRM rm field (or base).
static int rm_num(Operand *op) {
  if (op->kind == OP_REG || (op->kind == OP_MEM && op->reg && op->reg->kind == GP))
    return op->reg->num;
  return 0;
}

static bool needs_rex(Operand *op) {
  return op && op->kind == OP_REG && op->reg->needs_rex;
}

// The REX prefix, when needed: for 64-bit operands (w), for r8-r15 in the
// ModRM reg field or rm/base, or to reach %sil, %dil, %spl and %bpl.
static void rex(bool w, int reg, Operand *rm, bool force) {
  int rm_reg = rm ? rm_num(rm) : 0;
  int byte = 0x40 | w << 3 | (reg & 8) >> 1 | (rm_reg & 8) >> 3;
  wrote_rex = byte != 0x40 || force;
  if (wrote_rex)
    out(byte);
}

// The relocation for `sym@GOTPCREL(%rip)`: movs get the kinds the
// linker may turn into a plain lea, as GNU as does.
static int gotpcrel_type = R_X86_64_GOTPCREL;

// ModRM byte, with SIB and displacement, for ModRM.reg = `reg` and the
// register or memory operand `rm`. `imm_size` is the number of
// immediate bytes after it, which a %rip-relative address must count.
static void modrm(int reg, Operand *rm, int imm_size) {
  reg = (reg & 7) << 3;

  if (rm->kind == OP_REG) {
    out(0xc0 | reg | (rm->reg->num & 7));
    return;
  }
  if (rm->kind != OP_MEM)
    fail("expected a register or memory operand");

  // Absolute, like %fs:0: SIB with no base and no index.
  if (!rm->reg) {
    out(0x04 | reg);
    out(0x25);
    if (rm->sym)
      fail("unsupported absolute address");
    out_n(4, rm->val);
    return;
  }

  // %rip-relative: the field holds sym + disp - (end of instruction).
  if (rm->reg->kind == RIP) {
    out(0x05 | reg);
    if (!rm->sym) {
      out_n(4, rm->val);
      return;
    }
    int type = R_X86_64_PC32;
    if (rm->suffix == AT_GOTPCREL)
      type = gotpcrel_type;
    else if (rm->suffix == AT_TLSGD)
      type = R_X86_64_TLSGD;
    else if (rm->suffix != NO_SUFFIX)
      fail("unsupported relocation");
    if (type == R_X86_64_TLSGD)
      rm->sym->is_tls = true;
    hole(type, rm->sym, rm->val - 4 - imm_size, 4);
    return;
  }

  // disp(%base). (%rbp) and (%r13) need an explicit 0 displacement;
  // %rsp and %r12 as base need a SIB byte.
  int base = rm->reg->num & 7;
  int64_t disp = rm->val;
  int mod = (disp == 0 && base != 5) ? 0 : (disp >= -128 && disp <= 127) ? 1 : 2;
  out(mod << 6 | reg | (base == 4 ? 4 : base));
  if (base == 4)
    out(0x24);
  if (mod == 1)
    out(disp & 0xff);
  else if (mod == 2)
    out_n(4, disp);
}

static bool is_int8(int64_t v) {
  return v >= -128 && v <= 127;
}

static bool is_int32(int64_t v) {
  return v == (int32_t)v;
}

// An immediate for a `size`-byte operation, as the value the CPU sees:
// it must fit, and is sign-extended from `size` bytes (so $0xff for a
// byte is -1). 64-bit operations take 32-bit immediates.
static int64_t imm_value(Operand *imm, int size) {
  int64_t v = imm->val;
  if (size == 8) {
    if (!is_int32(v))
      fail("immediate doesn't fit in 32 bits");
    return v;
  }
  int bits = size * 8;
  if (v < -(1LL << (bits - 1)) || v >= (1LL << bits))
    fail("immediate doesn't fit in the operand");
  return (int64_t)((uint64_t)v << (64 - bits)) >> (64 - bits);
}

// Writes immediate `imm` in `size` bytes (a relocation, if symbolic).
static void out_imm(Operand *imm, int size) {
  if (!imm->sym) {
    out_n(size, imm->val);
    return;
  }
  if (size != 4)
    fail("symbolic immediate must be 32 bits");
  if (imm->suffix == AT_TPOFF) {
    imm->sym->is_tls = true;
    hole(R_X86_64_TPOFF32, imm->sym, imm->val, 4);
    return;
  }
  if (imm->suffix != NO_SUFFIX)
    fail("unsupported relocation");
  hole(R_X86_64_32S, imm->sym, imm->val, 4);
}

//---------- Instructions ----------------------------------------------------

typedef enum {
  ALU,      // add or adc sbb and sub xor cmp: op is the /digit
  SHIFT,    // rol ror rcl rcr shl shr sar: op is the /digit
  UNARY,    // not neg mul div idiv (F6/F7): op is the /digit
  INCDEC,   // inc dec (FE/FF): op is the /digit
  IMUL,     // imul, one or two operands
  MOV,
  MOVX,     // movsbl, movzwl, ...: op is the second opcode byte
  MOVXX,    // movzb, movzx, movsx: size from the operands; op for byte source
  MOVSXD,
  LEA,
  PUSH,     // op is the base opcode (50 or 58)
  TEST,
  SETCC,    // op is the condition code
  JCC,      // op is the condition code, -1 for jmp
  CALL,
  FIXED,    // fixed bytes: bytes[0..op-1]
  PREFIX,   // a prefix byte (op) before the rest of the statement
  SSE,      // op xmm/mem, xmm with mandatory prefix `pre`
  SSEMOV,   // movsd, movss
  CVTSI,    // cvtsi2sd, cvtsi2ss (GP -> XMM)
  CVTTSI,   // cvttsd2si, cvttss2si (XMM -> GP)
  MOVQ,
  X87,      // x87 memory operand: opcode op, /digit ext
  FSTP,     // fstp %st(i)
  CMPXCHG,
  XCHG,
} InsnKind;

typedef struct {
  char *name;
  InsnKind kind;
  int op;
  int size;           // operand size given by the suffix (movl: 4), or 0
  int pre;            // SSE: mandatory prefix (0x66, 0xf2, 0xf3) or 0
  int ext;            // X87: the /digit
  int bytes[4];       // FIXED
} Insn;

static Insn insns[] = {
  {"add", ALU, 0}, {"or", ALU, 1}, {"adc", ALU, 2}, {"sbb", ALU, 3},
  {"and", ALU, 4}, {"sub", ALU, 5}, {"xor", ALU, 6}, {"cmp", ALU, 7},
  {"rol", SHIFT, 0}, {"ror", SHIFT, 1}, {"rcl", SHIFT, 2}, {"rcr", SHIFT, 3},
  {"shl", SHIFT, 4}, {"sal", SHIFT, 4}, {"shr", SHIFT, 5}, {"sar", SHIFT, 7},
  {"not", UNARY, 2}, {"neg", UNARY, 3}, {"mul", UNARY, 4},
  {"div", UNARY, 6}, {"idiv", UNARY, 7},
  {"inc", INCDEC, 0}, {"dec", INCDEC, 1},
  {"imul", IMUL},
  {"mov", MOV},
  {"movsbw", MOVX, 0xbe, 2}, {"movsbl", MOVX, 0xbe, 4}, {"movsbq", MOVX, 0xbe, 8},
  {"movswl", MOVX, 0xbf, 4}, {"movswq", MOVX, 0xbf, 8},
  {"movzbw", MOVX, 0xb6, 2}, {"movzbl", MOVX, 0xb6, 4}, {"movzbq", MOVX, 0xb6, 8},
  {"movzwl", MOVX, 0xb7, 4}, {"movzwq", MOVX, 0xb7, 8},
  {"movzb", MOVXX, 0xb6}, {"movzx", MOVXX, 0xb6}, {"movsx", MOVXX, 0xbe},
  {"movsxd", MOVSXD}, {"movslq", MOVSXD},
  {"lea", LEA},
  {"push", PUSH, 0x50}, {"pop", PUSH, 0x58},
  {"test", TEST},
  {"jmp", JCC, -1},
  {"call", CALL},
  {"ret", FIXED, 1, .bytes = {0xc3}},
  {"leave", FIXED, 1, .bytes = {0xc9}},
  {"nop", FIXED, 1, .bytes = {0x90}},
  {"cdq", FIXED, 1, .bytes = {0x99}},
  {"cltd", FIXED, 1, .bytes = {0x99}},
  {"cqo", FIXED, 2, .bytes = {0x48, 0x99}},
  {"cqto", FIXED, 2, .bytes = {0x48, 0x99}},
  {"cltq", FIXED, 2, .bytes = {0x48, 0x98}},
  {"ud2", FIXED, 2, .bytes = {0x0f, 0x0b}},
  {"stosb", FIXED, 1, .bytes = {0xaa}},
  {"faddp", FIXED, 2, .bytes = {0xde, 0xc1}},
  {"fmulp", FIXED, 2, .bytes = {0xde, 0xc9}},
  {"fsubrp", FIXED, 2, .bytes = {0xde, 0xe9}},
  {"fdivrp", FIXED, 2, .bytes = {0xde, 0xf9}},
  {"fcomip", FIXED, 2, .bytes = {0xdf, 0xf1}},
  {"fucomip", FIXED, 2, .bytes = {0xdf, 0xe9}},
  {"fchs", FIXED, 2, .bytes = {0xd9, 0xe0}},
  {"fldz", FIXED, 2, .bytes = {0xd9, 0xee}},
  {"fld1", FIXED, 2, .bytes = {0xd9, 0xe8}},
  {"lock", PREFIX, 0xf0}, {"rep", PREFIX, 0xf3},
  {"data16", PREFIX, 0x66}, {"rex64", PREFIX, 0x48},
  {"addsd", SSE, 0x58, 0, 0xf2}, {"mulsd", SSE, 0x59, 0, 0xf2},
  {"subsd", SSE, 0x5c, 0, 0xf2}, {"divsd", SSE, 0x5e, 0, 0xf2},
  {"addss", SSE, 0x58, 0, 0xf3}, {"mulss", SSE, 0x59, 0, 0xf3},
  {"subss", SSE, 0x5c, 0, 0xf3}, {"divss", SSE, 0x5e, 0, 0xf3},
  {"cvtsd2ss", SSE, 0x5a, 0, 0xf2}, {"cvtss2sd", SSE, 0x5a, 0, 0xf3},
  {"ucomisd", SSE, 0x2e, 0, 0x66}, {"ucomiss", SSE, 0x2e, 0, 0},
  {"xorpd", SSE, 0x57, 0, 0x66}, {"xorps", SSE, 0x57, 0, 0},
  {"pxor", SSE, 0xef, 0, 0x66},
  {"movsd", SSEMOV, 0, 0, 0xf2}, {"movss", SSEMOV, 0, 0, 0xf3},
  {"cvtsi2sd", CVTSI, 0, 0, 0xf2}, {"cvtsi2sdl", CVTSI, 0, 4, 0xf2},
  {"cvtsi2sdq", CVTSI, 0, 8, 0xf2},
  {"cvtsi2ss", CVTSI, 0, 0, 0xf3}, {"cvtsi2ssl", CVTSI, 0, 4, 0xf3},
  {"cvtsi2ssq", CVTSI, 0, 8, 0xf3},
  {"cvttsd2si", CVTTSI, 0, 0, 0xf2}, {"cvttsd2sil", CVTTSI, 0, 4, 0xf2},
  {"cvttsd2siq", CVTTSI, 0, 8, 0xf2},
  {"cvttss2si", CVTTSI, 0, 0, 0xf3}, {"cvttss2sil", CVTTSI, 0, 4, 0xf3},
  {"cvttss2siq", CVTTSI, 0, 8, 0xf3},
  {"movq", MOVQ},
  {"flds", X87, 0xd9, .ext = 0}, {"fldl", X87, 0xdd, .ext = 0},
  {"fldt", X87, 0xdb, .ext = 5},
  {"fstps", X87, 0xd9, .ext = 3}, {"fstpl", X87, 0xdd, .ext = 3},
  {"fstpt", X87, 0xdb, .ext = 7},
  {"fadds", X87, 0xd8, .ext = 0}, {"faddl", X87, 0xdc, .ext = 0},
  {"filds", X87, 0xdf, .ext = 0}, {"fildl", X87, 0xdb, .ext = 0},
  {"fildll", X87, 0xdf, .ext = 5}, {"fildq", X87, 0xdf, .ext = 5},
  {"fistps", X87, 0xdf, .ext = 3}, {"fistpl", X87, 0xdb, .ext = 3},
  {"fistpll", X87, 0xdf, .ext = 7}, {"fistpq", X87, 0xdf, .ext = 7},
  {"fldcw", X87, 0xd9, .ext = 5}, {"fnstcw", X87, 0xd9, .ext = 7},
  {"fstp", FSTP},
  {"cmpxchg", CMPXCHG},
  {"xchg", XCHG},
};

// Condition codes, for jCC and setCC.
static struct { char *name; int cc; } conds[] = {
  {"o", 0}, {"no", 1}, {"b", 2}, {"c", 2}, {"nae", 2}, {"ae", 3}, {"nb", 3},
  {"nc", 3}, {"e", 4}, {"z", 4}, {"ne", 5}, {"nz", 5}, {"be", 6}, {"na", 6},
  {"a", 7}, {"nbe", 7}, {"s", 8}, {"ns", 9}, {"p", 10}, {"pe", 10},
  {"np", 11}, {"po", 11}, {"l", 12}, {"nge", 12}, {"ge", 13}, {"nl", 13},
  {"le", 14}, {"ng", 14}, {"g", 15}, {"nle", 15},
};

static HashMap insn_map;

static void init_tables(void) {
  if (insn_map.capacity)
    return;
  for (int i = 0; i < sizeof(regs) / sizeof(*regs); i++)
    hashmap_put(&reg_map, regs[i].name, &regs[i]);
  for (int i = 0; i < sizeof(insns) / sizeof(*insns); i++)
    hashmap_put(&insn_map, insns[i].name, &insns[i]);
  for (int i = 0; i < sizeof(conds) / sizeof(*conds); i++) {
    Insn *j = calloc(1, sizeof(Insn));
    *j = (Insn){format("j%s", conds[i].name), JCC, conds[i].cc};
    hashmap_put(&insn_map, j->name, j);
    Insn *set = calloc(1, sizeof(Insn));
    *set = (Insn){format("set%s", conds[i].name), SETCC, conds[i].cc};
    hashmap_put(&insn_map, set->name, set);
  }
}

// Finds a mnemonic. `addq`, `movl` and the like are found as `add`,
// `mov` with the size their suffix gives.
static Insn *find_insn(char *name, int len) {
  Insn *insn = hashmap_get2(&insn_map, name, len);
  if (insn)
    return insn;

  char *suffix = "bwlq";
  char *s = len > 1 ? strchr(suffix, name[len - 1]) : NULL;
  if (!s)
    return NULL;
  insn = hashmap_get2(&insn_map, name, len - 1);
  if (!insn || insn->size)
    return NULL;
  switch (insn->kind) {
  case ALU: case SHIFT: case UNARY: case INCDEC: case IMUL: case MOV:
  case LEA: case PUSH: case TEST: case CMPXCHG: case XCHG: {
    Insn *sized = calloc(1, sizeof(Insn));
    *sized = *insn;
    sized->size = 1 << (s - suffix);
    hashmap_put2(&insn_map, strndup(name, len), len, sized);
    return sized;
  }
  default:
    return NULL;
  }
}

static bool is_gp(Operand *op) {
  return op->kind == OP_REG && op->reg->kind == GP;
}

static bool is_xmm(Operand *op) {
  return op->kind == OP_REG && op->reg->kind == XMM;
}

// The operand size: from the suffix, else from a GP register operand
// (the destination first).
static int op_size(Insn *insn, Operand *ops, int n) {
  if (insn->size)
    return insn->size;
  for (int i = n - 1; i >= 0; i--)
    if (is_gp(&ops[i]))
      return ops[i].reg->size;
  fail("can't tell the operand size");
}

static void operand_prefixes(Operand *ops, int n, int size) {
  for (int i = 0; i < n; i++)
    if (ops[i].kind == OP_MEM && ops[i].seg)
      out(ops[i].seg->num);
  if (size == 2)
    out(0x66);
}

// Size in bytes of an immediate for a `size`-byte operation.
static int imm_bytes(int size) {
  return size == 8 ? 4 : size;
}

static void encode_alu(Insn *insn, Operand *ops, int n) {
  if (n != 2)
    fail("expected 2 operands");
  Operand *src = &ops[0], *dst = &ops[1];
  int size = op_size(insn, ops, n);
  bool w = size == 8;
  int op = insn->op;
  operand_prefixes(ops, n, size);

  if (src->kind == OP_IMM) {
    int64_t val = src->sym ? 0 : imm_value(src, size);

    // Short form: 8-bit immediate, sign-extended.
    if (!src->sym && size != 1 && is_int8(val)) {
      rex(w, 0, dst, needs_rex(dst));
      out(0x83);
      modrm(op, dst, 1);
      out(val & 0xff);
      return;
    }

    // Accumulator form: add $imm, %eax.
    if (is_gp(dst) && dst->reg->num == 0 && !dst->reg->is_high) {
      rex(w, 0, NULL, false);
      out((size == 1 ? 0x04 : 0x05) + op * 8);
      out_imm(src, imm_bytes(size));
      return;
    }

    rex(w, 0, dst, needs_rex(dst));
    out(size == 1 ? 0x80 : 0x81);
    modrm(op, dst, imm_bytes(size));
    out_imm(src, imm_bytes(size));
    return;
  }

  if (is_gp(src)) {
    rex(w, src->reg->num, dst, needs_rex(src) || needs_rex(dst));
    out((size == 1 ? 0x00 : 0x01) + op * 8);
    modrm(src->reg->num, dst, 0);
    return;
  }

  if (src->kind == OP_MEM && is_gp(dst)) {
    rex(w, dst->reg->num, src, needs_rex(dst));
    out((size == 1 ? 0x02 : 0x03) + op * 8);
    modrm(dst->reg->num, src, 0);
    return;
  }
  fail("unsupported operands");
}

static void encode_shift(Insn *insn, Operand *ops, int n) {
  Operand *dst = &ops[n - 1];
  int size = op_size(insn, dst, 1);
  operand_prefixes(ops, n, size);
  rex(size == 8, 0, dst, needs_rex(dst));

  // Shift by 1 has its own opcode; GNU as uses it for $1 too.
  if (n == 1 || (ops[0].kind == OP_IMM && !ops[0].sym && ops[0].val == 1)) {
    out(size == 1 ? 0xd0 : 0xd1);
    modrm(insn->op, dst, 0);
    return;
  }
  if (is_gp(&ops[0]) && ops[0].reg->num == 1 && ops[0].reg->size == 1) {
    out(size == 1 ? 0xd2 : 0xd3); // by %cl
    modrm(insn->op, dst, 0);
    return;
  }
  if (ops[0].kind == OP_IMM && !ops[0].sym) {
    out(size == 1 ? 0xc0 : 0xc1);
    modrm(insn->op, dst, 1);
    out(ops[0].val & 0xff);
    return;
  }
  fail("unsupported operands");
}

// One-operand F6/F7 (not, neg, mul, imul, div, idiv) and FE/FF (inc, dec).
static void encode_unary(Insn *insn, Operand *ops, int n, int opcode, int digit) {
  if (n != 1)
    fail("expected 1 operand");
  int size = op_size(insn, ops, n);
  operand_prefixes(ops, n, size);
  rex(size == 8, 0, &ops[0], needs_rex(&ops[0]));
  out(size == 1 ? opcode : opcode + 1);
  modrm(digit, &ops[0], 0);
}

static void encode_mov(Insn *insn, Operand *ops, int n) {
  if (n != 2)
    fail("expected 2 operands");
  Operand *src = &ops[0], *dst = &ops[1];
  int size = op_size(insn, ops, n);
  bool w = size == 8;
  operand_prefixes(ops, n, size);

  if (src->kind == OP_IMM && is_gp(dst)) {
    int r = dst->reg->num;
    if (size == 8) {
      // A 64-bit immediate needs movabs; others are sign-extended imm32.
      if (!src->sym && !is_int32(src->val)) {
        rex(true, 0, dst, false);
        out(0xb8 + (r & 7));
        out_n(8, src->val);
        return;
      }
      rex(true, 0, dst, false);
      out(0xc7);
      modrm(0, dst, 4);
      out_imm(src, 4);
      return;
    }
    if (!src->sym)
      imm_value(src, size); // range check
    rex(false, 0, dst, needs_rex(dst));
    out((size == 1 ? 0xb0 : 0xb8) + (r & 7));
    out_imm(src, size);
    return;
  }

  if (src->kind == OP_IMM && dst->kind == OP_MEM) {
    if (!src->sym)
      imm_value(src, size);
    rex(w, 0, dst, false);
    out(size == 1 ? 0xc6 : 0xc7);
    modrm(0, dst, imm_bytes(size));
    out_imm(src, imm_bytes(size));
    return;
  }

  if (is_gp(src) && (is_gp(dst) || dst->kind == OP_MEM)) {
    rex(w, src->reg->num, dst, needs_rex(src) || needs_rex(dst));
    out(size == 1 ? 0x88 : 0x89);
    modrm(src->reg->num, dst, 0);
    return;
  }

  if (src->kind == OP_MEM && is_gp(dst)) {
    rex(w, dst->reg->num, src, needs_rex(dst));
    out(size == 1 ? 0x8a : 0x8b);
    // A GOT load can be relaxed by the linker; say so, as GNU as does.
    gotpcrel_type = wrote_rex ? R_X86_64_REX_GOTPCRELX : R_X86_64_GOTPCRELX;
    modrm(dst->reg->num, src, 0);
    gotpcrel_type = R_X86_64_GOTPCREL;
    return;
  }
  fail("unsupported operands");
}

// movsbl, movzwl and friends: opcode 0F `op`, destination size `size`.
static void encode_movx(Operand *ops, int n, int op, int size) {
  if (n != 2 || !is_gp(&ops[1]))
    fail("expected a register destination");
  Operand *src = &ops[0], *dst = &ops[1];
  operand_prefixes(ops, n, size);
  rex(size == 8, dst->reg->num, src, needs_rex(src));
  out(0x0f);
  out(op);
  modrm(dst->reg->num, src, 0);
}

static void encode_jump(Insn *insn, Operand *ops, int n) {
  if (n != 1)
    fail("expected 1 operand");

  if (ops[0].indirect) {
    if (insn->op != -1)
      fail("conditional jumps can't be indirect");
    rex(false, 0, &ops[0], false);
    out(0xff);
    modrm(4, &ops[0], 0);
    return;
  }

  if (ops[0].kind != OP_SYM || ops[0].val)
    fail("expected a label");
  cur->jumps = grow(cur->jumps, cur->njumps, &cur->capjumps, sizeof(Jump));
  cur->jumps[cur->njumps++] = (Jump){cur->bytes.len, insn->op, ops[0].sym};
}

static void encode_call(Operand *ops, int n) {
  if (n != 1)
    fail("expected 1 operand");

  if (ops[0].indirect) {
    rex(false, 0, &ops[0], false);
    out(0xff);
    modrm(2, &ops[0], 0);
    return;
  }

  if (ops[0].kind != OP_SYM || (ops[0].suffix != NO_SUFFIX && ops[0].suffix != AT_PLT))
    fail("expected a function name");
  out(0xe8);
  hole(R_X86_64_PLT32, ops[0].sym, ops[0].val - 4, 4);
}

// An SSE instruction `op src, dst` where dst is an XMM register, like
// addsd: [prefix] [REX] 0F op ModRM.
static void encode_sse(int pre, bool w, int op, Operand *reg, Operand *rm) {
  if (pre)
    out(pre);
  rex(w, reg->reg->num, rm, false);
  out(0x0f);
  out(op);
  modrm(reg->reg->num, rm, 0);
}

// Encodes one instruction: mnemonic `name` (`len` chars), operands at p.
static void instruction(char *name, int len) {
  Insn *insn = find_insn(name, len);
  if (!insn)
    fail("unknown instruction '%.*s'", len, name);

  if (insn->kind == PREFIX) {
    out(insn->op);
    skip_space();
    if (*p) {
      char *start = p;
      while (isalnum((unsigned char)*p))
        p++;
      instruction(start, p - start);
    }
    return;
  }

  Operand ops[3];
  int n = 0;
  skip_space();
  while (*p) {
    if (n == 3)
      fail("too many operands");
    read_operand(&ops[n++]);
    skip_space();
    if (*p == ',')
      p++;
    else if (*p)
      fail("unexpected '%c'", *p);
  }

  wrote_rex = false;
  switch (insn->kind) {
  case ALU:
    encode_alu(insn, ops, n);
    return;
  case SHIFT:
    encode_shift(insn, ops, n);
    return;
  case UNARY:
    encode_unary(insn, ops, n, 0xf6, insn->op);
    return;
  case INCDEC:
    encode_unary(insn, ops, n, 0xfe, insn->op);
    return;
  case IMUL:
    if (n == 1) {
      encode_unary(insn, ops, n, 0xf6, 5);
      return;
    }
    if (n != 2 || !is_gp(&ops[1]))
      fail("unsupported operands");
    operand_prefixes(ops, n, op_size(insn, ops, n));
    rex(op_size(insn, ops, n) == 8, ops[1].reg->num, &ops[0], false);
    out(0x0f);
    out(0xaf);
    modrm(ops[1].reg->num, &ops[0], 0);
    return;
  case MOV:
    encode_mov(insn, ops, n);
    return;
  case MOVX:
    encode_movx(ops, n, insn->op, insn->size);
    return;
  case MOVXX: {
    // movzb/movzx/movsx: destination size from its register, source
    // size from its register (byte unless it's 16-bit).
    if (n != 2 || !is_gp(&ops[1]))
      fail("expected a register destination");
    int op = insn->op;
    if (is_gp(&ops[0]) && ops[0].reg->size == 2)
      op++;
    else if (!is_gp(&ops[0]) && insn->name[4] == 'x')
      fail("ambiguous source size");
    encode_movx(ops, n, op, ops[1].reg->size);
    return;
  }
  case MOVSXD:
    if (n != 2 || !is_gp(&ops[1]))
      fail("expected a register destination");
    rex(true, ops[1].reg->num, &ops[0], false);
    out(0x63);
    modrm(ops[1].reg->num, &ops[0], 0);
    return;
  case LEA:
    if (n != 2 || ops[0].kind != OP_MEM || !is_gp(&ops[1]))
      fail("unsupported operands");
    operand_prefixes(ops, 1, ops[1].reg->size);
    rex(ops[1].reg->size == 8, ops[1].reg->num, &ops[0], false);
    out(0x8d);
    modrm(ops[1].reg->num, &ops[0], 0);
    return;
  case PUSH:
    if (n != 1 || !is_gp(&ops[0]) || ops[0].reg->size != 8)
      fail("unsupported operand");
    if (ops[0].reg->num >= 8)
      out(0x41);
    out(insn->op + (ops[0].reg->num & 7));
    return;
  case TEST: {
    if (n != 2 || !is_gp(&ops[0]))
      fail("unsupported operands");
    int size = op_size(insn, ops, n);
    operand_prefixes(ops, n, size);
    rex(size == 8, ops[0].reg->num, &ops[1], needs_rex(&ops[0]) || needs_rex(&ops[1]));
    out(size == 1 ? 0x84 : 0x85);
    modrm(ops[0].reg->num, &ops[1], 0);
    return;
  }
  case SETCC:
    if (n != 1 || (is_gp(&ops[0]) && ops[0].reg->size != 1))
      fail("expected a byte register");
    rex(false, 0, &ops[0], needs_rex(&ops[0]));
    out(0x0f);
    out(0x90 + insn->op);
    modrm(0, &ops[0], 0);
    return;
  case JCC:
    encode_jump(insn, ops, n);
    return;
  case CALL:
    encode_call(ops, n);
    return;
  case FIXED:
    if (n)
      fail("expected no operands");
    for (int i = 0; i < insn->op; i++)
      out(insn->bytes[i]);
    return;
  case SSE:
    if (n != 2 || !is_xmm(&ops[1]))
      fail("expected an XMM destination");
    encode_sse(insn->pre, false, insn->op, &ops[1], &ops[0]);
    return;
  case SSEMOV:
    if (n != 2)
      fail("expected 2 operands");
    if (is_xmm(&ops[1]))
      encode_sse(insn->pre, false, 0x10, &ops[1], &ops[0]); // load or reg-reg
    else if (is_xmm(&ops[0]) && ops[1].kind == OP_MEM)
      encode_sse(insn->pre, false, 0x11, &ops[0], &ops[1]); // store
    else
      fail("unsupported operands");
    return;
  case CVTSI: {
    if (n != 2 || !is_xmm(&ops[1]))
      fail("expected an XMM destination");
    int size = insn->size ? insn->size : is_gp(&ops[0]) ? ops[0].reg->size : 0;
    if (size != 4 && size != 8)
      fail("can't tell the operand size");
    encode_sse(insn->pre, size == 8, 0x2a, &ops[1], &ops[0]);
    return;
  }
  case CVTTSI: {
    if (n != 2 || !is_gp(&ops[1]))
      fail("expected a register destination");
    int size = insn->size ? insn->size : ops[1].reg->size;
    encode_sse(insn->pre, size == 8, 0x2c, &ops[1], &ops[0]);
    return;
  }
  case MOVQ:
    if (n != 2)
      fail("expected 2 operands");
    if (is_gp(&ops[0]) && is_xmm(&ops[1])) {
      encode_sse(0x66, true, 0x6e, &ops[1], &ops[0]);
      return;
    }
    if (is_xmm(&ops[0]) && is_gp(&ops[1])) {
      encode_sse(0x66, true, 0x7e, &ops[0], &ops[1]);
      return;
    }
    if (is_xmm(&ops[0]) || is_xmm(&ops[1]))
      fail("unsupported operands");
    encode_mov(&(Insn){"movq", MOV, 0, 8}, ops, n);
    return;
  case X87:
    if (n != 1 || ops[0].kind != OP_MEM)
      fail("expected a memory operand");
    rex(false, 0, &ops[0], false);
    out(insn->op);
    modrm(insn->ext, &ops[0], 0);
    return;
  case FSTP:
    if (n != 1 || ops[0].kind != OP_REG || ops[0].reg->kind != ST)
      fail("unsupported operand");
    out(0xdd);
    out(0xd8 + ops[0].reg->num);
    return;
  case CMPXCHG: {
    if (n != 2 || !is_gp(&ops[0]))
      fail("unsupported operands");
    int size = op_size(insn, ops, n);
    operand_prefixes(ops, n, size);
    rex(size == 8, ops[0].reg->num, &ops[1], needs_rex(&ops[0]));
    out(0x0f);
    out(size == 1 ? 0xb0 : 0xb1);
    modrm(ops[0].reg->num, &ops[1], 0);
    return;
  }
  case XCHG: {
    if (n != 2)
      fail("expected 2 operands");
    Operand *reg = is_gp(&ops[0]) ? &ops[0] : &ops[1];
    Operand *rm = reg == &ops[0] ? &ops[1] : &ops[0];
    if (!is_gp(reg))
      fail("unsupported operands");
    int size = op_size(insn, ops, n);
    operand_prefixes(ops, n, size);
    rex(size == 8, reg->reg->num, rm, needs_rex(reg));
    out(size == 1 ? 0x86 : 0x87);
    modrm(reg->reg->num, rm, 0);
    return;
  }
  case PREFIX:
    break;
  }
  unreachable();
}

//---------- Directives ------------------------------------------------------

static void expect_comma(void) {
  skip_space();
  if (*p != ',')
    fail("expected ','");
  p++;
  skip_space();
}

static int64_t read_int(void) {
  skip_space();
  char *end;
  int64_t val = parse_number(&end, p);
  if (end == p)
    fail("expected a number");
  p = end;
  return val;
}

// Reads a double-quoted string (without escapes).
static char *read_string(void) {
  skip_space();
  if (*p != '"')
    fail("expected a string");
  char *start = ++p;
  while (*p && *p != '"')
    p++;
  if (*p != '"')
    fail("unterminated string");
  return strndup(start, p++ - start);
}

static void align_to_n(int n) {
  if (n <= 0 || (n & (n - 1)))
    fail("bad alignment");
  cur->align = MAX(cur->align, n);
  out_zeros((n - cur->bytes.len % n) % n);
}

// .section name[,"flags"[,@type]]
static void section_directive(void) {
  skip_space();
  char *start = p;
  while (*p && *p != ',' && *p != ' ')
    p++;
  char *name = strndup(start, p - start);

  int flags = 0;
  int type = SHT_PROGBITS;
  skip_space();
  if (*p == ',') {
    p++;
    for (char *f = read_string(); *f; f++) {
      if (*f == 'a')
        flags |= SHF_ALLOC;
      else if (*f == 'w')
        flags |= SHF_WRITE;
      else if (*f == 'x')
        flags |= SHF_EXECINSTR;
      else if (*f == 'T')
        flags |= SHF_TLS;
      else
        fail("unsupported section flag '%c'", *f);
    }
    skip_space();
    if (*p == ',') {
      p++;
      skip_space();
      if (!strncmp(p, "@nobits", 7))
        type = SHT_NOBITS;
      else if (strncmp(p, "@progbits", 9))
        fail("unsupported section type");
      while (*p)
        p++;
    }
  }

  Section *sec = find_section(name);
  cur = sec ? sec : new_section(name, type, flags);
}

static void directive(char *name, int len) {
#define IS(s) (len == sizeof(s) - 1 && !strncmp(name, s, len))
  skip_space();

  if (IS(".byte")) {
    int64_t v = read_int();
    if (v < -128 || v > 255)
      fail(".byte value out of range");
    out(v & 0xff);
  } else if (IS(".loc")) {
    int file = read_int();
    int line = read_int();
    locs = grow(locs, nlocs, &caplocs, sizeof(Loc));
    locs[nlocs++] = (Loc){cur, cur->bytes.len, cur->njumps, file, line};
    while (*p) // column etc., ignored
      p++;
  } else if (IS(".type")) {
    Sym *sym = read_sym();
    expect_comma();
    if (!strncmp(p, "@function", 9))
      sym->type = STT_FUNC;
    else if (!strncmp(p, "@object", 7))
      sym->type = STT_OBJECT;
    else
      fail("unsupported symbol type");
    while (*p)
      p++;
  } else if (IS(".local")) {
    read_sym()->is_local = true;
  } else if (IS(".globl") || IS(".global")) {
    read_sym()->is_global = true;
  } else if (IS(".align") || IS(".balign")) {
    align_to_n(read_int());
  } else if (IS(".size")) {
    Sym *sym = read_sym();
    expect_comma();
    sym->size = read_int();
  } else if (IS(".data")) {
    cur = find_section(".data");
  } else if (IS(".text")) {
    cur = text;
  } else if (IS(".bss")) {
    cur = find_section(".bss");
  } else if (IS(".section")) {
    section_directive();
  } else if (IS(".quad") || IS(".long") || IS(".value") || IS(".short")) {
    int size = IS(".quad") ? 8 : IS(".long") ? 4 : 2;
    Operand op = {0};
    read_value(&op);
    if (op.suffix != NO_SUFFIX)
      fail("unsupported relocation");
    if (!op.sym)
      out_n(size, op.val);
    else if (size == 8)
      hole(R_X86_64_64, op.sym, op.val, 8);
    else
      fail("symbol in a data directive smaller than .quad");
  } else if (IS(".zero")) {
    out_zeros(read_int());
  } else if (IS(".comm")) {
    Sym *sym = read_sym();
    expect_comma();
    int size = read_int();
    expect_comma();
    int align = read_int();
    if (sym->is_local) {
      // A local common symbol is simply allocated in .bss, as GNU as does.
      Section *saved = cur;
      cur = find_section(".bss");
      align_to_n(align);
      define_sym(sym);
      out_zeros(size);
      cur = saved;
    } else {
      sym->common_align = align;
      sym->is_global = true;
    }
    sym->size = size;
    sym->type = STT_OBJECT;
  } else if (IS(".file")) {
    if (*p == '"') {
      file_name = read_string();
    } else {
      int n = read_int();
      char *path = read_string();
      while (dwarf_files.len < n)
        strarray_push(&dwarf_files, "?");
      if (n < 1)
        fail("bad file number");
      dwarf_files.data[n - 1] = path;
    }
  } else {
    fail("unknown directive '%.*s'", len, name);
  }
#undef IS

  skip_space();
  if (*p)
    fail("unexpected text after directive");
}

//---------- Reading the input -----------------------------------------------

// Assembles one statement: optional labels, then a directive or an
// instruction.
static void statement(char *s) {
  stmt = s;
  p = s;

  for (;;) {
    skip_space();
    if (!*p)
      return;

    char *start = p;

    // A numeric label, `1:`
    if (isdigit((unsigned char)*p)) {
      int n = strtol(p, &p, 10);
      if (*p != ':' || n >= 100)
        fail("bad label");
      p++;
      char *name = format(".Lnum.%d.%d", n, ++numeric_labels[n]);
      define_sym(find_sym(name, strlen(name)));
      continue;
    }

    while (is_symchar(*p))
      p++;
    int len = p - start;
    if (len == 0)
      fail("syntax error");

    if (*p == ':') {
      p++;
      define_sym(find_sym(start, len));
      continue;
    }

    if (*start == '.')
      directive(start, len);
    else
      instruction(start, len);
    return;
  }
}

// Splits the text into statements: one per line, or several separated
// by ';'. Comments (#) are dropped. Works in place.
static void read_input(char *text) {
  char *s = text;       // start of the current statement
  bool in_string = false;

  for (char *q = text;; q++) {
    if (*q == '"')
      in_string = !in_string;

    if (!in_string && *q == '#') {
      *q = '\0';
      while (q[1] && q[1] != '\n')
        q++;
      continue;
    }

    if (*q == '\0' || *q == '\n' || (!in_string && *q == ';')) {
      bool at_end = *q == '\0';
      *q = '\0';
      statement(s);
      if (at_end)
        break;
      s = q + 1;
      in_string = false;
    }
  }
  stmt = NULL;
}

//---------- Layout: jump sizes and addresses --------------------------------

static int jump_size(Jump *j) {
  if (!j->is_long)
    return 2;
  return j->cc < 0 ? 5 : 6;
}

// Can `j` be a short jump? Only to a local label in the same section,
// where the distance is known now.
static bool is_near(Jump *j, Section *sec) {
  return j->target->sec == sec && !j->target->is_global;
}

static uint64_t sym_addr(Sym *sym) {
  return sym->pos + sym->sec->jumps_before[sym->njumps];
}

// Starts with every near jump short and makes long the ones whose target
// is too far, until nothing changes, as GNU as does.
static void layout(Section *sec) {
  sec->jumps_before = calloc(sec->njumps + 1, sizeof(int));
  for (int i = 0; i < sec->njumps; i++)
    if (!is_near(&sec->jumps[i], sec))
      sec->jumps[i].is_long = true;

  for (;;) {
    for (int i = 0; i < sec->njumps; i++)
      sec->jumps_before[i + 1] = sec->jumps_before[i] + jump_size(&sec->jumps[i]);

    bool changed = false;
    for (int i = 0; i < sec->njumps; i++) {
      Jump *j = &sec->jumps[i];
      if (j->is_long)
        continue;
      int64_t from = j->pos + sec->jumps_before[i] + 2;
      int64_t dist = (int64_t)sym_addr(j->target) - from;
      if (!is_int8(dist)) {
        j->is_long = true;
        changed = true;
      }
    }
    if (!changed)
      break;
  }

  sec->size = sec->bytes.len + sec->jumps_before[sec->njumps];
}

//---------- Filling holes and making relocations ----------------------------

static void add_reloc(Section *sec, uint64_t offset, int type, Sym *sym,
                      Section *target, int64_t addend) {
  sec->relocs = grow(sec->relocs, sec->nrelocs, &sec->caprelocs, sizeof(Reloc));
  sec->relocs[sec->nrelocs++] = (Reloc){offset, type, sym, target, addend};
  if (sym)
    sym->keep = true;
  else
    target->needs_secsym = true;
}

static void patch(Bytes *b, uint64_t offset, int size, uint64_t val) {
  for (int i = 0; i < size; i++)
    b->data[offset + i] = (val >> (i * 8)) & 0xff;
}

// Fills the hole at `offset` in `sec` (whose bytes are final) for `sym`
// + `addend`. It's done here if the value is known now: a pc-relative
// reference to a local symbol in the same section. Otherwise it becomes a
// relocation: against the symbol's section for local symbols, which the
// linker can do without the symbol, else against the symbol.
static void fill_hole(Section *sec, uint64_t offset, int type, Sym *sym, int64_t addend) {
  bool pcrel = type == R_X86_64_PC32 || type == R_X86_64_PLT32;
  bool is_local = sym->sec && !sym->is_global;

  if (is_local && pcrel && sym->sec == sec) {
    int64_t val = (int64_t)sym->value + addend - (int64_t)offset;
    if (!is_int32(val))
      fail("jump too far");
    patch(&sec->bytes, offset, 4, val);
    return;
  }

  bool adjustable = type == R_X86_64_PC32 || type == R_X86_64_64 ||
                    type == R_X86_64_32 || type == R_X86_64_32S;
  if (is_local && adjustable) {
    add_reloc(sec, offset, type, NULL, sym->sec, sym->value + addend);
    return;
  }

  if (!sym->sec && !sym->common_align && is_dot_l(sym->name))
    fail("undefined label '%s'", sym->name);
  add_reloc(sec, offset, type, sym, NULL, addend);
}

// Writes the final bytes of `sec`, with its jumps in place, then fills
// its holes.
static void finish_section(Section *sec) {
  if (sec->type == SHT_NOBITS)
    return;

  Bytes out = {calloc(1, sec->size + 1), sec->size, sec->size + 1};
  uint64_t dst = 0;
  int src = 0;

  for (int i = 0; i < sec->njumps; i++) {
    Jump *j = &sec->jumps[i];
    memcpy(out.data + dst, sec->bytes.data + src, j->pos - src);
    dst += j->pos - src;
    src = j->pos;

    if (!j->is_long) {
      out.data[dst] = j->cc < 0 ? 0xeb : 0x70 + j->cc;
      out.data[dst + 1] = sym_addr(j->target) - (dst + 2);
      dst += 2;
      continue;
    }

    if (j->cc < 0) {
      out.data[dst++] = 0xe9;
    } else {
      out.data[dst++] = 0x0f;
      out.data[dst++] = 0x80 + j->cc;
    }
    j->field = dst; // filled below, like the holes
    dst += 4;
  }
  memcpy(out.data + dst, sec->bytes.data + src, sec->bytes.len - src);

  free(sec->bytes.data);
  sec->bytes = out;

  for (int i = 0; i < sec->njumps; i++)
    if (sec->jumps[i].is_long)
      fill_hole(sec, sec->jumps[i].field, R_X86_64_PC32, sec->jumps[i].target, -4);

  for (int i = 0; i < sec->nholes; i++) {
    Hole *h = &sec->holes[i];
    uint64_t offset = h->pos + sec->jumps_before[h->njumps];
    fill_hole(sec, offset, h->type, h->sym, h->addend);
  }
}

//---------- DWARF line numbers ----------------------------------------------

// From the .file N and .loc directives, makes the three sections a
// debugger needs to map addresses to source lines: .debug_line (the line
// table), and a minimal .debug_info (one compile unit pointing at it)
// with its .debug_abbrev. DWARF version 4.

enum {
  LINE_BASE = -5, LINE_RANGE = 14, OPCODE_BASE = 13,
  DW_LNS_copy = 1, DW_LNS_advance_pc = 2, DW_LNS_advance_line = 3,
  DW_LNS_set_file = 4, DW_LNE_end_sequence = 1, DW_LNE_set_address = 2,
};

static void add_line_table(Section *line) {
  Bytes *b = &line->bytes;
  put_n(b, 4, 0); // unit_length, patched below
  put_n(b, 2, 4); // version
  put_n(b, 4, 0); // header_length, patched below
  int header_start = b->len;

  put(b, 1);            // minimum_instruction_length
  put(b, 1);            // maximum_operations_per_instruction
  put(b, 1);            // default_is_stmt
  put(b, LINE_BASE & 0xff);
  put(b, LINE_RANGE);
  put(b, OPCODE_BASE);
  static int std_lengths[] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
  for (int i = 0; i < 12; i++)
    put(b, std_lengths[i]);

  put(b, 0); // no include_directories: names are full paths
  for (int i = 0; i < dwarf_files.len; i++) {
    put_str(b, dwarf_files.data[i]);
    put_uleb(b, 0); // directory
    put_uleb(b, 0); // mtime
    put_uleb(b, 0); // length
  }
  put(b, 0);
  patch(b, 6, 4, b->len - header_start);

  // The program: set the start address, then one row per .loc.
  put(b, 0);
  put_uleb(b, 9);
  put(b, DW_LNE_set_address);
  add_reloc(line, b->len, R_X86_64_64, NULL, text, 0);
  put_n(b, 8, 0);

  uint64_t addr = 0;
  int file = 1;
  int lineno = 1;
  for (int i = 0; i < nlocs; i++) {
    Loc *loc = &locs[i];
    if (loc->sec != text)
      continue;
    uint64_t a = loc->pos + text->jumps_before[loc->njumps];

    if (loc->file != file) {
      put(b, DW_LNS_set_file);
      put_uleb(b, loc->file);
      file = loc->file;
    }

    int line_adv = loc->line - lineno;
    if (line_adv < LINE_BASE || line_adv >= LINE_BASE + LINE_RANGE) {
      put(b, DW_LNS_advance_line);
      put_sleb(b, line_adv);
      line_adv = 0;
    }

    // A "special opcode" advances both and adds the row in one byte.
    uint64_t addr_adv = a - addr;
    uint64_t op = (line_adv - LINE_BASE) + LINE_RANGE * addr_adv + OPCODE_BASE;
    if (op > 255) {
      put(b, DW_LNS_advance_pc);
      put_uleb(b, addr_adv);
      op = (line_adv - LINE_BASE) + OPCODE_BASE;
    }
    put(b, op);
    addr = a;
    lineno = loc->line;
  }

  put(b, DW_LNS_advance_pc);
  put_uleb(b, text->size - addr);
  put(b, 0);
  put_uleb(b, 1);
  put(b, DW_LNE_end_sequence);
  patch(b, 0, 4, b->len - 4);
}

static void add_debug_info(void) {
  Section *line = new_section(".debug_line", SHT_PROGBITS, 0);
  Section *abbrev = new_section(".debug_abbrev", SHT_PROGBITS, 0);
  Section *info = new_section(".debug_info", SHT_PROGBITS, 0);
  add_line_table(line);

  // One abbreviation: a compile unit with no children.
  static int abbrevs[] = {
    1, 0x11, 0,     // code 1: DW_TAG_compile_unit, DW_CHILDREN_no
    0x10, 0x17,     // DW_AT_stmt_list, DW_FORM_sec_offset
    0x11, 0x01,     // DW_AT_low_pc, DW_FORM_addr
    0x12, 0x07,     // DW_AT_high_pc, DW_FORM_data8 (a length)
    0x03, 0x08,     // DW_AT_name, DW_FORM_string
    0x1b, 0x08,     // DW_AT_comp_dir, DW_FORM_string
    0x25, 0x08,     // DW_AT_producer, DW_FORM_string
    0x13, 0x05,     // DW_AT_language, DW_FORM_data2
    0, 0, 0,
  };
  for (int i = 0; i < sizeof(abbrevs) / sizeof(*abbrevs); i++)
    put(&abbrev->bytes, abbrevs[i]);

  char cwd[4096];
  if (!getcwd(cwd, sizeof(cwd)))
    strcpy(cwd, ".");

  Bytes *b = &info->bytes;
  put_n(b, 4, 0); // unit_length, patched below
  put_n(b, 2, 4); // version
  add_reloc(info, b->len, R_X86_64_32, NULL, abbrev, 0);
  put_n(b, 4, 0); // debug_abbrev_offset
  put(b, 8);      // address_size
  put_uleb(b, 1);
  add_reloc(info, b->len, R_X86_64_32, NULL, line, 0);
  put_n(b, 4, 0); // DW_AT_stmt_list
  add_reloc(info, b->len, R_X86_64_64, NULL, text, 0);
  put_n(b, 8, 0); // DW_AT_low_pc
  put_n(b, 8, text->size);
  put_str(b, file_name ? file_name : "");
  put_str(b, cwd);
  put_str(b, "mucc");
  put_n(b, 2, 0x1d); // DW_LANG_C11
  patch(b, 0, 4, b->len - 4);

  line->size = line->bytes.len;
  abbrev->size = abbrev->bytes.len;
  info->size = info->bytes.len;
}

//---------- Writing the ELF file --------------------------------------------

static void put_raw(Bytes *b, void *data, int size) {
  for (int i = 0; i < size; i++)
    put(b, ((unsigned char *)data)[i]);
}

static void add_elf_sym(Bytes *symtab, Bytes *strtab, char *name, int bind,
                        int type, int shndx, uint64_t value, uint64_t size) {
  Elf64_Sym s = {0};
  if (*name) {
    s.st_name = strtab->len;
    put_str(strtab, name);
  }
  s.st_info = ELF64_ST_INFO(bind, type);
  s.st_shndx = shndx;
  s.st_value = value;
  s.st_size = size;
  put_raw(symtab, &s, sizeof(s));
}

static void write_elf(char *path) {
  // Section indices: each section, then its .rela section if it has
  // relocations. Then .symtab, .strtab and .shstrtab.
  int nsh = 1;
  for (int i = 0; i < nsections; i++) {
    sections[i]->index = nsh++;
    if (sections[i]->nrelocs)
      nsh++;
  }
  int symtab_idx = nsh++;
  int strtab_idx = nsh++;
  int shstrtab_idx = nsh++;

  // Symbols: the null one, the file, section symbols and other local
  // symbols, then the global ones.
  Bytes symtab = {0}, strtab = {0};
  put(&strtab, 0);
  add_elf_sym(&symtab, &strtab, "", STB_LOCAL, STT_NOTYPE, SHN_UNDEF, 0, 0);
  if (file_name)
    add_elf_sym(&symtab, &strtab, file_name, STB_LOCAL, STT_FILE, SHN_ABS, 0, 0);

  for (int i = 0; i < nsections; i++) {
    if (sections[i]->needs_secsym) {
      sections[i]->secsym = symtab.len / sizeof(Elf64_Sym);
      add_elf_sym(&symtab, &strtab, "", STB_LOCAL, STT_SECTION, sections[i]->index, 0, 0);
    }
  }

  // Referenced symbols that aren't defined here are global.
  for (int i = 0; i < nsyms; i++)
    if (!symlist[i]->sec && !symlist[i]->common_align && symlist[i]->keep)
      symlist[i]->is_global = true;

  int first_global = 0;
  for (int pass = 0; pass < 2; pass++) {
    bool global = pass == 1;
    if (global)
      first_global = symtab.len / sizeof(Elf64_Sym);

    for (int i = 0; i < nsyms; i++) {
      Sym *sym = symlist[i];
      if (sym->is_global != global)
        continue;
      // .L labels stay out, unless a relocation names them.
      bool defined = sym->sec || sym->common_align;
      if (!sym->keep && (!defined || (!global && is_dot_l(sym->name))))
        continue;

      int type = sym->is_tls ? STT_TLS : sym->type;
      int shndx = sym->common_align ? SHN_COMMON : sym->sec ? sym->sec->index : SHN_UNDEF;
      uint64_t value = sym->common_align ? sym->common_align : sym->value;
      sym->index = symtab.len / sizeof(Elf64_Sym);
      add_elf_sym(&symtab, &strtab, sym->name, global ? STB_GLOBAL : STB_LOCAL,
                  type, shndx, value, sym->size);
    }
  }

  // Lay out the file: header, section contents, section headers.
  Bytes f = {0};
  for (int i = 0; i < sizeof(Elf64_Ehdr); i++)
    put(&f, 0);

  Elf64_Shdr *sh = calloc(nsh, sizeof(Elf64_Shdr));
  Bytes shstrtab = {0};
  put(&shstrtab, 0);

  for (int i = 0; i < nsections; i++) {
    Section *sec = sections[i];
    Elf64_Shdr *s = &sh[sec->index];
    s->sh_name = shstrtab.len;
    put_str(&shstrtab, sec->name);
    s->sh_type = sec->type;
    s->sh_flags = sec->flags;
    s->sh_addralign = sec->align;
    s->sh_size = sec->type == SHT_NOBITS ? sec->bytes.len : sec->size;

    while (f.len % sec->align)
      put(&f, 0);
    s->sh_offset = f.len;
    if (sec->type != SHT_NOBITS)
      put_raw(&f, sec->bytes.data, sec->size);

    if (!sec->nrelocs)
      continue;

    Elf64_Shdr *r = &sh[sec->index + 1];
    r->sh_name = shstrtab.len;
    put_str(&shstrtab, format(".rela%s", sec->name));
    r->sh_type = SHT_RELA;
    r->sh_flags = SHF_INFO_LINK;
    r->sh_link = symtab_idx;
    r->sh_info = sec->index;
    r->sh_addralign = 8;
    r->sh_entsize = sizeof(Elf64_Rela);

    while (f.len % 8)
      put(&f, 0);
    r->sh_offset = f.len;
    for (int k = 0; k < sec->nrelocs; k++) {
      Reloc *rel = &sec->relocs[k];
      int sym = rel->sym ? rel->sym->index : rel->sec->secsym;
      Elf64_Rela ra = {rel->offset, ELF64_R_INFO(sym, rel->type), rel->addend};
      put_raw(&f, &ra, sizeof(ra));
    }
    r->sh_size = f.len - r->sh_offset;
  }

  Elf64_Shdr *s = &sh[symtab_idx];
  s->sh_name = shstrtab.len;
  put_str(&shstrtab, ".symtab");
  s->sh_type = SHT_SYMTAB;
  s->sh_link = strtab_idx;
  s->sh_info = first_global;
  s->sh_addralign = 8;
  s->sh_entsize = sizeof(Elf64_Sym);
  while (f.len % 8)
    put(&f, 0);
  s->sh_offset = f.len;
  s->sh_size = symtab.len;
  put_raw(&f, symtab.data, symtab.len);

  s = &sh[strtab_idx];
  s->sh_name = shstrtab.len;
  put_str(&shstrtab, ".strtab");
  s->sh_type = SHT_STRTAB;
  s->sh_addralign = 1;
  s->sh_offset = f.len;
  s->sh_size = strtab.len;
  put_raw(&f, strtab.data, strtab.len);

  s = &sh[shstrtab_idx];
  s->sh_name = shstrtab.len;
  put_str(&shstrtab, ".shstrtab");
  s->sh_type = SHT_STRTAB;
  s->sh_addralign = 1;
  s->sh_offset = f.len;
  s->sh_size = shstrtab.len;
  put_raw(&f, shstrtab.data, shstrtab.len);

  while (f.len % 8)
    put(&f, 0);
  uint64_t shoff = f.len;
  put_raw(&f, sh, nsh * sizeof(Elf64_Shdr));

  Elf64_Ehdr eh = {0};
  memcpy(eh.e_ident, ELFMAG, SELFMAG);
  eh.e_ident[EI_CLASS] = ELFCLASS64;
  eh.e_ident[EI_DATA] = ELFDATA2LSB;
  eh.e_ident[EI_VERSION] = EV_CURRENT;
  eh.e_type = ET_REL;
  eh.e_machine = EM_X86_64;
  eh.e_version = EV_CURRENT;
  eh.e_shoff = shoff;
  eh.e_ehsize = sizeof(Elf64_Ehdr);
  eh.e_shentsize = sizeof(Elf64_Shdr);
  eh.e_shnum = nsh;
  eh.e_shstrndx = shstrtab_idx;
  memcpy(f.data, &eh, sizeof(eh));

  FILE *fp = fopen(path, "wb");
  if (!fp)
    error("cannot open output file: %s: %s", path, strerror(errno));
  fwrite(f.data, 1, f.len, fp);
  fclose(fp);
}

//---------- Entry point -----------------------------------------------------

static void reset(void) {
  sections = NULL;
  nsections = capsections = 0;
  symbols = (HashMap){0};
  symlist = NULL;
  nsyms = capsyms = 0;
  file_name = NULL;
  dwarf_files = (StringArray){0};
  locs = NULL;
  nlocs = caplocs = 0;
  memset(numeric_labels, 0, sizeof(numeric_labels));
  stmt = NULL;
}

// Assembles `src` (modified in place) into an ELF object file at
// `path`. Returns false, writing nothing, if the text uses something
// this assembler doesn't know; then *why says what.
bool assemble_text(char *src, char *path, char **why) {
  init_tables();
  reset();

  jmp_buf here;
  fail_jmp = &here;
  if (setjmp(here)) {
    *why = fail_msg;
    return false;
  }

  // GNU as always has these three, in this order.
  text = new_section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
  new_section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
  new_section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
  cur = text;
  read_input(src);

  // Mark the stack as not executable, as gcc does.
  new_section(".note.GNU-stack", SHT_PROGBITS, 0);

  for (int i = 0; i < nsections; i++)
    layout(sections[i]);
  for (int i = 0; i < nsyms; i++)
    if (symlist[i]->sec)
      symlist[i]->value = sym_addr(symlist[i]);
  for (int i = 0; i < nsections; i++)
    finish_section(sections[i]);

  if (nlocs && dwarf_files.len)
    add_debug_info();

  write_elf(path);
  return true;
}
