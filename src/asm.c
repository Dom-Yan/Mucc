//============================================================================
// asm.c - STAGE 5 of 6: ASSEMBLE
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
  int64_t len;
  int64_t cap;
} Bytes;

typedef struct Sym {
  char *name;
  Section *sec;       // where it's defined, or NULL
  int64_t pos;        // offset in sec->bytes where it's defined
  int njumps;         // number of jumps in sec before it
  uint64_t value;     // its address in sec, after layout
  bool is_global;     // .globl, or undefined
  bool is_weak;       // .weak (also global)
  bool is_local;      // .local
  bool is_tls;        // in a TLS section, or used as TLS
  int type;           // STT_NOTYPE, STT_FUNC or STT_OBJECT (.type)
  uint64_t size;      // .size
  int common_align;   // for .comm symbols; 0 otherwise
  bool keep;          // named by a relocation, so it must be in .symtab
  int index;          // its index in .symtab
  int visibility;     // STV_DEFAULT, or STV_HIDDEN and so on (.hidden)
  struct Sym *alias_of; // .set sym, target: the target
  struct Sym *size_from, *size_to; // .size sym, .-start: from start to here
  bool is_abs;        // `.set sym, 5`: a constant, `value`
  bool is_used;       // named in an operand or data (see read_value())
} Sym;

// A jump to a label. It's short (2 bytes) or long (5 or 6); layout()
// decides, and finish_section() writes its bytes at `pos`. On ARM64, a
// conditional branch is short (4 bytes) or long (8: the opposite branch
// over a b).
typedef struct {
  int pos;
  int cc;             // condition code (4 for je, ...), or -1 for jmp; on
                      // ARM64, the short branch's relocation type
  Sym *target;
  bool is_long;
  uint64_t field;     // after layout: final offset of a long jump's rel32
  uint32_t insn;      // ARM64: the short branch, without its offset
} Jump;

// A 4- or 8-byte hole at `pos` for the address of `sym` + `addend`, made
// relative to the hole itself for pc-relative types. With `minus`, it's
// for the constant `sym - minus + addend` instead, two labels in one
// section, in `size` bytes.
typedef struct {
  int pos;
  int njumps;         // jumps before it
  int type;           // R_X86_64_*
  Sym *sym;
  int64_t addend;
  Sym *minus;
  int size;
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
  int type;           // SHT_PROGBITS, SHT_NOBITS, SHT_INIT_ARRAY or SHT_FINI_ARRAY
  int flags;          // SHF_*
  int align;
  int entsize;        // the size of each piece of a merged (M) section
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

// Local common symbols (`.local x` then `.comm x, size, align`), which go
// at the end of .bss once all of it is read, as GNU as puts them
static Sym **lcomms;
static int *lcomm_aligns;
static int nlcomms;

// The sections .pushsection left, innermost last
static Section *section_stack[64];
static int nsection_stack;

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
  if (sym->sec || sym->is_abs)
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

// Adds `n` bytes to the current section and returns where they go, for
// .incbin to read a file into.
static unsigned char *reserve(int64_t n) {
  if (cur->type == SHT_NOBITS)
    fail("data in a section without contents");
  Bytes *b = &cur->bytes;
  if (b->len + n > INT32_MAX)
    fail("section over 2 GiB");
  if (b->len + n > b->cap) {
    b->cap = MAX(b->cap * 2, b->len + n);
    b->data = realloc(b->data, b->cap);
    if (!b->data)
      fail("out of memory");
  }
  b->len += n;
  return b->data + b->len - n;
}

static void out_zeros(int64_t n) {
  if (n <= 0)
    return;
  if (cur->type == SHT_NOBITS)
    cur->bytes.len += n;
  else
    memset(reserve(n), 0, n);
}

// Leaves a `size`-byte hole for the linker or for pass 3.
static void hole(int type, Sym *sym, int64_t addend, int size) {
  cur->holes = grow(cur->holes, cur->nholes, &cur->capholes, sizeof(Hole));
  cur->holes[cur->nholes++] = (Hole){cur->bytes.len, cur->njumps, type, sym, addend};
  out_n(size, 0);
}

// The relocation for an absolute address in `size` (4 or 8) bytes
static int abs_reloc(int size) {
  if (target_arm64)
    return size == 8 ? R_AARCH64_ABS64 : R_AARCH64_ABS32;
  return size == 8 ? R_X86_64_64 : R_X86_64_32;
}

// Leaves a `size`-byte hole for `sym - minus + addend`, filled in pass 3,
// when the labels' addresses are known.
static void diff_hole(Sym *sym, Sym *minus, int64_t addend, int size) {
  cur->holes = grow(cur->holes, cur->nholes, &cur->capholes, sizeof(Hole));
  cur->holes[cur->nholes++] =
    (Hole){cur->bytes.len, cur->njumps, 0, sym, addend, minus, size};
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

static int arm64_gp_reg_number(char *name);

// The number (0-15) of general register `name`, as in "r10", "%eax" or
// "ah", or -1. For asm statements' clobbers and register variables. (On
// ARM64, x0-x30 are 0-30.)
int gp_reg_number(char *name) {
  if (*name == '%')
    name++;
  if (target_arm64)
    return arm64_gp_reg_number(name);
  for (int i = 0; i < sizeof(regs) / sizeof(*regs); i++)
    if (regs[i].kind == GP && !strcmp(regs[i].name, name))
      return regs[i].is_high ? regs[i].num - 4 : regs[i].num;
  return -1;
}

// The name of general register `num` for `size` bytes, as in "r10d" (or
// on ARM64, "x10" or "w10").
char *gp_reg_name(int num, int size) {
  if (target_arm64)
    return format("%c%d", size == 8 ? 'x' : 'w', num);
  for (int i = 0; i < sizeof(regs) / sizeof(*regs); i++)
    if (regs[i].kind == GP && regs[i].num == num && regs[i].size == size &&
        !regs[i].is_high)
      return regs[i].name;
  return NULL;
}

// Relocation suffixes, as in `foo@PLT`.
typedef enum { NO_SUFFIX, AT_PLT, AT_GOTPCREL, AT_TLSGD, AT_TPOFF } Suffix;

typedef enum { OP_REG, OP_IMM, OP_MEM, OP_SYM } OpKind;

// An operand: %reg, $imm, disp(%base,%index,scale), %fs:disp, or a
// label/symbol.
typedef struct {
  OpKind kind;
  bool indirect;      // `*%rax`, as in `jmp *%rax`
  Reg *reg;           // OP_REG; for OP_MEM, the base (NULL: none)
  Reg *index;         // OP_MEM: the index register, or NULL
  int scale;          // OP_MEM: 1, 2, 4 or 8, with an index
  int64_t val;        // immediate or displacement
  Sym *sym;           // symbol in the immediate or displacement
  Sym *minus;         // a symbol subtracted from sym, as in `end - start`
  Suffix suffix;
  Reg *seg;           // segment override, as in %fs:0
} Operand;

static HashMap reg_map;

static void skip_space(void) {
  while (*p == ' ' || *p == '\t')
    p++;
}

// Symbol names may also contain UTF-8, as C identifiers can. (No
// isalnum(): it finds its table through a function call each time.)
static bool is_symchar(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') ||
         c == '_' || c == '.' || c == '$' || (unsigned char)c >= 0x80;
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

static int64_t read_sum(int64_t val);
static bool is_symstart(char c);

// A number, a negated one, a constant or a parenthesized sum.
static int64_t read_term(void) {
  skip_space();
  if (*p == '-') {
    p++;
    return (int64_t)(0 - (uint64_t)read_term());
  }
  if (*p == '(') {
    p++;
    int64_t val = read_sum(read_term());
    if (*p != ')')
      fail("expected ')'");
    p++;
    return val;
  }
  // A constant from .set
  if (is_symstart(*p)) {
    Sym *sym = read_sym();
    if (!sym->is_abs)
      fail("expected a number");
    return sym->value;
  }
  char *end;
  int64_t val = parse_number(&end, p);
  if (end == p)
    fail("expected a number");
  p = end;
  return val;
}

// Is operator `op` next, after any spaces? Then it's skipped.
static bool next_op(char *op) {
  char *q = p;
  while (*q == ' ' || *q == '\t')
    q++;
  if (strncmp(q, op, strlen(op)))
    return false;
  p = q + strlen(op);
  return true;
}

// Multiplies, divides or shifts `val` by the terms that follow, as in
// `4 * 8`: these bind tighter than + and -.
static int64_t read_product(int64_t val) {
  for (;;) {
    if (next_op("*")) {
      val = (uint64_t)val * read_term();
    } else if (next_op("/")) {
      int64_t t = read_term();
      if (!t)
        fail("division by zero");
      val /= t;
    } else if (next_op("<<")) {
      val = (uint64_t)val << (read_term() & 63);
    } else if (next_op(">>")) {
      val >>= read_term() & 63;
    } else {
      return val;
    }
  }
}

// Adds or subtracts the terms that follow to `val`, as in `72+8` or
// `(-1-2)`. Wraps around as GNU as does.
static int64_t read_sum(int64_t val) {
  val = read_product(val);
  for (;;) {
    bool minus = next_op("-");
    if (!minus && !next_op("+"))
      return val;
    uint64_t t = read_product(read_term());
    val = minus ? (uint64_t)val - t : (uint64_t)val + t;
  }
}

static bool is_symstart(char c) {
  return isalpha((unsigned char)c) || c == '_' || c == '.';
}

// Numbers added to or subtracted from a symbol, and at most one symbol
// subtracted, as in `end - start + 4`.
static void read_sym_terms(Operand *op) {
  for (;;) {
    char *save = p;
    skip_space();
    if (*p != '+' && *p != '-') {
      p = save;
      return;
    }
    bool minus = *p++ == '-';
    skip_space();
    if (minus && !op->minus && is_symstart(*p)) {
      char *save = p;
      Sym *sym = read_sym();
      if (!sym->is_abs) {
        op->minus = sym;
        sym->is_used = true;
        continue;
      }
      p = save;
    }
    uint64_t t = read_product(read_term());
    op->val = minus ? (uint64_t)op->val - t : (uint64_t)op->val + t;
  }
}

// A constant from .set in op is its value; another symbol is marked
// used, so a .set after can't make it a constant.
static void use_value_sym(Operand *op) {
  if (op->sym && op->sym->is_abs) {
    op->val = (uint64_t)op->val + op->sym->value;
    op->sym = NULL;
  }
  if (op->sym)
    op->sym->is_used = true;
}

// Reads `123`, `1+2`, `sym`, `sym+8`, `end - start`, `(end - start)`,
// `sym@SUFFIX` or `1f` into op.
static void read_value(Operand *op) {
  if (*p == '(' && is_symstart(p[1])) {
    p++;
    op->sym = read_sym();
    read_sym_terms(op);
    skip_space();
    if (*p != ')')
      fail("expected ')'");
    p++;
    op->val = read_sum(op->val);
    use_value_sym(op);
    return;
  }

  if (isdigit((unsigned char)*p) || *p == '-' || *p == '(') {
    // `1f` and `1b` are numeric labels, not numbers.
    char *q = p;
    while (isdigit((unsigned char)*q))
      q++;
    if (q != p && (*q == 'f' || *q == 'b') && !is_symchar(q[1])) {
      op->sym = read_sym();
      return;
    }
    op->val = read_sum(read_term());
    return;
  }

  op->sym = read_sym();
  read_sym_terms(op);
  use_value_sym(op);

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

  // A displacement, unless the `(` starts the registers: `(1+2)(%rdi)`.
  if (*p != '(' || (p[1] != '%' && p[1] != ',')) {
    read_value(op);
    if (op->minus)
      fail("a difference of labels is only supported as an immediate or data");
  }

  if (*p != '(') {
    op->kind = op->seg ? OP_MEM : OP_SYM; // %fs:0 is an absolute address
    return;
  }

  // (%base), (%base,%index), (%base,%index,scale) or (,%index,scale)
  p++;
  op->kind = OP_MEM;
  if (*p == '%')
    op->reg = read_reg();
  if (*p == ',') {
    p++;
    op->index = read_reg();
    op->scale = 1;
    if (*p == ',') {
      p++;
      op->scale = strtol(p, &p, 10);
    }
    if (op->index->kind != GP || op->index->size != 8 || op->index->num == 4)
      fail("bad index register");
    if (op->scale != 1 && op->scale != 2 && op->scale != 4 && op->scale != 8)
      fail("bad scale");
  }
  if (*p != ')')
    fail("unsupported addressing mode");
  p++;
  if (!op->reg && !op->index)
    fail("expected a register");
  if (op->reg && op->reg->kind != GP && (op->reg->kind != RIP || op->index))
    fail("bad base register");
  if (op->reg && op->reg->kind == GP && op->reg->size != 8)
    fail("32-bit addressing is not supported");
  if (op->sym && (!op->reg || op->reg->kind != RIP))
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
// ModRM reg field, index or rm/base, or to reach %sil, %dil, %spl and
// %bpl.
static void rex(bool w, int reg, Operand *rm, bool force) {
  int rm_reg = rm ? rm_num(rm) : 0;
  int index = rm && rm->kind == OP_MEM && rm->index ? rm->index->num : 0;
  int byte = 0x40 | w << 3 | (reg & 8) >> 1 | (index & 8) >> 2 | (rm_reg & 8) >> 3;
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

  // disp(%base,%index,scale): a SIB byte. With no base, base 5 and mod 0
  // mean a 32-bit displacement; %rbp and %r13 as base need mod 1 or 2.
  if (rm->index) {
    int base = rm->reg ? rm->reg->num & 7 : 5;
    int64_t disp = rm->val;
    int mod = !rm->reg || (disp == 0 && base != 5) ? 0 :
              (disp >= -128 && disp <= 127) ? 1 : 2;
    int ss = rm->scale == 8 ? 3 : rm->scale == 4 ? 2 : rm->scale == 2 ? 1 : 0;
    out(mod << 6 | reg | 4);
    out(ss << 6 | (rm->index->num & 7) << 3 | base);
    if (mod == 1)
      out(disp & 0xff);
    else if (mod == 2 || !rm->reg)
      out_n(4, disp);
    return;
  }

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
  if (imm->minus) {
    if (imm->suffix != NO_SUFFIX)
      fail("unsupported relocation");
    diff_hole(imm->sym, imm->minus, imm->val, size);
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
  SHXD,     // shld, shrd: 0F op by %cl, 0F op-1 by an imm8
  UNARY,    // not neg mul div idiv (F6/F7): op is the /digit
  INCDEC,   // inc dec (FE/FF): op is the /digit
  IMUL,     // imul, one or two operands
  REG_RM,   // bsf, bsr, popcnt, ... (and imul's two-operand form): 0F op,
            // into a register, after prefix `pre` if it has one
  BSWAP,    // bswap: 0F C8+r
  IO,       // in, out: op is the opcode with an imm8 port (E4 or E6)
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
  SSE,      // op xmm/mem, xmm with mandatory prefix `pre`. An op above
            // 0xff is in the 0F 38 or 0F 3A map: 0x3800 | op.
  SSE_IMM,  // the same with an imm8 first: pshufd $1, %xmm1, %xmm0. The
            // middle operand may be a GP register (pinsrw), with REX.W if
            // size is 8 (pinsrq).
  SSE_CMP,  // cmpeqps and the like: cmpps with predicate ext
  SSE_SHIFT, // psllw and the like: by xmm/mem (opcode op, or none if 0),
            // or by an imm8 (opcode bytes[0], /ext)
  SSE_GP,   // into a GP register from xmm (pmovmskb), and an imm8 first if
            // there are 3 operands (pextrw)
  SSE_EXTR, // pextrd $imm, %xmm, r/m: from xmm into a GP register or memory
  SSEMOV,   // movss, movups, ...: load opcode op, store opcode ext (0: none)
  CVTSI,    // cvtsi2sd, cvtsi2ss (GP -> XMM)
  CVTTSI,   // cvttsd2si, cvtsd2si, ... (XMM -> GP): opcode op
  MOVD,     // movd: between xmm and a 32-bit GP register or memory
  CRC32,
  MOVQ,
  X87,      // x87 or MXCSR memory operand: opcode op (1 or 2 bytes), /digit ext,
            // with REX.W if size is 8
  FSTP,     // fstp %st(i)
  CMPXCHG,  // cmpxchg, xadd: 0F op (byte) or op + 1, from a register
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
  {"shld", SHXD, 0xa5}, {"shrd", SHXD, 0xad},
  {"not", UNARY, 2}, {"neg", UNARY, 3}, {"mul", UNARY, 4},
  {"div", UNARY, 6}, {"idiv", UNARY, 7},
  {"inc", INCDEC, 0}, {"dec", INCDEC, 1},
  {"imul", IMUL, 0xaf},
  {"bsf", REG_RM, 0xbc}, {"bsr", REG_RM, 0xbd},
  {"popcnt", REG_RM, 0xb8, 0, 0xf3}, {"lzcnt", REG_RM, 0xbd, 0, 0xf3},
  {"tzcnt", REG_RM, 0xbc, 0, 0xf3},
  {"bswap", BSWAP},
  {"in", IO, 0xe4}, {"out", IO, 0xe6},
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
  {"syscall", FIXED, 2, .bytes = {0x0f, 0x05}},
  {"stosb", FIXED, 1, .bytes = {0xaa}},
  {"stosq", FIXED, 2, .bytes = {0x48, 0xab}},
  {"movsb", FIXED, 1, .bytes = {0xa4}},
  {"movsq", FIXED, 2, .bytes = {0x48, 0xa5}},
  {"cld", FIXED, 1, .bytes = {0xfc}},
  {"std", FIXED, 1, .bytes = {0xfd}},
  {"hlt", FIXED, 1, .bytes = {0xf4}},
  {"pause", FIXED, 2, .bytes = {0xf3, 0x90}},
  {"mfence", FIXED, 3, .bytes = {0x0f, 0xae, 0xf0}},
  {"lfence", FIXED, 3, .bytes = {0x0f, 0xae, 0xe8}},
  {"sfence", FIXED, 3, .bytes = {0x0f, 0xae, 0xf8}},
  {"cpuid", FIXED, 2, .bytes = {0x0f, 0xa2}},
  {"rdtsc", FIXED, 2, .bytes = {0x0f, 0x31}},
  {"rdtscp", FIXED, 3, .bytes = {0x0f, 0x01, 0xf9}},
  {"rdpmc", FIXED, 2, .bytes = {0x0f, 0x33}},
  {"xgetbv", FIXED, 3, .bytes = {0x0f, 0x01, 0xd0}},
  {"int3", FIXED, 1, .bytes = {0xcc}},
  // System instructions, for kernels and firmware
  {"cli", FIXED, 1, .bytes = {0xfa}},
  {"sti", FIXED, 1, .bytes = {0xfb}},
  {"clts", FIXED, 2, .bytes = {0x0f, 0x06}},
  {"invd", FIXED, 2, .bytes = {0x0f, 0x08}},
  {"wbinvd", FIXED, 2, .bytes = {0x0f, 0x09}},
  {"rdmsr", FIXED, 2, .bytes = {0x0f, 0x32}},
  {"wrmsr", FIXED, 2, .bytes = {0x0f, 0x30}},
  {"swapgs", FIXED, 3, .bytes = {0x0f, 0x01, 0xf8}},
  {"iretq", FIXED, 2, .bytes = {0x48, 0xcf}},
  {"sysretq", FIXED, 3, .bytes = {0x48, 0x0f, 0x07}},
  {"fnclex", FIXED, 2, .bytes = {0xdb, 0xe2}},
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
  {"sqrtsd", SSE, 0x51, 0, 0xf2}, {"sqrtss", SSE, 0x51, 0, 0xf3},
  {"minsd", SSE, 0x5d, 0, 0xf2}, {"maxsd", SSE, 0x5f, 0, 0xf2},
  {"minss", SSE, 0x5d, 0, 0xf3}, {"maxss", SSE, 0x5f, 0, 0xf3},
  {"addpd", SSE, 0x58, 0, 0x66}, {"mulpd", SSE, 0x59, 0, 0x66},
  {"subpd", SSE, 0x5c, 0, 0x66}, {"divpd", SSE, 0x5e, 0, 0x66},
  {"addps", SSE, 0x58, 0, 0}, {"mulps", SSE, 0x59, 0, 0},
  {"subps", SSE, 0x5c, 0, 0}, {"divps", SSE, 0x5e, 0, 0},
  {"sqrtpd", SSE, 0x51, 0, 0x66}, {"sqrtps", SSE, 0x51, 0, 0},
  {"andpd", SSE, 0x54, 0, 0x66}, {"andps", SSE, 0x54, 0, 0},
  {"andnpd", SSE, 0x55, 0, 0x66}, {"andnps", SSE, 0x55, 0, 0},
  {"orpd", SSE, 0x56, 0, 0x66}, {"orps", SSE, 0x56, 0, 0},
  {"cvtsd2ss", SSE, 0x5a, 0, 0xf2}, {"cvtss2sd", SSE, 0x5a, 0, 0xf3},
  {"ucomisd", SSE, 0x2e, 0, 0x66}, {"ucomiss", SSE, 0x2e, 0, 0},
  {"comisd", SSE, 0x2f, 0, 0x66}, {"comiss", SSE, 0x2f, 0, 0},
  {"xorpd", SSE, 0x57, 0, 0x66}, {"xorps", SSE, 0x57, 0, 0},
  {"pxor", SSE, 0xef, 0, 0x66},
  // SSE and SSE2 on packed values (mucc's vectors and <emmintrin.h>)
  {"minpd", SSE, 0x5d, 0, 0x66}, {"minps", SSE, 0x5d, 0, 0},
  {"maxpd", SSE, 0x5f, 0, 0x66}, {"maxps", SSE, 0x5f, 0, 0},
  {"rcpps", SSE, 0x53, 0, 0}, {"rcpss", SSE, 0x53, 0, 0xf3},
  {"rsqrtps", SSE, 0x52, 0, 0}, {"rsqrtss", SSE, 0x52, 0, 0xf3},
  {"unpcklps", SSE, 0x14, 0, 0}, {"unpckhps", SSE, 0x15, 0, 0},
  {"unpcklpd", SSE, 0x14, 0, 0x66}, {"unpckhpd", SSE, 0x15, 0, 0x66},
  {"movhlps", SSE, 0x12, 0, 0}, {"movlhps", SSE, 0x16, 0, 0},
  {"cvtps2pd", SSE, 0x5a, 0, 0}, {"cvtpd2ps", SSE, 0x5a, 0, 0x66},
  {"cvtdq2ps", SSE, 0x5b, 0, 0}, {"cvtps2dq", SSE, 0x5b, 0, 0x66},
  {"cvttps2dq", SSE, 0x5b, 0, 0xf3}, {"cvtdq2pd", SSE, 0xe6, 0, 0xf3},
  {"cvtpd2dq", SSE, 0xe6, 0, 0xf2}, {"cvttpd2dq", SSE, 0xe6, 0, 0x66},
  {"punpcklbw", SSE, 0x60, 0, 0x66}, {"punpcklwd", SSE, 0x61, 0, 0x66},
  {"punpckldq", SSE, 0x62, 0, 0x66}, {"packsswb", SSE, 0x63, 0, 0x66},
  {"pcmpgtb", SSE, 0x64, 0, 0x66}, {"pcmpgtw", SSE, 0x65, 0, 0x66},
  {"pcmpgtd", SSE, 0x66, 0, 0x66}, {"packuswb", SSE, 0x67, 0, 0x66},
  {"punpckhbw", SSE, 0x68, 0, 0x66}, {"punpckhwd", SSE, 0x69, 0, 0x66},
  {"punpckhdq", SSE, 0x6a, 0, 0x66}, {"packssdw", SSE, 0x6b, 0, 0x66},
  {"punpcklqdq", SSE, 0x6c, 0, 0x66}, {"punpckhqdq", SSE, 0x6d, 0, 0x66},
  {"pcmpeqb", SSE, 0x74, 0, 0x66}, {"pcmpeqw", SSE, 0x75, 0, 0x66},
  {"pcmpeqd", SSE, 0x76, 0, 0x66},
  {"paddq", SSE, 0xd4, 0, 0x66}, {"pmullw", SSE, 0xd5, 0, 0x66},
  {"psubusb", SSE, 0xd8, 0, 0x66}, {"psubusw", SSE, 0xd9, 0, 0x66},
  {"pminub", SSE, 0xda, 0, 0x66}, {"pand", SSE, 0xdb, 0, 0x66},
  {"paddusb", SSE, 0xdc, 0, 0x66}, {"paddusw", SSE, 0xdd, 0, 0x66},
  {"pmaxub", SSE, 0xde, 0, 0x66}, {"pandn", SSE, 0xdf, 0, 0x66},
  {"pavgb", SSE, 0xe0, 0, 0x66}, {"pavgw", SSE, 0xe3, 0, 0x66},
  {"pmulhuw", SSE, 0xe4, 0, 0x66}, {"pmulhw", SSE, 0xe5, 0, 0x66},
  {"psubsb", SSE, 0xe8, 0, 0x66}, {"psubsw", SSE, 0xe9, 0, 0x66},
  {"pminsw", SSE, 0xea, 0, 0x66}, {"por", SSE, 0xeb, 0, 0x66},
  {"paddsb", SSE, 0xec, 0, 0x66}, {"paddsw", SSE, 0xed, 0, 0x66},
  {"pmaxsw", SSE, 0xee, 0, 0x66},
  {"pmuludq", SSE, 0xf4, 0, 0x66}, {"pmaddwd", SSE, 0xf5, 0, 0x66},
  {"psadbw", SSE, 0xf6, 0, 0x66}, {"maskmovdqu", SSE, 0xf7, 0, 0x66},
  {"psubb", SSE, 0xf8, 0, 0x66}, {"psubw", SSE, 0xf9, 0, 0x66},
  {"psubd", SSE, 0xfa, 0, 0x66}, {"psubq", SSE, 0xfb, 0, 0x66},
  {"paddb", SSE, 0xfc, 0, 0x66}, {"paddw", SSE, 0xfd, 0, 0x66},
  {"paddd", SSE, 0xfe, 0, 0x66},
  {"pshufd", SSE_IMM, 0x70, 0, 0x66}, {"pshufhw", SSE_IMM, 0x70, 0, 0xf3},
  {"pshuflw", SSE_IMM, 0x70, 0, 0xf2},
  {"shufps", SSE_IMM, 0xc6, 0, 0}, {"shufpd", SSE_IMM, 0xc6, 0, 0x66},
  {"cmpps", SSE_IMM, 0xc2, 0, 0}, {"cmppd", SSE_IMM, 0xc2, 0, 0x66},
  {"cmpss", SSE_IMM, 0xc2, 0, 0xf3}, {"cmpsd", SSE_IMM, 0xc2, 0, 0xf2},
  {"pinsrw", SSE_IMM, 0xc4, 0, 0x66},
  {"psllw", SSE_SHIFT, 0xf1, 0, 0x66, 6, {0x71}},
  {"pslld", SSE_SHIFT, 0xf2, 0, 0x66, 6, {0x72}},
  {"psllq", SSE_SHIFT, 0xf3, 0, 0x66, 6, {0x73}},
  {"psrlw", SSE_SHIFT, 0xd1, 0, 0x66, 2, {0x71}},
  {"psrld", SSE_SHIFT, 0xd2, 0, 0x66, 2, {0x72}},
  {"psrlq", SSE_SHIFT, 0xd3, 0, 0x66, 2, {0x73}},
  {"psraw", SSE_SHIFT, 0xe1, 0, 0x66, 4, {0x71}},
  {"psrad", SSE_SHIFT, 0xe2, 0, 0x66, 4, {0x72}},
  {"pslldq", SSE_SHIFT, 0, 0, 0x66, 7, {0x73}},
  {"psrldq", SSE_SHIFT, 0, 0, 0x66, 3, {0x73}},
  {"pmovmskb", SSE_GP, 0xd7, 0, 0x66}, {"movmskps", SSE_GP, 0x50, 0, 0},
  {"movmskpd", SSE_GP, 0x50, 0, 0x66}, {"pextrw", SSE_GP, 0xc5, 0, 0x66},
  {"movsd", SSEMOV, 0x10, 0, 0xf2, 0x11}, {"movss", SSEMOV, 0x10, 0, 0xf3, 0x11},
  {"movups", SSEMOV, 0x10, 0, 0, 0x11}, {"movupd", SSEMOV, 0x10, 0, 0x66, 0x11},
  {"movaps", SSEMOV, 0x28, 0, 0, 0x29}, {"movapd", SSEMOV, 0x28, 0, 0x66, 0x29},
  {"movdqu", SSEMOV, 0x6f, 0, 0xf3, 0x7f}, {"movdqa", SSEMOV, 0x6f, 0, 0x66, 0x7f},
  {"movlps", SSEMOV, 0x12, 0, 0, 0x13}, {"movhps", SSEMOV, 0x16, 0, 0, 0x17},
  {"movlpd", SSEMOV, 0x12, 0, 0x66, 0x13}, {"movhpd", SSEMOV, 0x16, 0, 0x66, 0x17},
  {"movntps", SSEMOV, 0, 0, 0, 0x2b}, {"movntpd", SSEMOV, 0, 0, 0x66, 0x2b},
  {"movntdq", SSEMOV, 0, 0, 0x66, 0xe7},
  {"movd", MOVD},
  // SSE3
  {"addsubps", SSE, 0xd0, 0, 0xf2}, {"addsubpd", SSE, 0xd0, 0, 0x66},
  {"haddps", SSE, 0x7c, 0, 0xf2}, {"haddpd", SSE, 0x7c, 0, 0x66},
  {"hsubps", SSE, 0x7d, 0, 0xf2}, {"hsubpd", SSE, 0x7d, 0, 0x66},
  {"movshdup", SSE, 0x16, 0, 0xf3}, {"movsldup", SSE, 0x12, 0, 0xf3},
  {"movddup", SSE, 0x12, 0, 0xf2}, {"lddqu", SSE, 0xf0, 0, 0xf2},
  // SSSE3
  {"pshufb", SSE, 0x3800, 0, 0x66}, {"phaddw", SSE, 0x3801, 0, 0x66},
  {"phaddd", SSE, 0x3802, 0, 0x66}, {"phaddsw", SSE, 0x3803, 0, 0x66},
  {"pmaddubsw", SSE, 0x3804, 0, 0x66}, {"phsubw", SSE, 0x3805, 0, 0x66},
  {"phsubd", SSE, 0x3806, 0, 0x66}, {"phsubsw", SSE, 0x3807, 0, 0x66},
  {"psignb", SSE, 0x3808, 0, 0x66}, {"psignw", SSE, 0x3809, 0, 0x66},
  {"psignd", SSE, 0x380a, 0, 0x66}, {"pmulhrsw", SSE, 0x380b, 0, 0x66},
  {"pabsb", SSE, 0x381c, 0, 0x66}, {"pabsw", SSE, 0x381d, 0, 0x66},
  {"pabsd", SSE, 0x381e, 0, 0x66}, {"palignr", SSE_IMM, 0x3a0f, 0, 0x66},
  // SSE4.1. pblendvb, blendvps and blendvpd take %xmm0 as a third operand,
  // written first or left out.
  {"pblendvb", SSE, 0x3810, 0, 0x66}, {"blendvps", SSE, 0x3814, 0, 0x66},
  {"blendvpd", SSE, 0x3815, 0, 0x66}, {"ptest", SSE, 0x3817, 0, 0x66},
  {"pmovsxbw", SSE, 0x3820, 0, 0x66}, {"pmovsxbd", SSE, 0x3821, 0, 0x66},
  {"pmovsxbq", SSE, 0x3822, 0, 0x66}, {"pmovsxwd", SSE, 0x3823, 0, 0x66},
  {"pmovsxwq", SSE, 0x3824, 0, 0x66}, {"pmovsxdq", SSE, 0x3825, 0, 0x66},
  {"pmuldq", SSE, 0x3828, 0, 0x66}, {"pcmpeqq", SSE, 0x3829, 0, 0x66},
  {"movntdqa", SSE, 0x382a, 0, 0x66}, {"packusdw", SSE, 0x382b, 0, 0x66},
  {"pmovzxbw", SSE, 0x3830, 0, 0x66}, {"pmovzxbd", SSE, 0x3831, 0, 0x66},
  {"pmovzxbq", SSE, 0x3832, 0, 0x66}, {"pmovzxwd", SSE, 0x3833, 0, 0x66},
  {"pmovzxwq", SSE, 0x3834, 0, 0x66}, {"pmovzxdq", SSE, 0x3835, 0, 0x66},
  {"pminsb", SSE, 0x3838, 0, 0x66}, {"pminsd", SSE, 0x3839, 0, 0x66},
  {"pminuw", SSE, 0x383a, 0, 0x66}, {"pminud", SSE, 0x383b, 0, 0x66},
  {"pmaxsb", SSE, 0x383c, 0, 0x66}, {"pmaxsd", SSE, 0x383d, 0, 0x66},
  {"pmaxuw", SSE, 0x383e, 0, 0x66}, {"pmaxud", SSE, 0x383f, 0, 0x66},
  {"pmulld", SSE, 0x3840, 0, 0x66}, {"phminposuw", SSE, 0x3841, 0, 0x66},
  {"roundps", SSE_IMM, 0x3a08, 0, 0x66}, {"roundpd", SSE_IMM, 0x3a09, 0, 0x66},
  {"roundss", SSE_IMM, 0x3a0a, 0, 0x66}, {"roundsd", SSE_IMM, 0x3a0b, 0, 0x66},
  {"blendps", SSE_IMM, 0x3a0c, 0, 0x66}, {"blendpd", SSE_IMM, 0x3a0d, 0, 0x66},
  {"pblendw", SSE_IMM, 0x3a0e, 0, 0x66},
  {"pinsrb", SSE_IMM, 0x3a20, 0, 0x66}, {"insertps", SSE_IMM, 0x3a21, 0, 0x66},
  {"pinsrd", SSE_IMM, 0x3a22, 0, 0x66}, {"pinsrq", SSE_IMM, 0x3a22, 8, 0x66},
  {"dpps", SSE_IMM, 0x3a40, 0, 0x66}, {"dppd", SSE_IMM, 0x3a41, 0, 0x66},
  {"mpsadbw", SSE_IMM, 0x3a42, 0, 0x66},
  {"pextrb", SSE_EXTR, 0x3a14, 0, 0x66}, {"pextrd", SSE_EXTR, 0x3a16, 0, 0x66},
  {"pextrq", SSE_EXTR, 0x3a16, 8, 0x66}, {"extractps", SSE_EXTR, 0x3a17, 0, 0x66},
  // SSE4.2, AES and carry-less multiplication
  {"pcmpgtq", SSE, 0x3837, 0, 0x66},
  {"pcmpestrm", SSE_IMM, 0x3a60, 0, 0x66}, {"pcmpestri", SSE_IMM, 0x3a61, 0, 0x66},
  {"pcmpistrm", SSE_IMM, 0x3a62, 0, 0x66}, {"pcmpistri", SSE_IMM, 0x3a63, 0, 0x66},
  {"crc32", CRC32}, {"crc32b", CRC32, 0, 1}, {"crc32w", CRC32, 0, 2},
  {"crc32l", CRC32, 0, 4}, {"crc32q", CRC32, 0, 8},
  {"aesimc", SSE, 0x38db, 0, 0x66}, {"aesenc", SSE, 0x38dc, 0, 0x66},
  {"aesenclast", SSE, 0x38dd, 0, 0x66}, {"aesdec", SSE, 0x38de, 0, 0x66},
  {"aesdeclast", SSE, 0x38df, 0, 0x66},
  {"aeskeygenassist", SSE_IMM, 0x3adf, 0, 0x66}, {"pclmulqdq", SSE_IMM, 0x3a44, 0, 0x66},
  {"emms", FIXED, 2, .bytes = {0x0f, 0x77}},
  {"prefetchnta", X87, 0x0f18, .ext = 0}, {"prefetcht0", X87, 0x0f18, .ext = 1},
  {"prefetcht1", X87, 0x0f18, .ext = 2}, {"prefetcht2", X87, 0x0f18, .ext = 3},
  {"prefetchw", X87, 0x0f0d, .ext = 1},
  {"cvtsi2sd", CVTSI, 0, 0, 0xf2}, {"cvtsi2sdl", CVTSI, 0, 4, 0xf2},
  {"cvtsi2sdq", CVTSI, 0, 8, 0xf2},
  {"cvtsi2ss", CVTSI, 0, 0, 0xf3}, {"cvtsi2ssl", CVTSI, 0, 4, 0xf3},
  {"cvtsi2ssq", CVTSI, 0, 8, 0xf3},
  {"cvttsd2si", CVTTSI, 0x2c, 0, 0xf2}, {"cvttsd2sil", CVTTSI, 0x2c, 4, 0xf2},
  {"cvttsd2siq", CVTTSI, 0x2c, 8, 0xf2},
  {"cvttss2si", CVTTSI, 0x2c, 0, 0xf3}, {"cvttss2sil", CVTTSI, 0x2c, 4, 0xf3},
  {"cvttss2siq", CVTTSI, 0x2c, 8, 0xf3},
  {"cvtsd2si", CVTTSI, 0x2d, 0, 0xf2}, {"cvtsd2sil", CVTTSI, 0x2d, 4, 0xf2},
  {"cvtsd2siq", CVTTSI, 0x2d, 8, 0xf2},
  {"cvtss2si", CVTTSI, 0x2d, 0, 0xf3}, {"cvtss2sil", CVTTSI, 0x2d, 4, 0xf3},
  {"cvtss2siq", CVTTSI, 0x2d, 8, 0xf3},
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
  {"fldenv", X87, 0xd9, .ext = 4}, {"fnstenv", X87, 0xd9, .ext = 6},
  {"fnstsw", X87, 0xdd, .ext = 7},
  {"ldmxcsr", X87, 0x0fae, .ext = 2}, {"stmxcsr", X87, 0x0fae, .ext = 3},
  {"fxsave", X87, 0x0fae, .ext = 0}, {"fxrstor", X87, 0x0fae, .ext = 1},
  {"clflush", X87, 0x0fae, .ext = 7},
  {"sgdt", X87, 0x0f01, .ext = 0}, {"sidt", X87, 0x0f01, .ext = 1},
  {"lgdt", X87, 0x0f01, .ext = 2}, {"lidt", X87, 0x0f01, .ext = 3},
  {"invlpg", X87, 0x0f01, .ext = 7},
  {"cmpxchg16b", X87, 0x0fc7, 8, .ext = 1},
  {"fstp", FSTP},
  {"cmpxchg", CMPXCHG, 0xb0}, {"xadd", CMPXCHG, 0xc0},
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

// cmpps's predicates, as in cmpltps: cmpps $1
static char *cmp_preds[] = {"eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"};

static HashMap insn_map;

static void init_tables(void) {
  if (insn_map.capacity)
    return;
  for (int i = 0; i < 8; i++) {
    static char *types[] = {"ps", "pd", "ss", "sd"};
    static int pres[] = {0, 0x66, 0xf3, 0xf2};
    for (int j = 0; j < 4; j++) {
      Insn *insn = calloc(1, sizeof(Insn));
      *insn = (Insn){format("cmp%s%s", cmp_preds[i], types[j]), SSE_CMP, 0xc2, 0,
                     pres[j], i};
      hashmap_put(&insn_map, insn->name, insn);
    }
  }
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
  case LEA: case PUSH: case TEST: case CMPXCHG: case XCHG: case REG_RM:
  case BSWAP: case IO: case SHXD: {
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
// addsd: [prefix] [REX] 0F [38 or 3A] op ModRM, and `imm` bytes of
// immediate after it.
static void encode_sse_imm(int pre, bool w, int op, int reg, Operand *rm, int imm) {
  if (pre)
    out(pre);
  rex(w, reg, rm, needs_rex(rm));
  out(0x0f);
  if (op > 0xff)
    out(op >> 8);
  out(op & 0xff);
  modrm(reg, rm, imm);
}

static void encode_sse(int pre, bool w, int op, Operand *reg, Operand *rm) {
  encode_sse_imm(pre, w, op, reg->reg->num, rm, 0);
}

// The imm8 operand of an SSE instruction
static void sse_imm8(Operand *op) {
  if (op->kind != OP_IMM || op->sym)
    fail("expected a number");
  out(op->val & 0xff);
}

static void a64_instruction(char *name, int len);

// Encodes one instruction: mnemonic `name` (`len` chars), operands at p.
static void instruction(char *name, int len) {
  if (target_arm64) {
    a64_instruction(name, len);
    return;
  }
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
  case SHXD: {
    // shld count, src, dst: dst shifted, filled from src, by %cl or an imm8
    if (n != 3 || !is_gp(&ops[1]))
      fail("unsupported operands");
    bool by_cl = is_gp(&ops[0]) && ops[0].reg->num == 1 && ops[0].reg->size == 1;
    if (!by_cl && (ops[0].kind != OP_IMM || ops[0].sym))
      fail("expected %%cl or a number as the count");
    int size = op_size(insn, &ops[1], 2);
    operand_prefixes(ops, n, size);
    rex(size == 8, ops[1].reg->num, &ops[2], false);
    out(0x0f);
    out(by_cl ? insn->op : insn->op - 1);
    modrm(ops[1].reg->num, &ops[2], by_cl ? 0 : 1);
    if (!by_cl)
      out(ops[0].val & 0xff);
    return;
  }
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
    // fallthrough
  case REG_RM: {
    if (n != 2 || !is_gp(&ops[1]))
      fail("unsupported operands");
    int size = op_size(insn, ops, n);
    operand_prefixes(ops, n, size);
    if (insn->pre)
      out(insn->pre);
    rex(size == 8, ops[1].reg->num, &ops[0], false);
    out(0x0f);
    out(insn->op);
    modrm(ops[1].reg->num, &ops[0], 0);
    return;
  }
  case BSWAP: {
    if (n != 1 || !is_gp(&ops[0]) || ops[0].reg->size < 4)
      fail("expected a 32- or 64-bit register");
    rex(ops[0].reg->size == 8, 0, &ops[0], false);
    out(0x0f);
    out(0xc8 + (ops[0].reg->num & 7));
    return;
  }
  case IO: {
    // in port, %al/%ax/%eax; out %al/%ax/%eax, port. The port is an
    // imm8 or %dx.
    if (n != 2)
      fail("expected 2 operands");
    bool is_in = insn->op == 0xe4;
    Operand *acc = is_in ? &ops[1] : &ops[0];
    Operand *port = is_in ? &ops[0] : &ops[1];
    if (!is_gp(acc) || acc->reg->num != 0 || acc->reg->size == 8)
      fail("expected %%al, %%ax or %%eax");
    int size = acc->reg->size;
    if (insn->size && insn->size != size)
      fail("operand size mismatch");
    if (size == 2)
      out(0x66);
    if (is_gp(port) && port->reg->num == 2 && port->reg->size == 2) {
      out(insn->op + 8 + (size != 1));
      return;
    }
    if (port->kind != OP_IMM || port->sym || port->val < 0 || port->val > 255)
      fail("expected a port number from 0 to 255 or %%dx");
    out(insn->op + (size != 1));
    out(port->val);
    return;
  }
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
    if (n != 1 || (insn->size && insn->size != 8))
      fail("unsupported operand");
    if (is_gp(&ops[0]) && ops[0].reg->size == 8) {
      if (ops[0].reg->num >= 8)
        out(0x41);
      out(insn->op + (ops[0].reg->num & 7));
      return;
    }
    if (ops[0].kind == OP_MEM) {
      operand_prefixes(ops, n, 8);
      rex(false, 0, &ops[0], false);
      out(insn->op == 0x50 ? 0xff : 0x8f);
      modrm(insn->op == 0x50 ? 6 : 0, &ops[0], 0);
      return;
    }
    if (ops[0].kind == OP_IMM && insn->op == 0x50) {
      if (!ops[0].sym && is_int8(ops[0].val)) {
        out(0x6a);
        out(ops[0].val & 0xff);
        return;
      }
      if (!ops[0].sym)
        imm_value(&ops[0], 8); // range check
      out(0x68);
      out_imm(&ops[0], 4);
      return;
    }
    fail("unsupported operand");
  case TEST: {
    if (n != 2)
      fail("expected 2 operands");
    int size = op_size(insn, ops, n);
    operand_prefixes(ops, n, size);
    if (ops[0].kind == OP_IMM) {
      Operand *dst = &ops[1];
      if (is_gp(dst) && dst->reg->num == 0 && !dst->reg->is_high) {
        rex(size == 8, 0, NULL, false);
        out(size == 1 ? 0xa8 : 0xa9);
      } else {
        rex(size == 8, 0, dst, needs_rex(dst));
        out(size == 1 ? 0xf6 : 0xf7);
        modrm(0, dst, imm_bytes(size));
      }
      if (!ops[0].sym)
        imm_value(&ops[0], size); // range check
      out_imm(&ops[0], imm_bytes(size));
      return;
    }
    if (!is_gp(&ops[0]))
      fail("unsupported operands");
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
    // pblendvb %xmm0, %xmm1, %xmm2: %xmm0 is implied
    if (n == 3 && is_xmm(&ops[0]) && ops[0].reg->num == 0) {
      ops[0] = ops[1];
      ops[1] = ops[2];
      n = 2;
    }
    if (n != 2 || !is_xmm(&ops[1]))
      fail("expected an XMM destination");
    encode_sse(insn->pre, false, insn->op, &ops[1], &ops[0]);
    return;
  case SSE_IMM:
    if (n != 3 || !is_xmm(&ops[2]))
      fail("expected an immediate and an XMM destination");
    encode_sse_imm(insn->pre, insn->size == 8, insn->op, ops[2].reg->num, &ops[1], 1);
    sse_imm8(&ops[0]);
    return;
  case SSE_CMP:
    if (n != 2 || !is_xmm(&ops[1]))
      fail("expected an XMM destination");
    encode_sse_imm(insn->pre, false, insn->op, ops[1].reg->num, &ops[0], 1);
    out(insn->ext);
    return;
  case SSE_SHIFT:
    if (n != 2 || !is_xmm(&ops[1]))
      fail("expected an XMM destination");
    if (ops[0].kind == OP_IMM) {
      encode_sse_imm(insn->pre, false, insn->bytes[0], insn->ext, &ops[1], 1);
      sse_imm8(&ops[0]);
      return;
    }
    if (!insn->op)
      fail("expected a number as the count");
    encode_sse(insn->pre, false, insn->op, &ops[1], &ops[0]);
    return;
  case SSE_GP: {
    // [$imm,] %xmm, %r32 (or %r64, the same)
    Operand *src = &ops[n - 2], *dst = &ops[n - 1];
    if ((n != 2 && n != 3) || !is_gp(dst) || dst->reg->size < 4 || !is_xmm(src))
      fail("expected an XMM source and a 32- or 64-bit register destination");
    encode_sse_imm(insn->pre, false, insn->op, dst->reg->num, src, n == 3);
    if (n == 3)
      sse_imm8(&ops[0]);
    return;
  }
  case SSE_EXTR: {
    // $imm, %xmm, r/m: the xmm is in ModRM.reg
    if (n != 3 || !is_xmm(&ops[1]) || is_xmm(&ops[2]))
      fail("expected an immediate, an XMM source and a register or memory destination");
    encode_sse_imm(insn->pre, insn->size == 8, insn->op, ops[1].reg->num, &ops[2], 1);
    sse_imm8(&ops[0]);
    return;
  }
  case SSEMOV:
    if (n != 2)
      fail("expected 2 operands");
    if (is_xmm(&ops[1]) && insn->op)
      encode_sse(insn->pre, false, insn->op, &ops[1], &ops[0]); // load or reg-reg
    else if (is_xmm(&ops[0]) && ops[1].kind == OP_MEM && insn->ext)
      encode_sse(insn->pre, false, insn->ext, &ops[0], &ops[1]); // store
    else
      fail("unsupported operands");
    return;
  case MOVD:
    // A 64-bit register makes it movq, as GNU as has it.
    if (n != 2)
      fail("expected 2 operands");
    if (is_xmm(&ops[1]) && !is_xmm(&ops[0])) {
      bool w = is_gp(&ops[0]) && ops[0].reg->size == 8;
      encode_sse(0x66, w, 0x6e, &ops[1], &ops[0]);
      return;
    }
    if (is_xmm(&ops[0]) && !is_xmm(&ops[1])) {
      bool w = is_gp(&ops[1]) && ops[1].reg->size == 8;
      encode_sse(0x66, w, 0x7e, &ops[0], &ops[1]);
      return;
    }
    fail("unsupported operands");
  case CRC32: {
    // crc32 r/m, %r32 or %r64: the size of the source
    if (n != 2 || !is_gp(&ops[1]) || ops[1].reg->size < 4)
      fail("expected a 32- or 64-bit register destination");
    int size = insn->size ? insn->size : is_gp(&ops[0]) ? ops[0].reg->size : 0;
    if (!size)
      fail("can't tell the operand size");
    if (size == 2)
      out(0x66);
    encode_sse(0xf2, ops[1].reg->size == 8, size == 1 ? 0x38f0 : 0x38f1, &ops[1], &ops[0]);
    return;
  }
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
    encode_sse(insn->pre, size == 8, insn->op, &ops[1], &ops[0]);
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
    // The low 8 bytes, from xmm or memory (the rest zeroed), or to memory
    if (is_xmm(&ops[1])) {
      encode_sse(0xf3, false, 0x7e, &ops[1], &ops[0]);
      return;
    }
    if (is_xmm(&ops[0])) {
      encode_sse(0x66, false, 0xd6, &ops[0], &ops[1]);
      return;
    }
    encode_mov(&(Insn){"movq", MOV, 0, 8}, ops, n);
    return;
  case X87:
    // fnstsw also stores to %ax, with its own opcode.
    if (n == 1 && !strcmp(insn->name, "fnstsw") && is_gp(&ops[0]) &&
        ops[0].reg->num == 0 && ops[0].reg->size == 2) {
      out(0xdf);
      out(0xe0);
      return;
    }
    if (n != 1 || ops[0].kind != OP_MEM)
      fail("expected a memory operand");
    rex(insn->size == 8, 0, &ops[0], false); // REX.W for cmpxchg16b
    if (insn->op > 0xff)
      out(insn->op >> 8);
    out(insn->op & 0xff);
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
    out(size == 1 ? insn->op : insn->op + 1);
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

//---------- ARM64: operands -------------------------------------------------
//
// With --target=aarch64-linux (target_arm64), instructions are ARM64's,
// in GNU as's syntax: registers by name (x0, w0, sp, xzr, s0, d0, q0,
// v0.d[1]), immediates with or without '#', memory as [x0], [x0, 8],
// [x0, 8]! (pre-index), [x0], 8 (post-index) or [x0, x1, lsl 3], and
// relocation operators, as in :lo12:sym. Every instruction is 4 bytes.
// The rest of the assembler (sections, symbols, directives, layout and
// the ELF file) is shared.

static bool word_is(char *w, int len, char *name);
static int64_t read_int(void);

typedef enum {
  A_GP, A_FP, A_VEC, A_IMM, A_FLOAT, A_MEM, A_COND, A_SHIFT, A_SYSREG,
} AKind;

// Shifts and extends, as in `lsl 12` or `sxtw 2`. An extend's code (from
// UXTB) is the option field of the instructions that take one.
enum { LSL, LSR, ASR, ROR, UXTB, UXTH, UXTW, UXTX, SXTB, SXTH, SXTW, SXTX };

// The relocation operators, as in :lo12:sym, each with the relocation it
// makes in an add (or adrp, or a load and store, see a64_lo12())
typedef struct {
  char *name;
  int reloc;
} AMod;

static AMod a64_mods[] = {
  {"lo12", R_AARCH64_ADD_ABS_LO12_NC},
  {"got", R_AARCH64_ADR_GOT_PAGE},
  {"got_lo12", R_AARCH64_LD64_GOT_LO12_NC},
  {"tprel_hi12", R_AARCH64_TLSLE_ADD_TPREL_HI12},
  {"tprel_lo12", R_AARCH64_TLSLE_ADD_TPREL_LO12},
  {"tprel_lo12_nc", R_AARCH64_TLSLE_ADD_TPREL_LO12_NC},
  {"tlsdesc", R_AARCH64_TLSDESC_ADR_PAGE21},
  {"tlsdesc_lo12", R_AARCH64_TLSDESC_ADD_LO12},
  {"gottprel", R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21},
  {"gottprel_lo12", R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC},
  {"abs_g0", R_AARCH64_MOVW_UABS_G0}, {"abs_g0_nc", R_AARCH64_MOVW_UABS_G0_NC},
  {"abs_g1", R_AARCH64_MOVW_UABS_G1}, {"abs_g1_nc", R_AARCH64_MOVW_UABS_G1_NC},
  {"abs_g2", R_AARCH64_MOVW_UABS_G2}, {"abs_g2_nc", R_AARCH64_MOVW_UABS_G2_NC},
  {"abs_g3", R_AARCH64_MOVW_UABS_G3},
};

typedef struct {
  AKind kind;
  int reg;          // A_GP, A_FP, A_VEC; A_MEM: the base register
  int size;         // A_GP: 4 or 8; A_FP: 1 to 16 (b to q); A_VEC: an element's
  bool is_sp;       // A_GP 31 named sp or wsp, not the zero register
  int lanes;        // A_VEC: v0.4s has 4, v0.d[1] and plain v0 have 0
  int elem;         // A_VEC: v0.d[1] is element 1; -1 for none
  int64_t val;      // A_IMM, A_MEM's offset; A_COND, A_SYSREG: the code
  Sym *sym;         // A_IMM, A_MEM's offset: a symbol, plus val
  AMod *mod;        // its relocation operator, or NULL
  double fval;      // A_FLOAT
  int shift;        // A_SHIFT: LSL ... SXTX; A_MEM: the index's
  int amount;       // A_SHIFT, A_MEM's index: the amount, or -1 for none
  int index;        // A_MEM: the index register, or -1
  int index_size;   // A_MEM: 4 (w) or 8 (x)
  int mode;         // A_MEM: 0 offset, 1 pre-index ([x0, 8]!), 2 post-index
  char *text;       // where it starts in the statement
} AOp;

static char *a64_conds[] = {
  "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt",
  "le", "al", "nv", "cs", "cc",
};

// Condition `name`'s code, or -1. (cs and cc are hs and lo.)
static int a64_cond(char *name, int len) {
  for (int i = 0; i < 18; i++)
    if (word_is(name, len, a64_conds[i]))
      return i < 16 ? i : i - 14;
  return -1;
}

static char *a64_shifts[] = {
  "lsl", "lsr", "asr", "ror", "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth",
  "sxtw", "sxtx",
};

// The system registers mrs and msr take, by name: op0:op1:CRn:CRm:op2,
// as the instructions encode them (op0 less 2)
static struct {
  char *name;
  int code;
} a64_sysregs[] = {
  {"tpidr_el0", 0x5e82}, {"tpidrro_el0", 0x5e83}, {"fpcr", 0x5a20},
  {"fpsr", 0x5a21}, {"nzcv", 0x5a10}, {"ctr_el0", 0x5801}, {"dczid_el0", 0x5807},
  {"cntvct_el0", 0x5f02}, {"cntfrq_el0", 0x5f00}, {"midr_el1", 0x4000},
};

// Reads `name`, a word of letters, digits and '_', at p.
static int a64_word(char **name) {
  *name = p;
  while (isalnum((unsigned char)*p) || *p == '_')
    p++;
  return p - *name;
}

// Is the word at `s` a register, and which? Sets op's kind, register and
// size. Doesn't move p.
static bool a64_reg_name(char *s, int len, AOp *op) {
  char c = tolower((unsigned char)s[0]);
  char *end;
  long n = len > 1 && isdigit((unsigned char)s[1]) ? strtol(s + 1, &end, 10) : -1;
  bool numbered = n >= 0 && end == s + len && (s[1] != '0' || len == 2);

  if (numbered && (c == 'x' || c == 'w') && n <= 30) {
    *op = (AOp){A_GP, n, c == 'x' ? 8 : 4};
    return true;
  }
  if (numbered && strchr("bhsdq", c) && n <= 31) {
    *op = (AOp){A_FP, n, c == 'b' ? 1 : c == 'h' ? 2 : c == 's' ? 4 : c == 'd' ? 8 : 16};
    return true;
  }
  if (numbered && c == 'v' && n <= 31) {
    *op = (AOp){A_VEC, n, 0, .elem = -1};
    return true;
  }
  static struct { char *name; int reg, size; bool sp; } named[] = {
    {"sp", 31, 8, true}, {"wsp", 31, 4, true}, {"xzr", 31, 8}, {"wzr", 31, 4},
    {"lr", 30, 8}, {"fp", 29, 8}, {"ip0", 16, 8}, {"ip1", 17, 8},
  };
  for (int i = 0; i < sizeof(named) / sizeof(*named); i++) {
    if (word_is(s, len, named[i].name)) {
      *op = (AOp){A_GP, named[i].reg, named[i].size, named[i].sp};
      return true;
    }
  }
  return false;
}

// gp_reg_number() for ARM64: x0-x30 (or w0-w30) are 0-30, fp is x29,
// lr x30, and sp 31.
static int arm64_gp_reg_number(char *name) {
  AOp op;
  if (!a64_reg_name(name, strlen(name), &op) || op.kind != A_GP ||
      (op.reg == 31 && !op.is_sp))
    return -1;
  return op.reg;
}

// After v0: an arrangement, .4s, or an element, .s[1]. An element's size
// (and an arrangement's) goes in op->size, its count in op->lanes.
static void a64_vec_suffix(AOp *op) {
  if (*p != '.')
    return;
  p++;
  int lanes = 0;
  while (isdigit((unsigned char)*p))
    lanes = lanes * 10 + *p++ - '0';
  char t = tolower((unsigned char)*p++);
  op->size = t == 'b' ? 1 : t == 'h' ? 2 : t == 's' ? 4 : t == 'd' ? 8 : t == 'q' ? 16 : 0;
  if (!op->size)
    fail("bad vector arrangement");
  op->lanes = lanes;
  if (*p == '[') {
    p++;
    op->elem = strtol(p, &p, 10);
    if (*p++ != ']')
      fail("expected ']'");
  }
}

// An immediate or an address: a number, or a symbol plus one, maybe after
// a relocation operator, :lo12:sym.
static void a64_value(AOp *op) {
  skip_space();
  if (*p == '#')
    p++;
  op->kind = A_IMM;
  if (*p == ':') {
    char *name = ++p;
    while (*p && *p != ':')
      p++;
    if (!*p)
      fail("expected ':'");
    int len = p++ - name;
    for (int i = 0; i < sizeof(a64_mods) / sizeof(*a64_mods); i++)
      if (word_is(name, len, a64_mods[i].name))
        op->mod = &a64_mods[i];
    if (!op->mod)
      fail("unknown relocation operator ':%.*s:'", len, name);
  }

  // A floating-point number, for fmov and fcmp
  char *q = p + (*p == '-');
  while (isdigit((unsigned char)*q))
    q++;
  if (!op->mod && q > p && (*q == '.' || *q == 'e' || *q == 'E') &&
      isdigit((unsigned char)p[*p == '-'])) {
    op->kind = A_FLOAT;
    op->fval = strtod(p, &p);
    return;
  }

  Operand v = {0};
  read_value(&v);
  if (v.suffix != NO_SUFFIX || v.minus)
    fail("unsupported operand");
  op->val = v.val;
  op->sym = v.sym;
}

// [base], [base, offset], [base, index{, extend {amount}}], and a
// following '!'
static void a64_mem(AOp *op) {
  p++; // '['
  skip_space();
  char *name;
  int len = a64_word(&name);
  AOp base;
  if (!a64_reg_name(name, len, &base) || base.kind != A_GP || base.size != 8 ||
      (base.reg == 31 && !base.is_sp))
    fail("expected a base register");
  *op = (AOp){A_MEM, base.reg, .index = -1, .amount = -1};
  skip_space();

  if (*p == ',') {
    p++;
    skip_space();
    char *save = p;
    len = a64_word(&name);
    AOp index;
    if (len && a64_reg_name(name, len, &index) && index.kind == A_GP) {
      op->index = index.reg;
      op->index_size = index.size;
      op->shift = index.size == 8 ? LSL : UXTW;
      skip_space();
      if (*p == ',') {
        p++;
        skip_space();
        len = a64_word(&name);
        int k = 0;
        while (k < 12 && !word_is(name, len, a64_shifts[k]))
          k++;
        if (k == 12 || k == LSR || k == ASR || k == ROR)
          fail("bad index extend");
        op->shift = k;
        skip_space();
        if (*p && *p != ']') {
          if (*p == '#')
            p++;
          op->amount = read_int();
        }
      }
    } else {
      p = save;
      AOp off = {0};
      a64_value(&off);
      op->val = off.val;
      op->sym = off.sym;
      op->mod = off.mod;
    }
  }
  skip_space();
  if (*p++ != ']')
    fail("expected ']'");
  if (*p == '!') {
    p++;
    op->mode = 1;
  }
}

static void a64_operand2(AOp *op) {
  *op = (AOp){0};
  op->elem = -1;
  if (*p == '[') {
    a64_mem(op);
    return;
  }
  if (*p == '{')
    fail("register lists are not supported");

  char *save = p;
  char *name;
  int len = a64_word(&name);
  bool whole = len && !is_symchar(*p);

  if (len && (whole || *p == '.') && a64_reg_name(name, len, op)) {
    if (op->kind == A_VEC)
      a64_vec_suffix(op);
    else if (!whole)
      fail("bad register");
    return;
  }
  if (whole) {
    int k = a64_cond(name, len);
    if (k >= 0) {
      *op = (AOp){A_COND, .val = k};
      return;
    }
    for (k = 0; k < 12; k++) {
      if (word_is(name, len, a64_shifts[k])) {
        *op = (AOp){A_SHIFT, .shift = k, .amount = -1};
        skip_space();
        if (*p && *p != ',') {
          if (*p == '#')
            p++;
          op->amount = read_int();
        }
        return;
      }
    }
    for (k = 0; k < sizeof(a64_sysregs) / sizeof(*a64_sysregs); k++) {
      if (word_is(name, len, a64_sysregs[k].name)) {
        *op = (AOp){A_SYSREG, .val = a64_sysregs[k].code};
        return;
      }
    }
  }
  p = save;
  a64_value(op);
}

static void a64_operand(AOp *op) {
  skip_space();
  char *text = p;
  a64_operand2(op);
  op->text = text;
}

// Reads operand `op` again as a label, where an instruction takes one: a
// symbol may be named like a register or a condition (v3, hi), as GNU as
// lets it be.
static void a64_label(AOp *op) {
  if (op->kind == A_IMM || op->kind == A_MEM)
    return;
  char *save = p;
  char *text = op->text;
  *op = (AOp){0};
  p = text;
  a64_value(op);
  op->text = text;
  p = save;
}

// Splits the operands at commas outside brackets.
static int a64_operands(AOp *ops, int max) {
  int n = 0;
  skip_space();
  while (*p) {
    if (n == max)
      fail("too many operands");
    a64_operand(&ops[n++]);
    skip_space();
    if (*p == ',')
      p++;
    else if (*p)
      fail("unexpected '%c'", *p);
  }
  return n;
}

//---------- ARM64: instructions ---------------------------------------------

// Encodes `val` as a logical instruction's immediate, N:immr:imms in 13
// bits, for a `size`-byte operation, or returns -1 if it isn't one: a run
// of ones, rotated, repeated in elements of 2, 4, ... 64 bits.
static int64_t a64_bitmask(uint64_t val, int size) {
  if (size == 4)
    val = (val & 0xffffffff) | val << 32;
  if (val == 0 || val == ~0ULL)
    return -1;

  int e = 64, lg = 6;
  while (e > 2) {
    int h = e / 2;
    uint64_t m = (1ULL << h) - 1;
    if (((val >> h) & m) != (val & m))
      break;
    e = h;
    lg--;
  }
  uint64_t emask = e == 64 ? ~0ULL : (1ULL << e) - 1;
  uint64_t elem = val & emask;
  int ones = 0;
  for (int i = 0; i < e; i++)
    ones += (elem >> i) & 1;

  // The rotation that brings the ones down to the bottom
  for (int r = 0; r < e; r++) {
    uint64_t rot = r ? ((elem >> r) | (elem << (e - r))) & emask : elem;
    if (rot != (1ULL << ones) - 1)
      continue;
    int immr = (e - r) % e;
    int imms = ((0x3f << (lg + 1)) & 0x3f) | (ones - 1);
    return (int64_t)(e == 64) << 12 | immr << 6 | imms;
  }
  return -1;
}

// Puts the bits of a value into an instruction's (or data's) field, as
// relocation `type` says: `val` is the result of the relocation's
// calculation (S + A - P, Page(S + A) - Page(P), ...). Returns false if
// it doesn't fit. The linker uses this too.
bool arm64_put_field(unsigned char *loc, int type, int64_t val) {
  uint32_t insn;
  memcpy(&insn, loc, 4);
  int64_t lo12 = val & 0xfff;
  int64_t field;
  int shift = 0;

  switch (type) {
  case R_AARCH64_NONE:
  case R_AARCH64_TLSDESC_CALL:
    return true;
  case R_AARCH64_ABS64:
  case R_AARCH64_PREL64:
    memcpy(loc, &val, 8);
    return true;
  case R_AARCH64_ABS32:
  case R_AARCH64_PREL32: {
    uint32_t v = val;
    memcpy(loc, &v, 4);
    return val >= INT32_MIN && val <= (type == R_AARCH64_ABS32 ? UINT32_MAX : INT32_MAX);
  }
  case R_AARCH64_ABS16:
  case R_AARCH64_PREL16: {
    uint16_t v = val;
    memcpy(loc, &v, 2);
    return val >= INT16_MIN && val <= UINT16_MAX;
  }
  case R_AARCH64_CALL26:
  case R_AARCH64_JUMP26:
    insn |= (val >> 2) & 0x3ffffff;
    memcpy(loc, &insn, 4);
    return !(val & 3) && val >= -(1L << 27) && val < (1L << 27);
  case R_AARCH64_CONDBR19:
  case R_AARCH64_LD_PREL_LO19:
    insn |= ((val >> 2) & 0x7ffff) << 5;
    memcpy(loc, &insn, 4);
    return !(val & 3) && val >= -(1L << 20) && val < (1L << 20);
  case R_AARCH64_TSTBR14:
    insn |= ((val >> 2) & 0x3fff) << 5;
    memcpy(loc, &insn, 4);
    return !(val & 3) && val >= -(1L << 15) && val < (1L << 15);
  case R_AARCH64_ADR_PREL_LO21:
  case R_AARCH64_ADR_PREL_PG_HI21:
  case R_AARCH64_ADR_PREL_PG_HI21_NC:
  case R_AARCH64_ADR_GOT_PAGE:
  case R_AARCH64_TLSDESC_ADR_PAGE21:
  case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21: {
    int64_t imm = type == R_AARCH64_ADR_PREL_LO21 ? val : val >> 12;
    insn |= (imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5;
    memcpy(loc, &insn, 4);
    return type == R_AARCH64_ADR_PREL_PG_HI21_NC || (imm >= -(1L << 20) && imm < (1L << 20));
  }
  case R_AARCH64_ADD_ABS_LO12_NC:
  case R_AARCH64_LDST8_ABS_LO12_NC:
  case R_AARCH64_TLSDESC_ADD_LO12:
  case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
    field = lo12;
    break;
  case R_AARCH64_TLSLE_ADD_TPREL_LO12:
    field = val;
    if (val < 0 || val > 0xfff)
      return false;
    break;
  case R_AARCH64_TLSLE_ADD_TPREL_HI12:
    field = (val >> 12) & 0xfff;
    if (val < 0 || val >= (1 << 24))
      return false;
    break;
  case R_AARCH64_LDST16_ABS_LO12_NC: field = lo12 >> 1; shift = 1; break;
  case R_AARCH64_LDST32_ABS_LO12_NC: field = lo12 >> 2; shift = 2; break;
  case R_AARCH64_LDST64_ABS_LO12_NC:
  case R_AARCH64_LD64_GOT_LO12_NC:
  case R_AARCH64_TLSDESC_LD64_LO12:
  case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    field = lo12 >> 3;
    shift = 3;
    break;
  case R_AARCH64_LDST128_ABS_LO12_NC: field = lo12 >> 4; shift = 4; break;
  case R_AARCH64_MOVW_UABS_G0:
  case R_AARCH64_MOVW_UABS_G0_NC:
  case R_AARCH64_MOVW_UABS_G1:
  case R_AARCH64_MOVW_UABS_G1_NC:
  case R_AARCH64_MOVW_UABS_G2:
  case R_AARCH64_MOVW_UABS_G2_NC:
  case R_AARCH64_MOVW_UABS_G3: {
    static int groups[] = {0, 0, 1, 1, 2, 2, 3};
    int g = groups[type - R_AARCH64_MOVW_UABS_G0];
    insn |= ((val >> (16 * g)) & 0xffff) << 5;
    memcpy(loc, &insn, 4);
    return true;
  }
  default:
    return false;
  }

  // A 12-bit immediate at bit 10, of an add or a load or store, which
  // scales it by the access's size: the low bits must be 0.
  insn |= field << 10;
  memcpy(loc, &insn, 4);
  return !(lo12 & ((1 << shift) - 1));
}

static void a64_out(uint32_t insn) {
  cur->align = MAX(cur->align, 4);
  out_n(4, insn);
}

// A hole in the instruction about to be written (see a64_out()), which
// fill_hole() fills or makes a relocation of.
static void a64_hole(int type, Sym *sym, int64_t addend) {
  cur->holes = grow(cur->holes, cur->nholes, &cur->capholes, sizeof(Hole));
  cur->holes[cur->nholes++] = (Hole){cur->bytes.len, cur->njumps, type, sym, addend};
  sym->is_used = true;
  // A symbol in a TLS relocation is thread-local (STT_TLS), as GNU as
  // marks it, if only declared here.
  if (type >= R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 && type <= R_AARCH64_TLSDESC_CALL)
    sym->is_tls = true;
}

static bool is_x(AOp *op) {
  return op->kind == A_GP && op->size == 8;
}

// Register operand `op` of a general register, sp only if `sp` (31 is
// the zero register otherwise)
static int gpreg(AOp *op, bool sp) {
  if (op->kind != A_GP)
    fail("expected a general register");
  if (op->reg == 31 && op->is_sp != sp)
    fail(sp ? "the zero register can't be used here" : "sp can't be used here");
  return op->reg;
}

// A constant operand, in [lo, hi]
static int64_t imm(AOp *op, int64_t lo, int64_t hi) {
  if (op->kind != A_IMM || op->sym || op->mod)
    fail("expected a number");
  if (op->val < lo || op->val > hi)
    fail("immediate out of range");
  return op->val;
}

// The size bit, sf, of a general register operand: bit 31
static uint32_t sf(AOp *op) {
  return op->size == 8 ? 1u << 31 : 0;
}

static void same_size(AOp *a, AOp *b) {
  if (a->size != b->size)
    fail("operands of different sizes");
}

// The FP type field (bits 22-23) of a scalar operand: s, d or h
static uint32_t ftype(AOp *op) {
  if (op->kind != A_FP)
    fail("expected an FP register");
  switch (op->size) {
  case 4: return 0;
  case 8: return 1 << 22;
  case 2: return 3 << 22;
  }
  fail("expected an s, d or h register");
}

// A shifted-register operand's shift: `ops[i]`, if there is one, as the
// shift (bits 22-23) and amount (bits 10-15).
static uint32_t opt_shift(AOp *ops, int n, int i, int size, bool ror) {
  if (i >= n)
    return 0;
  if (ops[i].kind != A_SHIFT || ops[i].shift > (ror ? ROR : ASR) || ops[i].amount < 0 ||
      ops[i].amount >= size * 8)
    fail("bad shift");
  return ops[i].shift << 22 | ops[i].amount << 10;
}

// add, adds, sub, subs, and cmp, cmn, neg, negs: `op` is 0 for add, 2
// for sub, plus 1 to set the flags. A negative immediate turns add into
// sub and back, as GNU as does.
static void a64_addsub(int op, AOp *d, AOp *n, AOp *ops, int nops) {
  AOp *m = &ops[0];
  bool flags = op & 1;
  uint32_t base = sf(d) | (op >> 1) << 30 | flags << 29;

  if (m->kind == A_IMM) {
    same_size(d, n);
    int64_t v = m->val;
    if (m->mod || m->sym) {
      if (!m->mod)
        fail("a symbol needs a relocation operator, as in :lo12:");
      a64_hole(m->mod->reloc, m->sym, v);
      v = 0;
    } else if (v < 0) {
      v = -v;
      base ^= 1 << 30;
    }
    uint32_t sh = 0;
    if (nops > 1) {
      if (ops[1].kind != A_SHIFT || ops[1].shift != LSL ||
          (ops[1].amount != 0 && ops[1].amount != 12))
        fail("bad shift");
      sh = ops[1].amount == 12;
    } else if (v > 0xfff && !(v & 0xfff)) {
      v >>= 12;
      sh = 1;
    }
    if (v > 0xfff)
      fail("immediate out of range");
    a64_out(base | 0x11000000 | sh << 22 | v << 10 | gpreg(n, true) << 5 |
            gpreg(d, !flags));
    return;
  }

  // The extended register form takes sp, and an extend; the shifted
  // register form takes the zero register, and a shift.
  bool extended = (d->reg == 31 && d->is_sp) || (n->reg == 31 && n->is_sp) ||
                  (nops > 1 && ops[1].kind == A_SHIFT && ops[1].shift >= UXTB);
  if (extended) {
    int opt = d->size == 8 ? UXTX : UXTW, amount = 0;
    if (nops > 1) {
      if (ops[1].kind != A_SHIFT)
        fail("expected an extend");
      opt = ops[1].shift == LSL ? opt : ops[1].shift;
      amount = MAX(ops[1].amount, 0);
      if (opt < UXTB || amount > 4)
        fail("bad extend");
    }
    a64_out(base | 0x0b200000 | gpreg(m, false) << 16 | (opt - UXTB) << 13 | amount << 10 |
            gpreg(n, true) << 5 | gpreg(d, !flags));
    return;
  }
  same_size(d, m);
  a64_out(base | 0x0b000000 | opt_shift(ops, nops, 1, d->size, false) |
          gpreg(m, false) << 16 | gpreg(n, false) << 5 | gpreg(d, false));
}

// and, orr, eor, ands (opc 0-3), and bic, orn, eon, bics (which invert
// the second operand). With an immediate, it's a bitmask.
static void a64_logic(int opc, bool invert, AOp *d, AOp *n, AOp *ops, int nops) {
  AOp *m = &ops[0];
  same_size(d, n);
  if (m->kind == A_IMM) {
    uint64_t v = imm(m, INT64_MIN, INT64_MAX);
    if (invert)
      v = ~v;
    int64_t bits = a64_bitmask(v, d->size);
    if (bits < 0)
      fail("immediate can't be encoded");
    a64_out(sf(d) | opc << 29 | 0x12000000 | bits << 10 | gpreg(n, false) << 5 |
            gpreg(d, opc != 3));
    return;
  }
  same_size(d, m);
  a64_out(sf(d) | opc << 29 | 0x0a000000 | invert << 21 | opt_shift(ops, nops, 1, d->size, true) |
          gpreg(m, false) << 16 | gpreg(n, false) << 5 | gpreg(d, false));
}

// mov: a register to another (orr, or an add for sp), or a number (movz,
// movn, or a bitmask's orr).
static void a64_mov(AOp *d, AOp *s) {
  if (s->kind == A_GP) {
    same_size(d, s);
    if ((d->reg == 31 && d->is_sp) || (s->reg == 31 && s->is_sp))
      a64_out(sf(d) | 0x11000000 | gpreg(s, true) << 5 | gpreg(d, true));
    else
      a64_out(sf(d) | 0x2a0003e0 | gpreg(s, false) << 16 | gpreg(d, false));
    return;
  }
  if (s->kind != A_IMM || s->mod || s->sym)
    fail("unsupported operands");

  int bits = d->size * 8;
  uint64_t v = d->size == 4 ? (uint32_t)s->val : (uint64_t)s->val;
  uint64_t inv = d->size == 4 ? (uint32_t)~v : ~v;
  for (int hw = 0; hw < bits / 16; hw++) {
    if (!(v & ~(0xffffULL << (hw * 16)))) {
      a64_out(sf(d) | 0x52800000 | hw << 21 | ((v >> (hw * 16)) & 0xffff) << 5 | gpreg(d, false));
      return;
    }
  }
  for (int hw = 0; hw < bits / 16; hw++) {
    if (!(inv & ~(0xffffULL << (hw * 16)))) {
      a64_out(sf(d) | 0x12800000 | hw << 21 | ((inv >> (hw * 16)) & 0xffff) << 5 |
              gpreg(d, false));
      return;
    }
  }
  int64_t mask = a64_bitmask(v, d->size);
  if (mask < 0)
    fail("immediate can't be encoded");
  a64_out(sf(d) | 0x320003e0 | mask << 10 | gpreg(d, true));
}

// movz, movn, movk: `op` 0, 2 or 3 (as opc), with `lsl N`
static void a64_movw(int op, AOp *ops, int n) {
  AOp *d = &ops[0];
  int hw = 0;
  if (n > 2) {
    if (ops[2].kind != A_SHIFT || ops[2].shift != LSL || ops[2].amount % 16 ||
        ops[2].amount >= d->size * 8)
      fail("bad shift");
    hw = ops[2].amount / 16;
  }
  int64_t v = 0;
  if (ops[1].mod) {
    // :abs_g1: and the like name their 16 bits, as hw.
    int type = ops[1].mod->reloc;
    if (type >= R_AARCH64_MOVW_UABS_G0 && type <= R_AARCH64_MOVW_UABS_G3)
      hw = (type - R_AARCH64_MOVW_UABS_G0) / 2; // G0, G0_NC, G1, G1_NC, ...
    a64_hole(type, ops[1].sym, ops[1].val);
  } else {
    v = imm(&ops[1], 0, 0xffff);
  }
  a64_out(sf(d) | op << 29 | 0x12800000 | hw << 21 | v << 5 | gpreg(d, false));
}

// sbfm, bfm, ubfm (opc 0-2): d, n, immr, imms
static void a64_bfm(int opc, AOp *d, AOp *n, int immr, int imms) {
  int bits = d->size * 8;
  if (immr < 0 || immr >= bits || imms < 0 || imms >= bits)
    fail("bad bit-field");
  uint32_t s = sf(d);
  a64_out(s | opc << 29 | 0x13000000 | (s ? 1 << 22 : 0) | immr << 16 | imms << 10 |
          gpreg(n, false) << 5 | gpreg(d, false));
}

// A load or store of the register `t` (whose kind and size, with `insn`'s
// signed and unsigned variants, give its size and opc fields) at memory
// operand `m`, with a post-index `post` if there is one.
static void a64_ldst(char *name, AOp *t, AOp *m, AOp *post, bool unscaled) {
  if (m->kind != A_MEM)
    fail("expected a memory operand");

  // size: log2 of the bytes accessed; opc: 0 store, 1 load, 2 load
  // sign-extending to 64 bits, 3 to 32; V: an FP/SIMD register
  bool load = name[0] == 'l';
  char *rest = name + (unscaled ? 4 : 3); // after ldr/str or ldur/stur
  bool sign = *rest == 's';
  if (sign)
    rest++;
  int size, opc = load, v = 0;
  if (*rest == 'b' || *rest == 'h' || *rest == 'w') {
    size = *rest == 'b' ? 0 : *rest == 'h' ? 1 : 2;
    if (rest[1])
      fail("unknown instruction '%s'", name);
    if (sign && (!load || (size == 2 && !is_x(t))))
      fail("bad operands");
    if (t->kind != A_GP || (!sign && t->size != 4))
      fail("expected a w register");
    if (sign)
      opc = is_x(t) ? 2 : 3;
  } else if (*rest || sign) {
    fail("unknown instruction '%s'", name);
  } else if (t->kind == A_GP) {
    size = t->size == 8 ? 3 : 2;
  } else if (t->kind == A_FP) {
    v = 1;
    size = t->size == 1 ? 0 : t->size == 2 ? 1 : t->size == 4 ? 2 : t->size == 8 ? 3 : 0;
    if (t->size == 16)
      opc |= 2;
  } else {
    fail("bad operands");
  }
  int bytes = t->kind == A_FP ? t->size : 1 << size;
  uint32_t base = (uint32_t)size << 30 | 0x38000000 | v << 26 | opc << 22 |
                  (t->kind == A_GP ? gpreg(t, false) : t->reg) | m->reg << 5;

  // [base, index{, extend amount}]: the index shifted by 0, or by the
  // access's size (S), as lsl (uxtx) for an x, uxtw or sxtw for a w
  if (m->index >= 0) {
    int lg = __builtin_ctz(bytes);
    if (m->amount > 0 && m->amount != lg)
      fail("bad index shift");
    if (unscaled || post || m->mode)
      fail("bad operands");
    int opt = m->shift == LSL ? UXTX : m->shift;
    if ((m->index_size == 8) != (opt == UXTX || opt == SXTX))
      fail("bad index extend");
    bool s = m->amount > 0 || (m->amount == 0 && bytes == 1);
    a64_out(base | 0x00200800 | m->index << 16 | (opt - UXTB) << 13 | s << 12);
    return;
  }

  int64_t off = m->val;
  if (post) {
    if (m->sym || m->mod || m->val || m->mode)
      fail("bad post-index");
    off = imm(post, -256, 255);
    a64_out(base | (off & 0x1ff) << 12 | 1 << 10);
    return;
  }
  if (m->mode) {
    if (m->sym || m->mod || off < -256 || off > 255)
      fail("bad pre-index");
    a64_out(base | (off & 0x1ff) << 12 | 3 << 10);
    return;
  }
  if (m->mod || m->sym) {
    if (!m->mod)
      fail("a symbol needs a relocation operator, as in :lo12:");
    int type = m->mod->reloc;
    if (type == R_AARCH64_ADD_ABS_LO12_NC) {
      static int lo12[] = {R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC,
                           R_AARCH64_LDST32_ABS_LO12_NC, R_AARCH64_LDST64_ABS_LO12_NC,
                           R_AARCH64_LDST128_ABS_LO12_NC};
      type = lo12[__builtin_ctz(bytes)];
    } else if (type == R_AARCH64_TLSDESC_ADD_LO12) {
      type = R_AARCH64_TLSDESC_LD64_LO12;
    }
    a64_hole(type, m->sym, off);
    a64_out(base | 0x01000000);
    return;
  }

  // Scaled and unsigned, if it can be; else unscaled (ldur), as GNU as
  // picks it.
  if (!unscaled && off >= 0 && !(off % bytes) && off / bytes <= 0xfff) {
    a64_out(base | 0x01000000 | (off / bytes) << 10);
    return;
  }
  if (off < -256 || off > 255)
    fail("offset out of range");
  a64_out(base | (off & 0x1ff) << 12);
}

// ldp, stp, ldpsw: two registers at [base, offset], pre- or post-indexed
static void a64_ldstp(char *name, AOp *ops, int n) {
  AOp *t1 = &ops[0], *t2 = &ops[1], *m = &ops[2];
  if (n < 3 || m->kind != A_MEM || m->index >= 0 || m->sym || m->mod || t1->kind != t2->kind)
    fail("bad operands");
  same_size(t1, t2);
  bool load = name[0] == 'l';
  bool sw = !strcmp(name, "ldpsw");
  int opc, v = t1->kind == A_FP, bytes = t1->size;
  if (v)
    opc = bytes == 4 ? 0 : bytes == 8 ? 1 : bytes == 16 ? 2 : -1;
  else
    opc = sw ? 1 : bytes == 8 ? 2 : 0;
  if (opc < 0 || (sw && t1->size != 8))
    fail("bad operands");
  if (sw)
    bytes = 4;

  int mode = 2; // signed offset
  int64_t off = m->val;
  if (n > 3) {
    if (m->val || m->mode)
      fail("bad post-index");
    off = imm(&ops[3], INT64_MIN, INT64_MAX);
    mode = 1;
  } else if (m->mode) {
    mode = 3;
  }
  if (off % bytes || off / bytes < -64 || off / bytes > 63)
    fail("offset out of range");
  int r1 = v ? t1->reg : gpreg(t1, false), r2 = v ? t2->reg : gpreg(t2, false);
  a64_out((uint32_t)opc << 30 | 0x28000000 | v << 26 | mode << 23 | load << 22 |
          ((off / bytes) & 0x7f) << 15 | r2 << 10 | m->reg << 5 | r1);
}

// The load-exclusive and store-exclusive family, and load-acquire and
// store-release: ldxr, ldaxr, stxr, stlxr, ldar, stlr (with b and h for
// bytes and halves), ldxp, ldaxp, stxp, stlxp.
static void a64_excl(char *name, AOp *ops, int n) {
  bool load = name[0] == 'l';
  bool acq_rel = strchr(name + 2, 'a') || name[2] == 'l'; // ldaxr, stlxr, ldar, stlr
  bool excl = strchr(name, 'x') != NULL;
  bool pair = excl && name[strlen(name) - 1] == 'p';
  char last = name[strlen(name) - 1];
  int status = !load && excl; // stxr's first operand is the status
  if (n != 1 + status + pair + 1)
    fail("bad operands");
  AOp *t = &ops[status], *m = &ops[n - 1];
  if (m->kind != A_MEM || m->index >= 0 || m->val || m->mode || m->sym)
    fail("expected [register]");
  int size = last == 'b' ? 0 : last == 'h' ? 1 : t->size == 8 ? 3 : 2;
  if ((last == 'b' || last == 'h') && t->size != 4)
    fail("expected a w register");
  uint32_t insn = (uint32_t)(pair ? (2 | (t->size == 8)) : size) << 30 | 0x08000000 | !excl << 23 |
                  load << 22 | pair << 21 | acq_rel << 15 | m->reg << 5 | gpreg(t, false);
  insn |= (status ? gpreg(&ops[0], false) : 31) << 16;
  insn |= (pair ? gpreg(&ops[status + 1], false) : 31) << 10;
  a64_out(insn);
}

// A branch to a label: b.cond, cbz, cbnz, tbz and tbnz may be too short
// to reach it, so they are jumps, which layout() makes long if they
// must be (see a64_place_jump()); b and bl reach 128 MiB, as far as the
// linker can put things.
static void a64_branch(uint32_t insn, int type, AOp *target) {
  a64_label(target);
  if (target->kind != A_IMM || !target->sym || target->mod)
    fail("expected a label");
  target->sym->is_used = true;
  if (type == R_AARCH64_JUMP26 || type == R_AARCH64_CALL26) {
    a64_hole(type, target->sym, target->val);
    a64_out(insn);
    return;
  }
  if (target->val)
    fail("a conditional branch can't take an offset from a label");
  cur->align = MAX(cur->align, 4);
  cur->jumps = grow(cur->jumps, cur->njumps, &cur->capjumps, sizeof(Jump));
  cur->jumps[cur->njumps++] = (Jump){cur->bytes.len, type, target->sym, .insn = insn};
}

// An FP data-processing instruction on registers of one size: fadd d0,
// d1, d2, fneg s0, s1 and so on
static void a64_fp(uint32_t op, AOp *ops, int n, int nregs) {
  if (n != nregs)
    fail("bad operands");
  uint32_t t = ftype(&ops[0]);
  for (int i = 1; i < n; i++)
    same_size(&ops[0], &ops[i]);
  uint32_t insn = op | t | ops[0].reg;
  if (nregs >= 2)
    insn |= ops[1].reg << 5;
  if (nregs >= 3)
    insn |= ops[2].reg << 16;
  if (nregs == 4)
    insn |= ops[3].reg << 10;
  a64_out(insn);
}

// The ARM64 instructions, by mnemonic: a kind, which says how its
// operands are read and encoded, and an opcode for it
typedef enum {
  I_ADDSUB, I_CMP, I_NEG, I_LOGIC, I_TST, I_MOV, I_MVN, I_MOVW, I_SHIFT, I_BFM,
  I_BFX, I_BFIZ, I_EXT, I_EXTR, I_DP1, I_DP2, I_DP3, I_MUL, I_MULH, I_ADC, I_NGC,
  I_CSEL, I_CSET, I_CINC, I_CCMP, I_LDST, I_LDSTU, I_LDSTP, I_EXCL, I_B, I_BCOND,
  I_CBZ, I_TBZ, I_BR, I_RET, I_ADR, I_EXCEPT, I_FIXED, I_BARRIER, I_MRS, I_MSR,
  I_FMOV, I_FP1, I_FP2, I_FP3, I_FCMP, I_FCVT, I_FCVTI, I_ICVTF, I_FCSEL, I_INS,
  I_UMOV,
} AInsnKind;

typedef struct {
  char *name;
  AInsnKind kind;
  uint32_t op;
} AInsn;

static AInsn a64_insns[] = {
  {"add", I_ADDSUB, 0}, {"adds", I_ADDSUB, 1}, {"sub", I_ADDSUB, 2}, {"subs", I_ADDSUB, 3},
  {"cmn", I_CMP, 1}, {"cmp", I_CMP, 3}, {"neg", I_NEG, 2}, {"negs", I_NEG, 3},
  {"and", I_LOGIC, 0}, {"orr", I_LOGIC, 1}, {"eor", I_LOGIC, 2}, {"ands", I_LOGIC, 3},
  {"bic", I_LOGIC, 4}, {"orn", I_LOGIC, 5}, {"eon", I_LOGIC, 6}, {"bics", I_LOGIC, 7},
  {"tst", I_TST, 3}, {"mov", I_MOV}, {"mvn", I_MVN},
  {"movn", I_MOVW, 0}, {"movz", I_MOVW, 2}, {"movk", I_MOVW, 3},
  {"lsl", I_SHIFT, LSL}, {"lsr", I_SHIFT, LSR}, {"asr", I_SHIFT, ASR}, {"ror", I_SHIFT, ROR},
  {"sbfm", I_BFM, 0}, {"bfm", I_BFM, 1}, {"ubfm", I_BFM, 2},
  {"sbfx", I_BFX, 0}, {"bfxil", I_BFX, 1}, {"ubfx", I_BFX, 2},
  {"sbfiz", I_BFIZ, 0}, {"bfi", I_BFIZ, 1}, {"ubfiz", I_BFIZ, 2},
  {"sxtb", I_EXT, SXTB}, {"sxth", I_EXT, SXTH}, {"sxtw", I_EXT, SXTW},
  {"uxtb", I_EXT, UXTB}, {"uxth", I_EXT, UXTH}, {"uxtw", I_EXT, UXTW},
  {"extr", I_EXTR},
  {"rbit", I_DP1, 0}, {"rev16", I_DP1, 1}, {"rev32", I_DP1, 2}, {"rev", I_DP1, 3},
  {"clz", I_DP1, 4}, {"cls", I_DP1, 5},
  {"udiv", I_DP2, 2}, {"sdiv", I_DP2, 3}, {"lslv", I_DP2, 8}, {"lsrv", I_DP2, 9},
  {"asrv", I_DP2, 10}, {"rorv", I_DP2, 11},
  {"madd", I_DP3, 0x1b000000}, {"msub", I_DP3, 0x1b008000},
  {"smaddl", I_DP3, 0x9b200000}, {"smsubl", I_DP3, 0x9b208000},
  {"umaddl", I_DP3, 0x9ba00000}, {"umsubl", I_DP3, 0x9ba08000},
  {"mul", I_MUL, 0x1b000000}, {"mneg", I_MUL, 0x1b008000},
  {"smull", I_MUL, 0x9b200000}, {"umull", I_MUL, 0x9ba00000},
  {"smulh", I_MULH, 0x9b407c00}, {"umulh", I_MULH, 0x9bc07c00},
  {"adc", I_ADC, 0}, {"adcs", I_ADC, 1}, {"sbc", I_ADC, 2}, {"sbcs", I_ADC, 3},
  {"ngc", I_NGC, 2}, {"ngcs", I_NGC, 3},
  {"csel", I_CSEL, 0}, {"csinc", I_CSEL, 1}, {"csinv", I_CSEL, 2}, {"csneg", I_CSEL, 3},
  {"cset", I_CSET, 1}, {"csetm", I_CSET, 2},
  {"cinc", I_CINC, 1}, {"cinv", I_CINC, 2}, {"cneg", I_CINC, 3},
  {"ccmn", I_CCMP, 0}, {"ccmp", I_CCMP, 1},
  {"ldr", I_LDST}, {"str", I_LDST}, {"ldrb", I_LDST}, {"strb", I_LDST},
  {"ldrh", I_LDST}, {"strh", I_LDST}, {"ldrsb", I_LDST}, {"ldrsh", I_LDST},
  {"ldrsw", I_LDST},
  {"ldur", I_LDSTU}, {"stur", I_LDSTU}, {"ldurb", I_LDSTU}, {"sturb", I_LDSTU},
  {"ldurh", I_LDSTU}, {"sturh", I_LDSTU}, {"ldursb", I_LDSTU}, {"ldursh", I_LDSTU},
  {"ldursw", I_LDSTU},
  {"ldp", I_LDSTP}, {"stp", I_LDSTP}, {"ldpsw", I_LDSTP},
  {"ldxr", I_EXCL}, {"ldxrb", I_EXCL}, {"ldxrh", I_EXCL}, {"ldaxr", I_EXCL},
  {"ldaxrb", I_EXCL}, {"ldaxrh", I_EXCL}, {"stxr", I_EXCL}, {"stxrb", I_EXCL},
  {"stxrh", I_EXCL}, {"stlxr", I_EXCL}, {"stlxrb", I_EXCL}, {"stlxrh", I_EXCL},
  {"ldar", I_EXCL}, {"ldarb", I_EXCL}, {"ldarh", I_EXCL}, {"stlr", I_EXCL},
  {"stlrb", I_EXCL}, {"stlrh", I_EXCL}, {"ldxp", I_EXCL}, {"ldaxp", I_EXCL},
  {"stxp", I_EXCL}, {"stlxp", I_EXCL},
  {"b", I_B, 0x14000000}, {"bl", I_B, 0x94000000},
  {"cbz", I_CBZ, 0x34000000}, {"cbnz", I_CBZ, 0x35000000},
  {"tbz", I_TBZ, 0x36000000}, {"tbnz", I_TBZ, 0x37000000},
  {"br", I_BR, 0xd61f0000}, {"blr", I_BR, 0xd63f0000}, {"ret", I_RET, 0xd65f0000},
  {"adr", I_ADR, 0x10000000}, {"adrp", I_ADR, 0x90000000},
  {"svc", I_EXCEPT, 0xd4000001}, {"hvc", I_EXCEPT, 0xd4000002},
  {"brk", I_EXCEPT, 0xd4200000}, {"hlt", I_EXCEPT, 0xd4400000},
  {"nop", I_FIXED, 0xd503201f}, {"yield", I_FIXED, 0xd503203f},
  {"wfe", I_FIXED, 0xd503205f}, {"wfi", I_FIXED, 0xd503207f},
  {"sev", I_FIXED, 0xd503209f}, {"sevl", I_FIXED, 0xd50320bf},
  {"clrex", I_FIXED, 0xd5033f5f},
  {"dmb", I_BARRIER, 0xd50330bf}, {"dsb", I_BARRIER, 0xd503309f},
  {"isb", I_BARRIER, 0xd50330df},
  {"mrs", I_MRS, 0xd5300000}, {"msr", I_MSR, 0xd5100000},
  {"fmov", I_FMOV},
  {"fabs", I_FP1, 0x1e20c000}, {"fneg", I_FP1, 0x1e214000}, {"fsqrt", I_FP1, 0x1e21c000},
  {"frintn", I_FP1, 0x1e244000}, {"frintp", I_FP1, 0x1e24c000},
  {"frintm", I_FP1, 0x1e254000}, {"frintz", I_FP1, 0x1e25c000},
  {"frinta", I_FP1, 0x1e264000}, {"frintx", I_FP1, 0x1e274000},
  {"frinti", I_FP1, 0x1e27c000},
  {"fmul", I_FP2, 0x1e200800}, {"fdiv", I_FP2, 0x1e201800}, {"fadd", I_FP2, 0x1e202800},
  {"fsub", I_FP2, 0x1e203800}, {"fmax", I_FP2, 0x1e204800}, {"fmin", I_FP2, 0x1e205800},
  {"fmaxnm", I_FP2, 0x1e206800}, {"fminnm", I_FP2, 0x1e207800},
  {"fnmul", I_FP2, 0x1e208800},
  {"fmadd", I_FP3, 0x1f000000}, {"fmsub", I_FP3, 0x1f008000},
  {"fnmadd", I_FP3, 0x1f200000}, {"fnmsub", I_FP3, 0x1f208000},
  {"fcmp", I_FCMP, 0x00}, {"fcmpe", I_FCMP, 0x10},
  {"fcvt", I_FCVT},
  {"fcvtns", I_FCVTI, 0x00}, {"fcvtnu", I_FCVTI, 0x01}, {"fcvtas", I_FCVTI, 0x04},
  {"fcvtau", I_FCVTI, 0x05}, {"fcvtps", I_FCVTI, 0x08}, {"fcvtpu", I_FCVTI, 0x09},
  {"fcvtms", I_FCVTI, 0x10}, {"fcvtmu", I_FCVTI, 0x11}, {"fcvtzs", I_FCVTI, 0x18},
  {"fcvtzu", I_FCVTI, 0x19},
  {"scvtf", I_ICVTF, 0x02}, {"ucvtf", I_ICVTF, 0x03},
  {"fcsel", I_FCSEL, 0x1e200c00},
  {"ins", I_INS}, {"umov", I_UMOV},
};

static HashMap a64_insn_map;

// Vector (Advanced SIMD) instructions on arrangements, as in add v0.4s,
// v1.4s, v2.4s: the integer add, sub, mul, and, bic, orr and eor, and
// the floating-point fadd, fsub, fmul, fdiv, fmax, fmin, fabs, fneg and
// fsqrt. Returns false if `name` isn't one of them.
static bool a64_simd(char *name, AOp *ops, int n) {
  static struct {
    char *name;
    uint32_t op;
    int kind; // 0 integer (size bits 22-23), 1 bytes only, 2 FP (sz bit 22)
    int nregs;
  } insns[] = {
    {"add", 0x0e208400, 0, 3}, {"sub", 0x2e208400, 0, 3}, {"mul", 0x0e209c00, 0, 3},
    {"and", 0x0e201c00, 1, 3}, {"bic", 0x0e601c00, 1, 3}, {"orr", 0x0ea01c00, 1, 3},
    {"eor", 0x2e201c00, 1, 3},
    {"fadd", 0x0e20d400, 2, 3}, {"fsub", 0x0ea0d400, 2, 3}, {"fmul", 0x2e20dc00, 2, 3},
    {"fdiv", 0x2e20fc00, 2, 3}, {"fmax", 0x0e20f400, 2, 3}, {"fmin", 0x0ea0f400, 2, 3},
    {"fabs", 0x0ea0f800, 2, 2}, {"fneg", 0x2ea0f800, 2, 2}, {"fsqrt", 0x2ea1f800, 2, 2},
  };
  int i = 0;
  while (i < sizeof(insns) / sizeof(*insns) && strcmp(insns[i].name, name))
    i++;
  if (i == sizeof(insns) / sizeof(*insns))
    return false;

  AOp *d = &ops[0];
  if (n != insns[i].nregs)
    fail("bad operands");
  for (int k = 0; k < n; k++)
    if (ops[k].kind != A_VEC || ops[k].size != d->size || ops[k].lanes != d->lanes)
      fail("bad operands");
  int bytes = d->size * d->lanes;
  int lg = __builtin_ctz(d->size);
  if ((bytes != 8 && bytes != 16) || (insns[i].kind == 1 && d->size != 1) ||
      (insns[i].kind == 2 && d->size != 4 && d->size != 8) ||
      (insns[i].kind == 0 && lg == 3 && (bytes == 8 || !strcmp(name, "mul"))))
    fail("bad arrangement");
  uint32_t insn = insns[i].op | (bytes == 16) << 30 | d->reg | ops[1].reg << 5;
  if (insns[i].kind == 0)
    insn |= lg << 22;
  if (insns[i].kind == 2)
    insn |= (d->size == 8) << 22;
  if (n == 3)
    insn |= ops[2].reg << 16;
  a64_out(insn);
  return true;
}

// An FP number that fmov can take, as its 8-bit immediate abcdefgh, or
// -1: (16 + efgh) / 16 times 2 to the power cd + 1, or cd - 3 if b is
// set, negated if a is.
static int a64_fimm8(double f) {
  for (int i = 0; i < 256; i++) {
    int cd = (i >> 4) & 3;
    int e = (i & 0x40) ? cd - 3 : cd + 1;
    double v = (16 + (i & 15)) / 16.0 * (e >= 0 ? (double)(1 << e) : 1.0 / (1 << -e));
    if ((i & 0x80 ? -v : v) == f)
      return i;
  }
  return -1;
}

// Encodes one ARM64 instruction: mnemonic `name` (`len` chars), operands
// at p.
static void a64_instruction(char *name, int len) {
  if (!a64_insn_map.capacity)
    for (int i = 0; i < sizeof(a64_insns) / sizeof(*a64_insns); i++)
      hashmap_put(&a64_insn_map, a64_insns[i].name, &a64_insns[i]);

  char *mnem = strndup(name, len);
  for (char *c = mnem; *c; c++)
    *c = tolower((unsigned char)*c);

  AOp ops[5];
  int n;

  // b.eq, b.ne, ...
  if (len > 2 && !strncmp(mnem, "b.", 2)) {
    int cc = a64_cond(mnem + 2, len - 2);
    if (cc < 0)
      fail("unknown instruction '%s'", mnem);
    n = a64_operands(ops, 1);
    if (n != 1)
      fail("bad operands");
    a64_branch(0x54000000 | cc, R_AARCH64_CONDBR19, &ops[0]);
    return;
  }

  AInsn *insn = hashmap_get(&a64_insn_map, mnem);
  if (!insn)
    fail("unknown instruction '%s'", mnem);

  // The barriers take a name (dmb ish), which isn't an operand elsewhere.
  if (insn->kind == I_BARRIER) {
    static struct { char *name; int crm; } opts[] = {
      {"sy", 15}, {"ish", 11}, {"ishld", 9}, {"ishst", 10}, {"ld", 13}, {"st", 14},
      {"osh", 3}, {"oshld", 1}, {"oshst", 2}, {"nsh", 7}, {"nshld", 5}, {"nshst", 6},
    };
    skip_space();
    if (*p == '#')
      p++;
    int crm = 15;
    if (*p) {
      char *w;
      int wlen = a64_word(&w);
      crm = -1;
      for (int i = 0; i < sizeof(opts) / sizeof(*opts); i++)
        if (word_is(w, wlen, opts[i].name))
          crm = opts[i].crm;
      if (crm < 0) {
        p = w;
        crm = read_int();
      }
      if (crm < 0 || crm > 15)
        fail("bad barrier option");
    }
    a64_out(insn->op | crm << 8);
    return;
  }

  n = a64_operands(ops, 5);
  AOp *d = &ops[0];
  if (n && d->kind == A_VEC && d->lanes && a64_simd(mnem, ops, n))
    return;

  // These take general registers all of one size.
  switch (insn->kind) {
  case I_SHIFT: case I_EXTR: case I_DP1: case I_DP2: case I_ADC: case I_NGC:
  case I_CSEL: case I_CSET: case I_CINC: case I_CCMP: case I_MULH:
    for (int i = 0; i < n; i++)
      if (ops[i].kind == A_GP && (ops[i].size != d->size || d->kind != A_GP))
        fail("operands of different sizes");
  }

  switch (insn->kind) {
  case I_ADDSUB:
    if (n < 3)
      fail("bad operands");
    a64_addsub(insn->op, d, &ops[1], &ops[2], n - 2);
    return;
  case I_CMP: {
    if (n < 2)
      fail("bad operands");
    AOp zr = {A_GP, 31, d->size};
    a64_addsub(insn->op, &zr, d, &ops[1], n - 1);
    return;
  }
  case I_NEG: {
    if (n < 2)
      fail("bad operands");
    AOp zr = {A_GP, 31, d->size};
    a64_addsub(insn->op, d, &zr, &ops[1], n - 1);
    return;
  }
  case I_LOGIC:
    if (n < 3)
      fail("bad operands");
    a64_logic(insn->op & 3, insn->op > 3, d, &ops[1], &ops[2], n - 2);
    return;
  case I_TST: {
    if (n < 2)
      fail("bad operands");
    AOp zr = {A_GP, 31, d->size};
    a64_logic(3, false, &zr, d, &ops[1], n - 1);
    return;
  }
  case I_MOV:
    if (n != 2)
      fail("bad operands");
    if (d->kind == A_GP) {
      if (ops[1].kind == A_VEC) { // mov x0, v0.d[1]
        insn = hashmap_get(&a64_insn_map, "umov");
        break;
      }
      a64_mov(d, &ops[1]);
      return;
    }
    if (d->kind == A_VEC && d->elem >= 0) { // mov v0.d[1], x0
      insn = hashmap_get(&a64_insn_map, "ins");
      break;
    }
    fail("unsupported operands");
  case I_MVN: {
    if (n < 2)
      fail("bad operands");
    // mvn is orn from the zero register.
    AOp zr = {A_GP, 31, d->size};
    a64_logic(1, true, d, &zr, &ops[1], n - 1);
    return;
  }
  case I_MOVW:
    if (n < 2 || n > 3)
      fail("bad operands");
    a64_movw(insn->op, ops, n);
    return;
  case I_SHIFT: {
    if (n != 3)
      fail("bad operands");
    int bits = d->size * 8;
    if (ops[2].kind == A_GP) {
      static int dp2[] = {8, 9, 10, 11};
      a64_out(sf(d) | 0x1ac00000 | gpreg(&ops[2], false) << 16 | dp2[insn->op] << 10 |
              gpreg(&ops[1], false) << 5 | gpreg(d, false));
      return;
    }
    int s = imm(&ops[2], 0, bits - 1);
    switch (insn->op) {
    case LSL: a64_bfm(2, d, &ops[1], (bits - s) % bits, bits - 1 - s); return;
    case LSR: a64_bfm(2, d, &ops[1], s, bits - 1); return;
    case ASR: a64_bfm(0, d, &ops[1], s, bits - 1); return;
    }
    // ror is extr of a register with itself.
    a64_out(sf(d) | 0x13800000 | (d->size == 8) << 22 | gpreg(&ops[1], false) << 16 |
            s << 10 | gpreg(&ops[1], false) << 5 | gpreg(d, false));
    return;
  }
  case I_BFM:
    if (n != 4)
      fail("bad operands");
    a64_bfm(insn->op, d, &ops[1], imm(&ops[2], 0, 63), imm(&ops[3], 0, 63));
    return;
  case I_BFX: {
    if (n != 4)
      fail("bad operands");
    int lsb = imm(&ops[2], 0, 63), w = imm(&ops[3], 1, 64);
    a64_bfm(insn->op, d, &ops[1], lsb, lsb + w - 1);
    return;
  }
  case I_BFIZ: {
    if (n != 4)
      fail("bad operands");
    int bits = d->size * 8;
    int lsb = imm(&ops[2], 0, bits - 1), w = imm(&ops[3], 1, bits);
    a64_bfm(insn->op, d, &ops[1], (bits - lsb) % bits, w - 1);
    return;
  }
  case I_EXT: {
    // sxtb x0, w1 and the like: the source is a w register
    if (n != 2 || ops[1].kind != A_GP || ops[1].size != 4)
      fail("bad operands");
    int bits = insn->op == SXTB || insn->op == UXTB ? 8 : insn->op == SXTH || insn->op == UXTH ? 16 : 32;
    bool sign = insn->op >= SXTB;
    if ((!sign && bits < 32 && d->size == 8) || (bits == 32 && d->size == 4))
      fail("bad operands");
    // uxtw x0, w1 is mov w0, w1 (which zeroes the upper half), as GNU as
    // makes it.
    if (insn->op == UXTW) {
      a64_out(0x2a0003e0 | gpreg(&ops[1], false) << 16 | gpreg(d, false));
      return;
    }
    AOp src = ops[1];
    src.size = d->size;
    a64_bfm(sign ? 0 : 2, d, &src, 0, bits - 1);
    return;
  }
  case I_EXTR:
    if (n != 4)
      fail("bad operands");
    a64_out(sf(d) | 0x13800000 | (d->size == 8) << 22 | gpreg(&ops[2], false) << 16 |
            imm(&ops[3], 0, d->size * 8 - 1) << 10 | gpreg(&ops[1], false) << 5 |
            gpreg(d, false));
    return;
  case I_DP1: {
    // rbit, rev16, rev32 (64 bits only), rev, clz, cls. rev of 32 bits
    // is rev32's opcode.
    if (n != 2)
      fail("bad operands");
    int opc = insn->op;
    if (opc == 3 && d->size == 4)
      opc = 2;
    else if (opc == 2 && d->size != 8)
      fail("bad operands");
    a64_out(sf(d) | 0x5ac00000 | opc << 10 | gpreg(&ops[1], false) << 5 | gpreg(d, false));
    return;
  }
  case I_DP2:
    if (n != 3)
      fail("bad operands");
    a64_out(sf(d) | 0x1ac00000 | gpreg(&ops[2], false) << 16 | insn->op << 10 |
            gpreg(&ops[1], false) << 5 | gpreg(d, false));
    return;
  case I_DP3:
  case I_MUL: {
    // madd d, n, m, a; mul d, n, m is madd with a the zero register. The
    // long ones (smaddl, smull...) take w registers and make an x.
    bool is_long = insn->op & 0x80000000;
    if (n != (insn->kind == I_MUL ? 3 : 4))
      fail("bad operands");
    if (is_long ? d->size != 8 || ops[1].size != 4 || ops[2].size != 4
                : ops[1].size != d->size || ops[2].size != d->size)
      fail("bad operands");
    int ra = insn->kind == I_MUL ? 31 : gpreg(&ops[3], false);
    a64_out((is_long ? 0 : sf(d)) | insn->op | gpreg(&ops[2], false) << 16 | ra << 10 |
            gpreg(&ops[1], false) << 5 | gpreg(d, false));
    return;
  }
  case I_MULH:
    if (n != 3 || d->size != 8)
      fail("bad operands");
    a64_out(insn->op | gpreg(&ops[2], false) << 16 | gpreg(&ops[1], false) << 5 |
            gpreg(d, false));
    return;
  case I_ADC:
    if (n != 3)
      fail("bad operands");
    a64_out(sf(d) | insn->op << 29 | 0x1a000000 | gpreg(&ops[2], false) << 16 |
            gpreg(&ops[1], false) << 5 | gpreg(d, false));
    return;
  case I_NGC:
    if (n != 2)
      fail("bad operands");
    a64_out(sf(d) | insn->op << 29 | 0x1a000000 | gpreg(&ops[1], false) << 16 | 31 << 5 |
            gpreg(d, false));
    return;
  case I_CSEL: {
    // csel, csinc, csinv, csneg: op2 in bit 10, op in bit 30
    if (n != 4 || ops[3].kind != A_COND)
      fail("bad operands");
    a64_out(sf(d) | (insn->op >> 1) << 30 | 0x1a800000 | gpreg(&ops[2], false) << 16 |
            ops[3].val << 12 | (insn->op & 1) << 10 | gpreg(&ops[1], false) << 5 |
            gpreg(d, false));
    return;
  }
  case I_CSET: {
    // cset d, cond is csinc d, zr, zr, !cond; csetm is csinv.
    if (n != 2 || ops[1].kind != A_COND || ops[1].val >= 14)
      fail("bad operands");
    a64_out(sf(d) | (insn->op >> 1) << 30 | 0x1a800000 | 31 << 16 | (ops[1].val ^ 1) << 12 |
            (insn->op & 1) << 10 | 31 << 5 | gpreg(d, false));
    return;
  }
  case I_CINC: {
    // cinc d, n, cond is csinc d, n, n, !cond; cinv csinv, cneg csneg.
    if (n != 3 || ops[2].kind != A_COND || ops[2].val >= 14)
      fail("bad operands");
    int rn = gpreg(&ops[1], false);
    a64_out(sf(d) | (insn->op >> 1) << 30 | 0x1a800000 | rn << 16 | (ops[2].val ^ 1) << 12 |
            (insn->op & 1) << 10 | rn << 5 | gpreg(d, false));
    return;
  }
  case I_CCMP: {
    // ccmp n, m|imm5, nzcv, cond
    if (n != 4 || ops[3].kind != A_COND)
      fail("bad operands");
    bool is_imm = ops[1].kind == A_IMM;
    int m = is_imm ? imm(&ops[1], 0, 31) : gpreg(&ops[1], false);
    a64_out(sf(d) | insn->op << 30 | 0x3a400000 | m << 16 | ops[3].val << 12 | is_imm << 11 |
            gpreg(d, false) << 5 | imm(&ops[2], 0, 15));
    return;
  }
  case I_LDST:
  case I_LDSTU:
    if (n < 2 || n > 3)
      fail("bad operands");
    if (n == 2 && insn->kind == I_LDST)
      a64_label(&ops[1]);
    if (ops[1].kind == A_IMM && insn->kind == I_LDST && ops[1].sym && !ops[1].mod) {
      // ldr x0, label: pc-relative, within 1 MiB
      uint32_t lit = d->kind == A_FP ? (d->size == 4 ? 0x1c000000 : d->size == 8 ? 0x5c000000 :
                                        0x9c000000) | d->reg :
                     !strcmp(mnem, "ldrsw") ? 0x98000000 | gpreg(d, false) :
                     !strcmp(mnem, "ldr") ? (d->size == 8 ? 0x58000000 : 0x18000000) |
                                             gpreg(d, false) : 0;
      if (!lit)
        fail("bad operands");
      a64_hole(R_AARCH64_LD_PREL_LO19, ops[1].sym, ops[1].val);
      a64_out(lit);
      return;
    }
    a64_ldst(mnem, d, &ops[1], n == 3 ? &ops[2] : NULL, insn->kind == I_LDSTU);
    return;
  case I_LDSTP:
    a64_ldstp(mnem, ops, n);
    return;
  case I_EXCL:
    a64_excl(mnem, ops, n);
    return;
  case I_B:
    if (n != 1)
      fail("bad operands");
    a64_branch(insn->op, insn->op & 0x80000000 ? R_AARCH64_CALL26 : R_AARCH64_JUMP26, d);
    return;
  case I_CBZ:
    if (n != 2)
      fail("bad operands");
    a64_branch(sf(d) | insn->op | gpreg(d, false), R_AARCH64_CONDBR19, &ops[1]);
    return;
  case I_TBZ: {
    if (n != 3)
      fail("bad operands");
    int bit = imm(&ops[1], 0, d->size * 8 - 1);
    a64_branch((bit >> 5) << 31 | insn->op | (bit & 31) << 19 | gpreg(d, false),
               R_AARCH64_TSTBR14, &ops[2]);
    return;
  }
  case I_BR:
    if (n != 1 || !is_x(d))
      fail("bad operands");
    a64_out(insn->op | gpreg(d, false) << 5);
    return;
  case I_RET:
    if (n > 1 || (n && !is_x(d)))
      fail("bad operands");
    a64_out(insn->op | (n ? gpreg(d, false) : 30) << 5);
    return;
  case I_ADR: {
    // adr: pc-relative, within 1 MiB; adrp: the page, within 4 GiB
    if (n == 2)
      a64_label(&ops[1]);
    if (n != 2 || !is_x(d) || ops[1].kind != A_IMM || !ops[1].sym)
      fail("bad operands");
    bool page = insn->op & 0x80000000;
    int type = !page ? R_AARCH64_ADR_PREL_LO21 :
               ops[1].mod ? ops[1].mod->reloc : R_AARCH64_ADR_PREL_PG_HI21;
    if (ops[1].mod && (!page || type == R_AARCH64_ADD_ABS_LO12_NC))
      fail("bad relocation operator");
    a64_hole(type, ops[1].sym, ops[1].val);
    a64_out(insn->op | gpreg(d, false));
    return;
  }
  case I_EXCEPT:
    a64_out(insn->op | (n ? imm(d, 0, 0xffff) : 0) << 5);
    return;
  case I_FIXED:
    if (n)
      fail("bad operands");
    a64_out(insn->op);
    return;
  case I_MRS:
    if (n != 2 || !is_x(d) || ops[1].kind != A_SYSREG)
      fail("bad operands");
    a64_out(insn->op | ops[1].val << 5 | gpreg(d, false));
    return;
  case I_MSR:
    if (n != 2 || d->kind != A_SYSREG || !is_x(&ops[1]))
      fail("bad operands");
    a64_out(insn->op | d->val << 5 | gpreg(&ops[1], false));
    return;
  case I_FMOV: {
    if (n != 2)
      fail("bad operands");
    AOp *s = &ops[1];
    if (d->kind == A_FP && s->kind == A_FP) {
      a64_fp(0x1e204000, ops, n, 2);
      return;
    }
    if (d->kind == A_FP && s->kind == A_FLOAT) {
      int imm8 = a64_fimm8(s->fval);
      if (imm8 < 0)
        fail("immediate can't be encoded");
      a64_out(0x1e201000 | ftype(d) | imm8 << 13 | d->reg);
      return;
    }
    // Between general and FP registers: the sizes must match, but for
    // the high half of a vector, v0.d[1].
    bool to_fp = d->kind != A_GP;
    AOp *g = to_fp ? s : d, *f = to_fp ? d : s;
    if (g->kind != A_GP)
      fail("bad operands");
    uint32_t insn32;
    if (f->kind == A_VEC && f->size == 8 && f->elem == 1 && g->size == 8)
      insn32 = 0x9eae0000;
    else if (f->kind == A_FP && f->size == g->size)
      insn32 = g->size == 8 ? 0x9e660000 : 0x1e260000;
    else
      fail("bad operands");
    a64_out(insn32 | to_fp << 16 | (to_fp ? f->reg : gpreg(g, false)) |
            (to_fp ? gpreg(g, false) : f->reg) << 5);
    return;
  }
  case I_FP1:
    a64_fp(insn->op, ops, n, 2);
    return;
  case I_FP2:
    a64_fp(insn->op, ops, n, 3);
    return;
  case I_FP3:
    a64_fp(insn->op, ops, n, 4);
    return;
  case I_FCMP:
    if (n != 2)
      fail("bad operands");
    if (ops[1].kind == A_FLOAT || ops[1].kind == A_IMM) {
      if ((ops[1].kind == A_FLOAT && ops[1].fval != 0) ||
          (ops[1].kind == A_IMM && (ops[1].val || ops[1].sym)))
        fail("fcmp compares with 0.0 only");
      a64_out(0x1e202000 | ftype(d) | d->reg << 5 | insn->op | 8);
      return;
    }
    same_size(d, &ops[1]);
    a64_out(0x1e202000 | ftype(d) | ops[1].reg << 16 | d->reg << 5 | insn->op);
    return;
  case I_FCVT: {
    // Between half, single and double precision
    if (n != 2 || d->kind != A_FP || ops[1].kind != A_FP || d->size == ops[1].size)
      fail("bad operands");
    int opc = d->size == 4 ? 0 : d->size == 8 ? 1 : 3;
    a64_out(0x1e224000 | ftype(&ops[1]) | opc << 15 | ops[1].reg << 5 | d->reg);
    return;
  }
  case I_FCVTI:
    // To an integer: rmode and opcode fields from the table
    if (n != 2 || d->kind != A_GP)
      fail("bad operands");
    a64_out(sf(d) | 0x1e200000 | ftype(&ops[1]) | (insn->op >> 3) << 19 |
            (insn->op & 7) << 16 | ops[1].reg << 5 | gpreg(d, false));
    return;
  case I_ICVTF:
    if (n != 2 || ops[1].kind != A_GP)
      fail("bad operands");
    a64_out(sf(&ops[1]) | 0x1e200000 | ftype(d) | insn->op << 16 | gpreg(&ops[1], false) << 5 |
            d->reg);
    return;
  case I_FCSEL:
    if (n != 4 || ops[3].kind != A_COND)
      fail("bad operands");
    same_size(d, &ops[1]);
    same_size(d, &ops[2]);
    a64_out(insn->op | ftype(d) | ops[2].reg << 16 | ops[3].val << 12 | ops[1].reg << 5 |
            d->reg);
    return;
  default:
    break;
  }

  // ins v0.d[1], x0 and umov x0, v0.d[1] (and mov, their alias): an
  // element of 1, 2, 4 or 8 bytes, as imm5 has it
  AOp *v = insn->kind == I_INS ? &ops[0] : &ops[1];
  AOp *g = insn->kind == I_INS ? &ops[1] : &ops[0];
  if (n != 2 || v->kind != A_VEC || v->elem < 0 || g->kind != A_GP)
    fail("bad operands");
  int lg = __builtin_ctz(v->size);
  if (lg > 3 || v->elem << lg >= 16 || (insn->kind == I_UMOV && (g->size == 8) != (lg == 3)))
    fail("bad operands");
  uint32_t imm5 = (v->elem << (lg + 1)) | (1 << lg);
  if (insn->kind == I_INS)
    a64_out(0x4e001c00 | imm5 << 16 | gpreg(g, false) << 5 | v->reg);
  else
    a64_out((lg == 3) << 30 | 0x0e003c00 | imm5 << 16 | v->reg << 5 | gpreg(g, false));
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
  return read_sum(read_term());
}

// Skips the ',' before a directive's next argument, if there is one.
static bool next_arg(void) {
  skip_space();
  if (*p != ',')
    return false;
  p++;
  skip_space();
  return true;
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

// .ascii "str"[, "str"...], and .asciz and .string, which end each
// string with a 0: the bytes, with GNU as's escapes (\n, \t, \\, \",
// octal \ddd, hex \xhh, ...).
static void string_directive(bool nul) {
  for (;;) {
    skip_space();
    if (*p != '"')
      fail("expected a string");
    p++;
    while (*p != '"') {
      if (!*p)
        fail("unterminated string");
      if (*p != '\\') {
        out(*p++);
        continue;
      }
      p++;
      if ('0' <= *p && *p <= '7') {
        int c = 0;
        for (int i = 0; i < 3 && '0' <= *p && *p <= '7'; i++)
          c = c * 8 + *p++ - '0';
        out(c & 0xff);
        continue;
      }
      if (*p == 'x' && isxdigit((unsigned char)p[1])) {
        int c = 0;
        for (p++; isxdigit((unsigned char)*p); p++)
          c = c * 16 + (isdigit((unsigned char)*p) ? *p - '0' : tolower(*p) - 'a' + 10);
        out(c & 0xff);
        continue;
      }
      switch (*p) {
      case 'b': out('\b'); break;
      case 'f': out('\f'); break;
      case 'n': out('\n'); break;
      case 'r': out('\r'); break;
      case 't': out('\t'); break;
      case '\0': fail("unterminated string");
      default: out(*p); break;
      }
      p++;
    }
    p++;
    if (nul)
      out(0);
    skip_space();
    if (*p != ',')
      return;
    p++;
  }
}

// `n` bytes of `fill`
static void out_fill(int64_t n, int fill) {
  if (n < 0)
    fail("negative size");
  if (fill & 0xff)
    memset(reserve(n), fill, n);
  else
    out_zeros(n);
}

// `n` bytes of NOPs, as GNU as pads code (test/asm.sh compares the
// bytes): from 88 bytes on, a jump over them first, then the shortest
// NOP and 11-byte ones.
static void out_nops(int64_t n) {
  static char *nops[] = {
    "", "\x90", "\x66\x90", "\x0f\x1f\x00", "\x0f\x1f\x40\x00",
    "\x0f\x1f\x44\x00\x00", "\x66\x0f\x1f\x44\x00\x00",
    "\x0f\x1f\x80\x00\x00\x00\x00", "\x0f\x1f\x84\x00\x00\x00\x00\x00",
    "\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
    "\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00",
    "\x66\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00",
  };
  if (n >= 88 && n - 2 <= 127) {
    out(0xeb);
    out(n - 2);
    n -= 2;
  } else if (n >= 88) {
    out(0xe9);
    out_n(4, n - 5);
    n -= 5;
  }
  for (int i = 0; i < n % 11; i++)
    out((unsigned char)nops[n % 11][i]);
  for (int64_t k = 0; k < n / 11; k++)
    for (int i = 0; i < 11; i++)
      out((unsigned char)nops[11][i]);
}

// Pads to a multiple of `n` bytes with `fill`, or by default with NOPs
// in code and zeros elsewhere, unless that takes more than `max` bytes
// (if max isn't 0).
static void pad_to(int64_t n, int fill, int64_t max) {
  if (n <= 0 || (n & (n - 1)) || n > (1 << 30))
    fail("bad alignment");
  cur->align = MAX(cur->align, n);
  int64_t pad = (n - cur->bytes.len % n) % n;
  if (!pad || (max && pad > max))
    return;
  // A jump before it may still change size (see layout()), and move it,
  // but for ARM64's, which keep everything 4-byte aligned.
  if (cur->njumps && !(target_arm64 && n <= 4))
    fail("alignment after a jump");
  if (fill < 0 && (cur->flags & SHF_EXECINSTR) && target_arm64 && pad % 4 == 0) {
    for (int64_t i = 0; i < pad; i += 4)
      out_n(4, 0xd503201f); // nop
  } else if (fill < 0 && (cur->flags & SHF_EXECINSTR) && !target_arm64) {
    out_nops(pad);
  } else {
    out_fill(pad, fill < 0 ? 0 : fill);
  }
}

static void align_to_n(int n) {
  pad_to(n, -1, 0);
}

// .section name[,"flags"[,@type]]
static void section_directive(void) {
  skip_space();
  char *start = p;
  while (*p && *p != ',' && *p != ' ')
    p++;
  char *name = strndup(start, p - start);

  // As with GNU as, the name alone makes .init_array.N an init array.
  int flags = 0;
  int type = SHT_PROGBITS;
  int entsize = 0;
  if (!strncmp(name, ".init_array", 11))
    type = SHT_INIT_ARRAY;
  else if (!strncmp(name, ".fini_array", 11))
    type = SHT_FINI_ARRAY;
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
      else if (*f == 'M')
        flags |= SHF_MERGE;
      else if (*f == 'S')
        flags |= SHF_STRINGS;
      else
        fail("unsupported section flag '%c'", *f);
    }
    skip_space();
    if (*p == ',') {
      p++;
      skip_space();
      if (!strncmp(p, "@nobits", 7))
        type = SHT_NOBITS;
      else if (!strncmp(p, "@init_array", 11))
        type = SHT_INIT_ARRAY;
      else if (!strncmp(p, "@fini_array", 11))
        type = SHT_FINI_ARRAY;
      else if (strncmp(p, "@progbits", 9))
        fail("unsupported section type");
      // A merged section's pieces are this many bytes: "aMS",@progbits,1
      if (flags & SHF_MERGE) {
        while (*p && *p != ',')
          p++;
        if (!next_arg())
          fail("a merged section needs an entry size");
        entsize = read_int();
      }
      while (*p)
        p++;
    } else if (flags & SHF_MERGE) {
      fail("a merged section needs an entry size");
    }
  } else {
    // Without flags, a well-known name gets the ones GNU as gives it.
    if (!strncmp(name, ".text", 5) || !strcmp(name, ".init") ||
        !strcmp(name, ".fini"))
      flags = SHF_ALLOC | SHF_EXECINSTR;
    else if (!strncmp(name, ".rodata", 7))
      flags = SHF_ALLOC;
    else if (!strncmp(name, ".data", 5) || type != SHT_PROGBITS)
      flags = SHF_ALLOC | SHF_WRITE;
    else if (!strncmp(name, ".bss", 4))
      flags = SHF_ALLOC | SHF_WRITE;
    else if (!strncmp(name, ".tdata", 6) || !strncmp(name, ".tbss", 5))
      flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
    if (!strncmp(name, ".bss", 4) || !strncmp(name, ".tbss", 5))
      type = SHT_NOBITS;
  }

  Section *sec = find_section(name);
  cur = sec ? sec : new_section(name, type, flags);
  if (!sec)
    cur->entsize = entsize;
}

static void directive(char *name, int len) {
#define IS(s) (len == sizeof(s) - 1 && !strncmp(name, s, len))
  skip_space();

  if (IS(".ascii") || IS(".asciz") || IS(".string")) {
    string_directive(!IS(".ascii"));
  } else if (IS(".byte") || IS(".quad") || IS(".long") || IS(".int") ||
             IS(".value") || IS(".short") || IS(".word") || IS(".hword") ||
             IS(".xword") || IS(".dword") || IS(".inst")) {
    // A list of values: numbers, symbols (in a .quad) or differences of
    // labels, as in `.long end - start`. (.word is 4 bytes on ARM64, as
    // .inst, an instruction's.)
    int size = IS(".byte") ? 1 : IS(".quad") || IS(".xword") || IS(".dword") ? 8 :
               IS(".long") || IS(".int") || IS(".inst") || (IS(".word") && target_arm64) ? 4 : 2;
    for (;;) {
      Operand op = {0};
      skip_space();
      read_value(&op);
      if (op.suffix != NO_SUFFIX)
        fail("unsupported relocation");
      if (op.minus)
        diff_hole(op.sym, op.minus, op.val, size);
      else if (op.sym && size == 8)
        hole(abs_reloc(8), op.sym, op.val, 8);
      else if (op.sym && size == 4)
        // A 32-bit address, as -g's .debug_info has for the offsets of
        // .debug_abbrev and .debug_line
        hole(abs_reloc(4), op.sym, op.val, 4);
      else if (op.sym)
        fail("symbol in a data directive smaller than .long");
      else if (size == 1 && (op.val < -128 || op.val > 255))
        fail(".byte value out of range");
      else
        out_n(size, op.val);
      skip_space();
      if (*p != ',')
        break;
      p++;
    }
  } else if (IS(".float") || IS(".single") || IS(".double")) {
    // Floating-point numbers, each rounded once from its decimal
    bool dbl = IS(".double");
    do {
      char *end;
      double d = dbl ? strtod(p, &end) : 0;
      float f = dbl ? 0 : strtof(p, &end);
      if (end == p)
        fail("expected a floating-point number");
      p = end;
      uint64_t bits = 0;
      memcpy(&bits, dbl ? (void *)&d : (void *)&f, dbl ? 8 : 4);
      out_n(dbl ? 8 : 4, bits);
    } while (next_arg());
  } else if (IS(".skip") || IS(".space")) {
    // .skip N[, fill]: N bytes of fill, or of zeros
    int64_t n = read_int();
    out_fill(n, next_arg() ? read_int() : 0);
  } else if (IS(".fill")) {
    // .fill repeat[, size[, value]]: repeat copies of value, in size
    // bytes each (1 by default), of which only the low 4 are value's
    int64_t repeat = read_int();
    int64_t size = 1, value = 0;
    if (next_arg()) {
      size = read_int();
      if (next_arg())
        value = read_int();
    }
    if (repeat < 0 || size < 0 || size > 8)
      fail("bad .fill");
    if (size > 4)
      value = (uint32_t)value;
    if (!value)
      out_zeros(repeat * size);
    for (int64_t i = 0; value && i < repeat; i++)
      out_n(size, value);
  } else if (IS(".loc")) {
    int file = read_int();
    int line = read_int();
    locs = grow(locs, nlocs, &caplocs, sizeof(Loc));
    locs[nlocs++] = (Loc){cur, cur->bytes.len, cur->njumps, file, line};
    while (*p) // column etc., ignored
      p++;
  } else if (IS(".type")) {
    // .type sym, @function (or %function, as GNU as also takes)
    Sym *sym = read_sym();
    expect_comma();
    if (*p == '%')
      *p = '@';
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
  } else if (IS(".weak")) {
    Sym *sym = read_sym();
    sym->is_global = sym->is_weak = true;
  } else if (IS(".hidden")) {
    read_sym()->visibility = STV_HIDDEN;
  } else if (IS(".protected")) {
    read_sym()->visibility = STV_PROTECTED;
  } else if (IS(".internal")) {
    read_sym()->visibility = STV_INTERNAL;
  } else if (IS(".set") || IS(".equ")) {
    // Another name for a symbol, `.set alias, target`, or a constant,
    // `.set n, 4 * 8`, which may be set again (as in a .rept loop)
    Sym *sym = read_sym();
    expect_comma();
    char *start = p;
    if (is_symstart(*p)) {
      Sym *target = read_sym();
      skip_space();
      if (!target->is_abs && !*p) {
        sym->alias_of = target;
        return;
      }
      p = start;
    }
    int64_t val = read_int();
    if (sym->sec || sym->alias_of || sym->is_used)
      fail("'%s' is used before it's set to a constant", sym->name);
    sym->is_abs = true;
    sym->value = val;
  } else if (IS(".align") || IS(".balign") || IS(".p2align")) {
    // .balign N[, fill[, max]] (.align is the same on x86-64) and
    // .p2align log2(N)[, fill[, max]] (.align on ARM64)
    int64_t n = read_int();
    if (IS(".p2align") || (IS(".align") && target_arm64)) {
      if (n < 0 || n > 30)
        fail("bad alignment");
      n = (int64_t)1 << n;
    }
    int fill = -1;
    int64_t max = 0;
    if (next_arg()) {
      if (*p != ',')
        fill = read_int() & 0xff;
      if (next_arg())
        max = read_int();
    }
    pad_to(n, fill, max);
  } else if (IS(".size")) {
    // .size sym, N or .size sym, .-start (from start to here, known
    // after layout)
    Sym *sym = read_sym();
    expect_comma();
    if (p[0] == '.' && p[1] == '-') {
      p += 2;
      sym->size_from = read_sym();
      char *name = format(".Lsize.%d", nsyms);
      sym->size_to = find_sym(name, strlen(name));
      define_sym(sym->size_to);
    } else {
      sym->size = read_int();
    }
  } else if (IS(".data")) {
    cur = find_section(".data");
  } else if (IS(".text")) {
    cur = text;
  } else if (IS(".bss")) {
    cur = find_section(".bss");
  } else if (IS(".section")) {
    section_directive();
  } else if (IS(".pushsection")) {
    if (nsection_stack == sizeof(section_stack) / sizeof(*section_stack))
      fail(".pushsection nested too deep");
    section_stack[nsection_stack++] = cur;
    section_directive();
  } else if (IS(".popsection")) {
    if (!nsection_stack)
      fail(".popsection without .pushsection");
    cur = section_stack[--nsection_stack];
  } else if (IS(".ident")) {
    // A note of what made the file, in .comment, as GNU as puts it
    char *s = read_string();
    Section *sec = cur;
    cur = find_section(".comment");
    if (!cur) {
      cur = new_section(".comment", SHT_PROGBITS, SHF_MERGE | SHF_STRINGS);
      cur->entsize = 1;
      out(0);
    }
    do {
      out(*s);
    } while (*s++);
    cur = sec;
  } else if (IS(".tlsdesccall")) {
    // The blr of a TLS descriptor call (see cgen_arm64.c), which the
    // linker may change: a relocation at the instruction that follows
    Sym *sym = read_sym();
    a64_hole(R_AARCH64_TLSDESC_CALL, sym, 0);
  } else if (IS(".arch") || IS(".cpu") || IS(".arch_extension")) {
    // ARM64's CPU and extensions: every instruction here is ARMv8-A's.
    while (*p)
      p++;
  } else if (len > 5 && !strncmp(name, ".cfi_", 5)) {
    // Call frame information, for unwinding through the code: mucc makes
    // none for C (no .eh_frame), so none for assembly either.
    while (*p)
      p++;
  } else if (IS(".zero")) {
    out_zeros(read_int());
  } else if (IS(".incbin")) {
    // .incbin "file"[, skip[, count]]: the file's bytes, as they are
    char *path = read_string();
    int64_t skip = 0, count = -1;
    skip_space();
    if (*p == ',') {
      p++;
      skip = read_int();
      skip_space();
      if (*p == ',') {
        p++;
        count = read_int();
      }
    }
    FILE *fp = fopen(path, "rb");
    if (!fp)
      fail(".incbin: %s: %s", path, strerror(errno));
    fseek(fp, 0, SEEK_END);
    int64_t size = ftell(fp);
    if (skip < 0 || skip > size || count < -1 || (count >= 0 && skip + count > size))
      fail(".incbin: %s: skip or count out of range", path);
    if (count < 0)
      count = size - skip;
    fseek(fp, skip, SEEK_SET);
    unsigned char *dest = reserve(count);
    if (fread(dest, 1, count, fp) != count)
      fail(".incbin: %s: read error", path);
    fclose(fp);
  } else if (IS(".comm")) {
    Sym *sym = read_sym();
    expect_comma();
    int64_t size = read_int();
    expect_comma();
    int align = read_int();
    if (sym->is_local) {
      // A local common symbol is allocated in .bss at the end.
      lcomms = realloc(lcomms, (nlcomms + 1) * sizeof(Sym *));
      lcomm_aligns = realloc(lcomm_aligns, (nlcomms + 1) * sizeof(int));
      lcomms[nlcomms] = sym;
      lcomm_aligns[nlcomms++] = align;
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

// A macro, `.macro name a, b=1, c:vararg` up to `.endm`: its parameters,
// their defaults ("" for none), and its lines, in which `\a` is the
// argument for a, `\@` counts expansions and `\()` is nothing.
typedef struct {
  StringArray params;
  StringArray defaults;
  bool is_vararg;     // the last parameter takes the rest of the arguments
  StringArray body;
} Macro;

static HashMap macros;
static int nexpansions; // for \@
static int expand_depth;

// The .rept or .macro block being read: its first line, its lines up to
// the matching .endr or .endm, and how many blocks are open inside it
static char *block_head;
static StringArray block;
static int block_depth;

static void line(char *s);

// The directive or instruction name `s` starts with, after spaces
static int first_word(char *s, char **start) {
  while (*s == ' ' || *s == '\t')
    s++;
  *start = s;
  while (is_symchar(*s))
    s++;
  return s - *start;
}

// (As equal() in mucc.h: first characters differ, mostly.)
static bool word_is(char *w, int len, char *name) {
  return w[0] == name[0] && !strncmp(w, name, len) && name[len] == '\0';
}

// Reads `name a, b=1, c:vararg` after .macro.
static void read_macro(char *s) {
  p = s;
  skip_space();
  char *start = p;
  while (is_symchar(*p))
    p++;
  if (p == start)
    fail("expected a macro name");
  Macro *m = calloc(1, sizeof(Macro));
  hashmap_put2(&macros, start, p - start, m);

  for (;;) {
    while (*p == ' ' || *p == '\t' || *p == ',')
      p++;
    if (!*p)
      break;
    if (m->is_vararg)
      fail("a parameter after a vararg one");
    start = p;
    while (is_symchar(*p))
      p++;
    if (p == start)
      fail("expected a macro parameter");
    strarray_push(&m->params, strndup(start, p - start));
    char *def = "";
    if (*p == ':') {
      p++;
      if (!strncmp(p, "vararg", 6))
        m->is_vararg = true;
      else if (strncmp(p, "req", 3))
        fail("unsupported macro parameter qualifier");
      while (isalpha((unsigned char)*p))
        p++;
    }
    if (*p == '=') {
      start = ++p;
      while (*p && *p != ',' && *p != ' ' && *p != '\t')
        p++;
      def = strndup(start, p - start);
    }
    strarray_push(&m->defaults, def);
  }
  m->body = block;
}

// Splits a macro call's arguments at commas outside quotes and
// parentheses, or at spaces if there are no commas.
static StringArray macro_args(char *s) {
  StringArray args = {};
  bool commas = false;
  int depth = 0;
  bool in_string = false;
  for (char *q = s; *q; q++) {
    if (in_string && *q == '\\' && q[1])
      q++;
    else if (*q == '"')
      in_string = !in_string;
    else if (!in_string && *q == '(')
      depth++;
    else if (!in_string && *q == ')')
      depth--;
    else if (!in_string && !depth && *q == ',')
      commas = true;
  }

  for (;;) {
    while (*s == ' ' || *s == '\t')
      s++;
    if (!*s && !commas)
      break;
    char *start = s;
    depth = 0;
    in_string = false;
    for (; *s; s++) {
      if (in_string && *s == '\\' && s[1]) {
        s++;
        continue;
      }
      if (*s == '"')
        in_string = !in_string;
      if (in_string)
        continue;
      if (*s == '(')
        depth++;
      else if (*s == ')')
        depth--;
      else if (!depth && (commas ? *s == ',' : (*s == ' ' || *s == '\t')))
        break;
    }
    char *end = s;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
      end--;
    strarray_push(&args, strndup(start, end - start));
    if (!*s)
      break;
    s++;
  }
  return args;
}

// Runs macro `m` with the arguments in `s`.
static void expand_macro(Macro *m, char *s) {
  StringArray args = macro_args(s);
  int n = m->params.len;
  char **vals = calloc(n + 1, sizeof(char *));

  int next = 0;
  for (int i = 0; i < args.len; i++) {
    // `name=value` sets that parameter.
    char *arg = args.data[i];
    char *eq = arg;
    while (is_symchar(*eq))
      eq++;
    int k = n;
    if (*eq == '=')
      for (k = 0; k < n; k++)
        if (word_is(arg, eq - arg, m->params.data[k]))
          break;
    if (k < n) {
      vals[k] = eq + 1;
      next = k + 1;
      continue;
    }

    if (next >= n)
      fail("too many macro arguments");
    // The last vararg parameter takes the rest, commas and all.
    if (m->is_vararg && next == n - 1) {
      char *rest = arg;
      for (int j = i + 1; j < args.len; j++)
        rest = format("%s,%s", rest, args.data[j]);
      vals[next++] = rest;
      break;
    }
    vals[next++] = arg;
  }

  if (++expand_depth > 1000)
    fail("macros nested too deep");
  int id = nexpansions++;

  for (int i = 0; i < m->body.len; i++) {
    char *buf;
    size_t len;
    FILE *out = open_memstream(&buf, &len);
    for (char *q = m->body.data[i]; *q; q++) {
      if (*q != '\\') {
        fputc(*q, out);
        continue;
      }
      if (q[1] == '@') {
        fprintf(out, "%d", id);
        q++;
        continue;
      }
      if (q[1] == '(' && q[2] == ')') {
        q += 2;
        continue;
      }
      char *start = q + 1;
      char *end = start;
      while (is_symchar(*end) && *end != '.' && *end != '$')
        end++;
      int k = 0;
      while (k < n && !word_is(start, end - start, m->params.data[k]))
        k++;
      if (end == start || k == n) {
        fputc(*q, out);
        continue;
      }
      fputs(vals[k] ? vals[k] : m->defaults.data[k], out);
      q = end - 1;
    }
    fclose(out);
    line(buf);
  }
  expand_depth--;
}

// Ends the .rept or .macro block being read, and runs a .rept.
static void end_block(void) {
  char *head = block_head;
  StringArray body = block;
  block_head = NULL;
  block = (StringArray){};

  char *word;
  int len = first_word(head, &word);
  if (word_is(word, len, ".macro")) {
    stmt = head;
    block = body;
    read_macro(word + len);
    block = (StringArray){};
    return;
  }

  stmt = head;
  p = word + len;
  int64_t count = read_int();
  skip_space();
  if (*p)
    fail("unexpected text after directive");
  for (int64_t i = 0; i < count; i++)
    for (int j = 0; j < body.len; j++)
      line(strdup(body.data[j]));
}

// Assembles one statement: optional labels, then a directive, a macro
// call or an instruction.
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

    Macro *m = hashmap_get2(&macros, start, len);
    if (m)
      expand_macro(m, p);
    else if (*start == '.')
      directive(start, len);
    else
      instruction(start, len);
    return;
  }
}

// A statement, or a line of a .rept or .macro block, kept until it ends
static void line(char *s) {
  char *word;
  int len = first_word(s, &word);

  if (block_head) {
    if (word_is(word, len, ".rept") || word_is(word, len, ".macro")) {
      block_depth++;
    } else if (word_is(word, len, ".endr") || word_is(word, len, ".endm")) {
      if (!block_depth--) {
        end_block();
        return;
      }
    }
    strarray_push(&block, strdup(s));
    return;
  }

  if (word_is(word, len, ".rept") || word_is(word, len, ".macro")) {
    block_head = strdup(s);
    block_depth = 0;
    return;
  }
  statement(s);
}

// Splits the text into statements: one per line, or several separated
// by ';'. Comments (# and /* */, or on ARM64 // and a # that starts a
// line) are dropped. Works in place.
static void read_input(char *text) {
  char *s = text;       // start of the current statement
  char *bol = text;     // start of the current line
  bool in_string = false;

  // The characters the loop acts on. It skips the rest at once: they are
  // most of the text.
  static bool special[256] = {
    ['\0'] = 1, ['\n'] = 1, [';'] = 1, ['"'] = 1, ['#'] = 1, ['/'] = 1, ['\\'] = 1,
  };

  for (char *q = text;; q++) {
    if (!special[(unsigned char)*q])
      continue;

    // On ARM64, # marks an immediate, unless it starts the line, and //
    // starts a comment, as # does below.
    if (target_arm64 && !in_string && *q == '#') {
      char *c = q;
      while (c > bol && (c[-1] == ' ' || c[-1] == '\t'))
        c--;
      if (c != bol)
        continue;
    }
    if (target_arm64 && !in_string && q[0] == '/' && q[1] == '/')
      *q = '#';

    // In a string, \" and \\ are escapes, not its end.
    if (in_string && *q == '\\' && q[1]) {
      q++;
      continue;
    }
    if (*q == '"')
      in_string = !in_string;

    // A /* */ comment becomes spaces; the newlines in it still end
    // statements.
    if (!in_string && q[0] == '/' && q[1] == '*') {
      char *end = strstr(q + 2, "*/");
      if (!end)
        fail("unterminated comment");
      for (char *c = q; c < end + 2; c++)
        if (*c != '\n')
          *c = ' ';
    }

    if (!in_string && *q == '#') {
      *q = '\0';
      while (q[1] && q[1] != '\n')
        q++;
      continue;
    }

    if (*q == '\0' || *q == '\n' || (!in_string && *q == ';')) {
      bool at_end = *q == '\0';
      if (*q == '\n')
        bol = q + 1;
      *q = '\0';
      line(s);
      if (at_end)
        break;
      s = q + 1;
      in_string = false;
    }
  }
  if (block_head) {
    stmt = block_head;
    fail("no .endr or .endm");
  }
  stmt = NULL;
}

//---------- Layout: jump sizes and addresses --------------------------------

static int jump_size(Jump *j) {
  if (target_arm64)
    return j->is_long ? 8 : 4;
  if (!j->is_long)
    return 2;
  return j->cc < 0 ? 5 : 6;
}

// Does a short jump at `from` reach `dist` bytes away? On ARM64, a
// conditional branch reaches 1 MiB, and tbz and tbnz 32 KiB.
static bool in_short_range(Jump *j, int64_t dist) {
  if (!target_arm64)
    return is_int8(dist);
  int64_t range = j->cc == R_AARCH64_TSTBR14 ? 1 << 15 : 1 << 20;
  return -range <= dist && dist < range;
}

// Can `j` be a short jump? Only to a label in the same section, where the
// distance is known now, that is local or hidden (so no other definition
// can take its place).
// (On ARM64, as with GNU as there, only a local one.)
static bool is_near(Jump *j, Section *sec) {
  Sym *t = j->target;
  return t->sec == sec &&
         (!t->is_global || (!target_arm64 && t->visibility != STV_DEFAULT));
}

static uint64_t sym_addr(Sym *sym) {
  return sym->pos + sym->sec->jumps_before[sym->njumps];
}

// Starts with every near jump short and makes long the ones whose target
// is too far, until nothing changes, as GNU as does.
static void layout(Section *sec) {
  // (An ARM64 branch elsewhere stays short, a relocation, as GNU as has it.)
  sec->jumps_before = calloc(sec->njumps + 1, sizeof(int));
  for (int i = 0; i < sec->njumps; i++)
    if (!is_near(&sec->jumps[i], sec) && !target_arm64)
      sec->jumps[i].is_long = true;

  for (;;) {
    for (int i = 0; i < sec->njumps; i++)
      sec->jumps_before[i + 1] = sec->jumps_before[i] + jump_size(&sec->jumps[i]);

    bool changed = false;
    for (int i = 0; i < sec->njumps; i++) {
      Jump *j = &sec->jumps[i];
      if (j->is_long || !is_near(j, sec))
        continue;
      // (from the jump's end on x86-64, and its start on ARM64)
      int64_t from = j->pos + sec->jumps_before[i] + (target_arm64 ? 0 : 2);
      int64_t dist = (int64_t)sym_addr(j->target) - from;
      if (!in_short_range(j, dist)) {
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

// ARM64's: a branch or an adr to a label in the same section (that only
// this file defines) is known now, and other pc-relative ones (adrp's
// page) are relocations, as are all the rest. Those of a local symbol are
// against its section, but GOT and TLS ones, which need the symbol.
static void a64_fill_hole(Section *sec, uint64_t offset, int type, Sym *sym, int64_t addend) {
  bool is_local = sym->sec && !sym->is_global;
  bool pcrel = type == R_AARCH64_CALL26 || type == R_AARCH64_JUMP26 ||
               type == R_AARCH64_CONDBR19 || type == R_AARCH64_TSTBR14 ||
               type == R_AARCH64_ADR_PREL_LO21 || type == R_AARCH64_LD_PREL_LO19;

  if (is_local && pcrel && sym->sec == sec) {
    int64_t val = (int64_t)sym->value + addend - (int64_t)offset;
    if (!arm64_put_field(sec->bytes.data + offset, type, val))
      fail("branch or address out of range");
    return;
  }

  // (These are the data and address relocations, from ABS64 to
  // LDST128_ABS_LO12_NC. As GNU as does on ARM64, one to a function
  // keeps its symbol.)
  bool adjustable = type >= R_AARCH64_ABS64 && type <= R_AARCH64_LDST128_ABS_LO12_NC &&
                    sym->type != STT_FUNC;
  if (is_local && adjustable) {
    add_reloc(sec, offset, type, NULL, sym->sec, sym->value + addend);
    return;
  }

  if (!sym->sec && !sym->common_align && is_dot_l(sym->name))
    fail("undefined label '%s'", sym->name);
  add_reloc(sec, offset, type, sym, NULL, addend);
}

// Fills the hole at `offset` in `sec` (whose bytes are final) for `sym`
// + `addend`. It's done here if the value is known now: a pc-relative
// reference to a local symbol in the same section. Otherwise it becomes a
// relocation: against the symbol's section for local symbols, which the
// linker can do without the symbol, else against the symbol.
static void fill_hole(Section *sec, uint64_t offset, int type, Sym *sym, int64_t addend) {
  if (target_arm64) {
    a64_fill_hole(sec, offset, type, sym, addend);
    return;
  }
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

// `end - start`: a constant, if both labels are in one section, as GNU as
// computes it (otherwise it would need a relocation pair).
static void fill_diff_hole(Section *sec, uint64_t offset, Hole *h) {
  if (!h->sym->sec || h->sym->sec != h->minus->sec)
    fail("can't subtract '%s' from '%s': they must be labels in one section",
         h->minus->name, h->sym->name);
  int64_t val = (int64_t)(h->sym->value - h->minus->value) + h->addend;
  if (h->size < 8) {
    int bits = h->size * 8;
    if (val < -(1LL << (bits - 1)) || val >= (1LL << bits))
      fail("'%s - %s' doesn't fit in %d bytes", h->sym->name, h->minus->name,
           h->size);
  }
  patch(&sec->bytes, offset, h->size, val);
}

static void place_jumps(Section *sec);

// Writes the final bytes of `sec`, with its jumps in place, then fills
// its holes.
static void finish_section(Section *sec) {
  if (sec->type == SHT_NOBITS)
    return;
  if (sec->njumps)
    place_jumps(sec);

  for (int i = 0; i < sec->nholes; i++) {
    Hole *h = &sec->holes[i];
    uint64_t offset = h->pos + sec->jumps_before[h->njumps];
    if (h->minus)
      fill_diff_hole(sec, offset, h);
    else
      fill_hole(sec, offset, h->type, h->sym, h->addend);
  }
}

// ARM64's: a short jump is its branch; a long one is the opposite branch
// over the next instruction, then a b to the target. (b.cond's opposite
// has the condition's low bit flipped; cbz and cbnz, and tbz and tbnz,
// differ in bit 24.)
static void a64_place_jumps(Section *sec) {
  Bytes out = {calloc(1, sec->size + 1), sec->size, sec->size + 1};
  if (!out.data)
    fail("out of memory");
  uint64_t dst = 0;
  int src = 0;

  for (int i = 0; i < sec->njumps; i++) {
    Jump *j = &sec->jumps[i];
    memcpy(out.data + dst, sec->bytes.data + src, j->pos - src);
    dst += j->pos - src;
    src = j->pos;
    uint32_t insn = j->insn;
    if (j->is_long) {
      bool is_bcond = (insn & 0xff000000) == 0x54000000;
      insn ^= is_bcond ? 1 : 1 << 24;
      arm64_put_field((unsigned char *)&insn, j->cc, 8);
      memcpy(out.data + dst, &insn, 4);
      dst += 4;
      insn = 0x14000000;
    }
    memcpy(out.data + dst, &insn, 4);
    j->field = dst;
    dst += 4;
  }
  memcpy(out.data + dst, sec->bytes.data + src, sec->bytes.len - src);
  free(sec->bytes.data);
  sec->bytes = out;

  // The offsets: a branch to another section, or to a global symbol, is
  // a relocation.
  for (int i = 0; i < sec->njumps; i++) {
    Jump *j = &sec->jumps[i];
    int type = j->is_long ? R_AARCH64_JUMP26 : j->cc;
    if (is_near(j, sec)) {
      int64_t val = (int64_t)sym_addr(j->target) - (int64_t)j->field;
      if (!arm64_put_field(sec->bytes.data + j->field, type, val))
        fail("branch out of range");
      continue;
    }
    fill_hole(sec, j->field, type, j->target, 0);
  }
}

// Copies `sec`'s bytes with its jumps, now that their sizes are known,
// in place. (Without jumps, as in data, its bytes are already final.)
static void place_jumps(Section *sec) {
  if (target_arm64) {
    a64_place_jumps(sec);
    return;
  }
  Bytes out = {calloc(1, sec->size + 1), sec->size, sec->size + 1};
  if (!out.data)
    fail("out of memory");
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

  // A long jump within the section is filled in here, even to a global
  // (not weak) symbol; one elsewhere is a relocation, through the PLT for
  // a global target. As GNU as does.
  for (int i = 0; i < sec->njumps; i++) {
    Jump *j = &sec->jumps[i];
    if (!j->is_long)
      continue;
    if (j->target->sec == sec && !j->target->is_weak) {
      int64_t val = (int64_t)sym_addr(j->target) - (int64_t)(j->field + 4);
      patch(&sec->bytes, j->field, 4, val);
      continue;
    }
    bool is_local = j->target->sec && !j->target->is_global;
    fill_hole(sec, j->field, is_local ? R_X86_64_PC32 : R_X86_64_PLT32, j->target, -4);
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

static void add_line_sequence(Section *line, Section *sec);

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

  // The program: a sequence for .text, and for any other section with
  // code, like section("name") gives, as with GNU as.
  for (int i = 0; i < nsections; i++) {
    Section *sec = sections[i];
    bool has_locs = sec == text;
    for (int k = 0; k < nlocs && !has_locs; k++)
      has_locs = locs[k].sec == sec;
    if (has_locs)
      add_line_sequence(line, sec);
  }
  patch(b, 0, 4, b->len - 4);
}

// A line table sequence for `sec`: set the start address, then one row
// per .loc in it.
static void add_line_sequence(Section *line, Section *sec) {
  Bytes *b = &line->bytes;
  put(b, 0);
  put_uleb(b, 9);
  put(b, DW_LNE_set_address);
  add_reloc(line, b->len, abs_reloc(8), NULL, sec, 0);
  put_n(b, 8, 0);

  uint64_t addr = 0;
  int file = 1;
  int lineno = 1;
  for (int i = 0; i < nlocs; i++) {
    Loc *loc = &locs[i];
    if (loc->sec != sec)
      continue;
    uint64_t a = loc->pos + sec->jumps_before[loc->njumps];

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
  put_uleb(b, sec->size - addr);
  put(b, 0);
  put_uleb(b, 1);
  put(b, DW_LNE_end_sequence);
}

static void add_debug_info(void) {
  // With -g, cgen.c writes .debug_info itself, naming .debug_line with
  // nothing in it: the line table goes there, as GNU as does it.
  Section *line = find_section(".debug_line");
  if (find_section(".debug_info")) {
    if (!line || line->bytes.len)
      fail(".debug_info without an empty .debug_line");
    add_line_table(line);
    line->size = line->bytes.len;
    return;
  }

  line = new_section(".debug_line", SHT_PROGBITS, 0);
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
  add_reloc(info, b->len, abs_reloc(4), NULL, abbrev, 0);
  put_n(b, 4, 0); // debug_abbrev_offset
  put(b, 8);      // address_size
  put_uleb(b, 1);
  add_reloc(info, b->len, abs_reloc(4), NULL, line, 0);
  put_n(b, 4, 0); // DW_AT_stmt_list
  add_reloc(info, b->len, abs_reloc(8), NULL, text, 0);
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
  // relocations. Then .symtab and .strtab, unless there are no symbols
  // (see below), and .shstrtab.
  int nsh = 1;
  for (int i = 0; i < nsections; i++) {
    sections[i]->index = nsh++;
    if (sections[i]->nrelocs)
      nsh++;
  }
  int symtab_idx = nsh;
  int strtab_idx = nsh + 1;

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
      bool defined = sym->sec || sym->common_align || sym->is_abs;
      if (!sym->keep && (!defined || (!global && is_dot_l(sym->name))))
        continue;

      int type = sym->is_tls ? STT_TLS : sym->type;
      int shndx = sym->common_align ? SHN_COMMON : sym->sec ? sym->sec->index :
                  sym->is_abs ? SHN_ABS : SHN_UNDEF;
      uint64_t value = sym->common_align ? sym->common_align : sym->value;
      sym->index = symtab.len / sizeof(Elf64_Sym);
      int bind = !global ? STB_LOCAL : sym->is_weak ? STB_WEAK : STB_GLOBAL;
      add_elf_sym(&symtab, &strtab, sym->name, bind, type, shndx, value,
                  sym->size);
      ((Elf64_Sym *)(symtab.data + symtab.len))[-1].st_other = sym->visibility;
    }
  }

  // With nothing but the null symbol, GNU as writes no .symtab or
  // .strtab (crtn.s, for one, has no symbols).
  bool has_symtab = symtab.len > sizeof(Elf64_Sym);
  if (has_symtab)
    nsh += 2;
  int shstrtab_idx = nsh++;

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
    s->sh_entsize = sec->entsize;
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

  Elf64_Shdr *s;
  if (has_symtab) {
    s = &sh[symtab_idx];
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
  }

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
  eh.e_machine = target_arm64 ? EM_AARCH64 : EM_X86_64;
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
  nlcomms = 0;
  nsection_stack = 0;
  macros = (HashMap){0};
  nexpansions = expand_depth = 0;
  block_head = NULL;
  block = (StringArray){0};
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

  cur = find_section(".bss");
  for (int i = 0; i < nlcomms; i++) {
    align_to_n(lcomm_aligns[i]);
    define_sym(lcomms[i]);
    out_zeros(lcomms[i]->size);
  }

  // Mark the stack as not executable, as gcc does. mucc's own output
  // already asks for this section; a hand-written .s file may not.
  if (!find_section(".note.GNU-stack"))
    new_section(".note.GNU-stack", SHT_PROGBITS, 0);

  // An alias is defined where its target is.
  for (int i = 0; i < nsyms; i++) {
    Sym *sym = symlist[i];
    if (!sym->alias_of)
      continue;
    Sym *t = sym->alias_of;
    for (int n = 0; t->alias_of; n++) {
      if (n > nsyms)
        fail("'%s' is an alias of itself", sym->name);
      t = t->alias_of;
    }
    if (!t->sec)
      fail("'%s' is an alias of '%s', which is not defined", sym->name, t->name);
    sym->sec = t->sec;
    sym->pos = t->pos;
    sym->njumps = t->njumps;
    sym->is_tls = t->is_tls;
    sym->type = sym->type ? sym->type : t->type;
    // (A size of .-start is only known after layout, below.)
    if (!sym->size && !sym->size_from) {
      sym->size = t->size;
      sym->size_from = t->size_from;
      sym->size_to = t->size_to;
    }
  }

  for (int i = 0; i < nsections; i++)
    layout(sections[i]);
  for (int i = 0; i < nsyms; i++)
    if (symlist[i]->sec)
      symlist[i]->value = sym_addr(symlist[i]);
  for (int i = 0; i < nsyms; i++) {
    Sym *sym = symlist[i];
    if (!sym->size_from)
      continue;
    if (sym->size_from->sec != sym->size_to->sec)
      fail(".size of '%s': not in the same section", sym->name);
    sym->size = sym->size_to->value - sym->size_from->value;
  }
  for (int i = 0; i < nsections; i++)
    finish_section(sections[i]);

  if ((nlocs && dwarf_files.len) || find_section(".debug_info"))
    add_debug_info();

  write_elf(path);
  return true;
}
