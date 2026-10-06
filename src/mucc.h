//============================================================================
// mucc.h - SHARED DECLARATIONS
//
// Every .c file includes this. It declares the data that flows through
// the pipeline, one stage per file:
//
//   1 token.c       source text -> Token list
//   2 preprocess.c  Token list -> Token list, with macros expanded
//   3 parser.c      Token list -> Obj (functions, globals) and Node (AST),
//     type.c        typed by add_type()
//   4 cgen.c        AST -> x86-64 assembly text
//   5 asm.c         assembly text -> ELF object file
//   6 link.c        object files and archives -> static executable
//
// main.c is the driver that runs them; ar.c is `mucc -ar`; strings.c,
// hashmap.c and unicode.c are support code.
//
// Where things are
//
// Each file is split into sections by `//---------- Name ---` lines;
// `grep -n '^//-------' src/*.c` lists them all. New code goes in the
// section it belongs to. Common changes and where they're made:
//
//   a keyword               token.c is_keyword(); also parser.c
//                           is_typename() if it can start a type
//   a predefined macro      preprocess.c "Predefined and builtin macros",
//                           init_macros()
//   a command-line option   main.c "Argument parsing", parse_args(); one
//                           that changes nothing goes in ignored_options[]
//   a __builtin_ function   parser.c "GNU builtins": gnu_builtin_names[]
//                           and gnu_builtin(); one that takes a type, in
//                           "Primary expressions", primary()
//   an __attribute__        parser.c "GNU attributes", apply_attribute()
//                           and is_known_attribute()
//   a warning               parser.c "Warnings"
//   a new AST node kind     NodeKind below, type.c add_type(), and cgen.c
//                           gen_expr() or gen_stmt()
//   an x86 instruction      asm.c "Instructions", insns[]
//   a test                  test/*.c (ASSERT) for the language,
//                           test/errors.sh for diagnostics, test/driver.sh
//                           for options
//============================================================================

//---------- System headers and small utilities ------------------------------

#define MUCC_VERSION "1.1.0"

// POSIX 2008 with its XSI part, which has realpath() (with musl, only then)
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <libgen.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#ifndef __GNUC__
# define __attribute__(x)
#endif

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;
typedef struct Relocation Relocation;
typedef struct Hideset Hideset;
typedef struct Cleanup Cleanup;
typedef struct LexBlock LexBlock;
typedef struct EnumConst EnumConst;

//---------- strings.c: string arrays, format() ------------------------------

typedef struct {
  char **data;
  int capacity;
  int len;
} StringArray;

void *arena_alloc(size_t size);
void strarray_push(StringArray *arr, char *s);
char *format(char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *vformat(char *fmt, va_list ap);

//---------- token.c: tokens and error reporting (stage 1) -------------------

// Token
typedef enum {
  TK_IDENT,   // Identifiers
  TK_PUNCT,   // Punctuators
  TK_KEYWORD, // Keywords
  TK_STR,     // String literals
  TK_NUM,     // Numeric literals
  TK_PP_NUM,  // Preprocessing numbers
  TK_EMBED,   // #embed's bytes, as one token (see "#embed" in preprocess.c)
  TK_EOF,     // End-of-file markers
} TokenKind;

typedef struct {
  char *name;
  int file_no;
  char *contents;

  // For #line directive
  char *display_name;
  int line_delta;
} File;

// #embed's bytes, which a TK_EMBED token holds
typedef struct {
  int64_t len;
  unsigned char data[];
} EmbedBytes;

// Token type. There is one for each token of every file and macro
// expansion, which is millions for a big file, so it is kept small: 80
// bytes.
typedef struct Token Token;
struct Token {
  TokenKind kind;   // Token kind
  int len;          // Token length
  Token *next;      // Next token
  char *loc;        // Token location
  Type *ty;         // Used if TK_NUM or TK_STR
  union {
    int64_t val;       // TK_NUM of an integer type: its value
    long double *fval; // TK_NUM of a floating type: its value
    char *str;         // TK_STR: its contents including terminating '\0'
    EmbedBytes *embed; // TK_EMBED
  };

  File *file;       // Source location
  char *filename;   // Filename, as #line says
  int line_no;      // Line number, as #line says once has_line (see
                    // set_line() in preprocess.c)
  uint16_t diag;    // The #pragma GCC diagnostic state here (see token.c)
  uint8_t pack;     // The #pragma pack in effect here, or 0
  bool at_bol : 1;  // True if this token is at beginning of line
  bool has_space : 1; // True if this token follows a space character
  bool has_line : 1;  // #line's difference is added to line_no
  Hideset *hideset; // For macro expansion
  Token *origin;    // If this is expanded from a macro, the original token
};

// Where the parser resumes after an error (see token.c), how many errors
// have been reported, and how many warnings -Werror made errors.
extern jmp_buf *error_recovery;
extern int error_count;
extern int werror_count;

noreturn void error(char *fmt, ...) __attribute__((format(printf, 1, 2)));
noreturn void error_at(char *loc, char *fmt, ...) __attribute__((format(printf, 2, 3)));
noreturn void error_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));
void warn_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));
void warn_opt(char *name, Token *tok, char *fmt, ...) __attribute__((format(printf, 3, 4)));
extern int diag_state;
void pragma_diagnostic(Token *tok);
bool warning_on(char *name);
noreturn void error_expected(Token *tok, char *what);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
void convert_pp_tokens(Token *tok);
File **get_input_files(void);
File *new_file(char *name, int file_no, char *contents);
File *add_input_file(char *path, char *contents);
Token *tokenize_string_literal(Token *tok, Type *basety);
Token *tokenize(File *file);
Token *tokenize_file(char *filename);
Token *alloc_token(void);
void free_tokens(Token *tok, Token *end);

#define unreachable() \
  error("internal error at %s:%d", __FILE__, __LINE__)

//---------- preprocess.c: preprocessor (stage 2) ----------------------------

char *search_include_paths(char *filename);
void init_macros(void);
void define_macro(char *name, char *buf);
void undef_macro(char *name);
Token *preprocess(Token *tok);
void expand_embed(Token *tok);
void join_adjacent_string_literals(Token *tok);
void print_macros(FILE *out);

//---------- parser.c: AST and parser (stage 3) ------------------------------

// Variable or function
typedef struct Obj Obj;
struct Obj {
  Obj *next;
  char *name;    // Variable name
  Type *ty;      // Type
  Token *tok;    // representative token
  bool is_local; // local or global/function
  int align;     // alignment

  // Local variable
  int offset;
  bool is_used; // named somewhere after its declaration (for warnings)
  int reg;      // kept in callee-saved register reg - 1, or 0 (cgen.c)
  int asm_reg;  // `register T x asm("r10")`: x86 register number + 1, or 0
  bool is_overaligned; // aligned above 16: its slot holds its address (cgen.c)
  int uses;     // how often it's used, weighted by loop depth (cgen.c)
  bool is_addr_taken;
  bool is_register; // declared `register`, so it has no address
  LexBlock *block;  // the block it's declared in, or NULL for the function's own

  // Global variable or function
  bool is_function;
  bool is_definition;
  bool is_static;
  bool is_weak;       // __attribute__((weak))
  char *alias_target; // alias("target"): another name for target
  char *asm_name;     // asm("name") on its declaration: its symbol, which
                      // it is renamed to once parsed (see parse())
  char *section;      // section("name"), or NULL
  char *visibility;   // visibility("hidden") and so on, or NULL

  // Global variable
  bool is_tentative;
  int8_t common;      // attribute common (1) or nocommon (-1), or 0
  bool is_tls;
  bool is_string; // a string literal, which goes in .rodata
  char *init_data;
  int64_t flex_size;  // the bytes an initialized flexible array member
                      // adds after the type's size
  Relocation *rel;

  // C23 constexpr scalar: its value, for use in constant expressions
  bool is_constexpr;
  int64_t constexpr_val;
  long double constexpr_fval;

  // Function
  bool is_inline;
  bool is_noreturn; // _Noreturn, or a C library function like exit()
  Obj *params;
  Node *body;
  Obj *locals;
  Obj *va_area;
  Obj *alloca_bottom;
  int stack_size;
  int nregs;        // callee-saved registers its variables use
  int regs_offset;  // where it saves them in its frame
  int asm_regs;     // x86 registers its asm statements use (bit mask)
  bool is_kept;     // __attribute__((used)): emitted even if never called
  bool is_ctor;     // __attribute__((constructor)): run before main
  bool is_dtor;     // __attribute__((destructor)): run after main
  int ctor_prio;    // their priorities, or -1 for none
  int dtor_prio;

  // Static inline function
  bool is_live;
  bool is_root;
  StringArray refs;
};

// Global variable can be initialized either by a constant expression
// or a pointer to another global variable. This struct represents the
// latter.
typedef struct Relocation Relocation;
struct Relocation {
  Relocation *next;
  int64_t offset;
  char **label;
  long addend;
};

// AST node
typedef enum {
  ND_NULL_EXPR, // Do nothing
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_NEG,       // unary -
  ND_MOD,       // %
  ND_BITAND,    // &
  ND_BITOR,     // |
  ND_BITXOR,    // ^
  ND_SHL,       // <<
  ND_SHR,       // >>
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // =
  ND_COND,      // ?:
  ND_COMMA,     // ,
  ND_MEMBER,    // . (struct member access)
  ND_ADDR,      // unary &
  ND_DEREF,     // unary *
  ND_NOT,       // !
  ND_BITNOT,    // ~
  ND_LOGAND,    // &&
  ND_LOGOR,     // ||
  ND_RETURN,    // "return"
  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_DO,        // "do"
  ND_SWITCH,    // "switch"
  ND_CASE,      // "case"
  ND_BLOCK,     // { ... }
  ND_GOTO,      // "goto"
  ND_GOTO_EXPR, // "goto" labels-as-values
  ND_LABEL,     // Labeled statement
  ND_LABEL_VAL, // [GNU] Labels-as-values
  ND_FUNCALL,   // Function call
  ND_EXPR_STMT, // Expression statement
  ND_STMT_EXPR, // Statement expression
  ND_VAR,       // Variable
  ND_VLA_PTR,   // VLA designator
  ND_NUM,       // Integer
  ND_CAST,      // Type cast
  ND_MEMZERO,   // Zero-clear a stack variable
  ND_ASM,       // "asm"
  ND_COMPLEX,   // __builtin_complex(lhs, rhs), until lower_complex()
  ND_CAS,       // Atomic compare-and-swap
  ND_EXCH,      // Atomic exchange
  ND_UNREACHABLE, // __builtin_unreachable() (C23 unreachable())
  ND_TRAP,        // __builtin_trap()
  ND_CLZ,       // [GNU] __builtin_clz: leading zero bits of lhs
  ND_CTZ,       // [GNU] __builtin_ctz: trailing zero bits of lhs
  ND_POPCOUNT,  // [GNU] __builtin_popcount: one bits in lhs
  ND_BSWAP,     // [GNU] __builtin_bswap16/32/64: lhs's bytes reversed
  ND_FENCE,     // [GNU] __sync_synchronize(): a full memory barrier
  ND_FRAME_ADDR, // [GNU] __builtin_frame_address(val)
  ND_OVERFLOW,  // [GNU] *cas_addr = lhs op rhs (op in val), and if it overflowed
  ND_VA_ARG,    // va_arg(): the next argument's address
  ND_VLA_FREE,  // Free VLAs back to the stack bottom in lhs
} NodeKind;

// An operand of an asm statement: `[name] "constraint" (expr)`. x86
// registers are numbered as the instruction set does: %rax 0, %rcx 1,
// %rdx 2, %rbx 3, %rsp 4, %rbp 5, %rsi 6, %rdi 7, %r8-%r15 8-15.
typedef struct {
  char *name;      // [name], or NULL
  Token *tok;      // the constraint, for errors
  bool is_output;
  bool is_rw;      // '+': an output that is also read
  char kind;       // 'r' register, 'x' SSE register, 'm' memory, 'i' constant
  int reg;         // 'r': its register, 'x': its %xmm number; 'm' through
                   // `addr`: the address's
  Type *ty;        // its type
  Obj *value;      // a temporary holding the input's value, or NULL
  Obj *addr;       // a temporary holding the operand's address, or NULL
  int64_t val;     // 'i': the constant, plus the address of `label`
  char **label;
} AsmOperand;

// A scope inside a function (a block, a for loop, a statement
// expression), for -g: gdb shows its variables only in its code, which
// cgen.c labels .L.block.<id>.begin and .end.
struct LexBlock {
  LexBlock *parent; // or NULL for the function's own scope
  int id;
  bool has_vars;
  bool emitted;     // its code and labels were written
  bool in_dwarf;    // its DIE was written
};

// AST node type
// There is one for each part of every expression and statement, which is
// hundreds of thousands for a big file. So the fields after `rhs` are in
// a union, by the kinds that use them, and a node is only as big as its
// kind needs (see node_size() in parser.c): `a + b` is 48 bytes, `x` 56,
// a statement 208. A pass over every node reads a field in the union only
// for kinds that have it (see has_stmt_fields()).
struct Node {
  NodeKind kind; // Node kind
  bool pass_by_stack; // Function call argument: see below
  bool stack_pad;
  Node *next;    // Next node
  Type *ty;      // Type, e.g. int or pointer to int
  Token *tok;    // Representative token

  Node *lhs;     // Left-hand side
  Node *rhs;     // Right-hand side

  union {
    // Variable: ND_VAR, ND_VLA_PTR, ND_MEMZERO and ND_VA_ARG
    Obj *var;

    // Struct member access
    Member *member;

    // Numeric literal (also ND_FRAME_ADDR's level), atomic
    // compare-and-swap, and ND_OVERFLOW, which uses val and cas_addr
    struct {
      int64_t val;
      long double *fval; // of a floating type
      Node *cas_addr;
      Node *cas_old;
      Node *cas_new;
    };

    // Function call. An argument's `pass_by_stack` (at the top) says it
    // goes on the stack, and `stack_pad`, with 8 bytes of padding before it.
    struct {
      Type *func_ty;
      Node *args;
      Obj *ret_buffer;
    };

    // Statements, and ?:
    struct {
      // "if" or "for" statement, or ?:
      Node *cond;
      Node *then;
      Node *els;
      Node *init;
      Node *inc;

      // "break" and "continue" labels
      char *brk_label;
      char *cont_label;

      // Block or statement expression
      Node *body;

      // A block, for loop or statement expression with a scope of its
      // own: its variables' block, for -g
      LexBlock *block;

      // Goto or labeled statement, or labels-as-values. A goto, break or
      // continue that leaves the scope of variables with a cleanup calls
      // the cleanups (lhs) first.
      char *label;
      char *unique_label;
      Node *goto_next;
      Cleanup *cleanups; // in scope at a goto or label (parser.c)

      // Switch
      Node *case_next;
      Node *default_case;

      union {
        // Case: `case begin ... end:`, and in a switch on __int128, the
        // values' high halves
        struct {
          long begin;
          long end;
          long begin_hi;
          long end_hi;
        };

        // "asm" string literal. With operands (or any ':'), `%` in it
        // refers to them; `body` computes their values and addresses
        // first.
        struct {
          char *asm_str;
          AsmOperand *asm_ops; // outputs, then inputs
          Node *asm_labels;    // asm goto's labels: ND_GOTOs, linked by `next`
          int asm_nops;
          int asm_scratch;     // a register free after the asm, to store outputs
          bool asm_extended;
        };
      };
    };
  };
};

Node *new_cast(Node *expr, Type *ty);
int64_t const_expr(Token **rest, Token *tok);
Obj *parse(Token *tok);
extern StringArray toplevel_asm;
bool is_known_attribute(char *name);
bool is_known_builtin(char *name);

//---------- type.c: types (stage 3) -----------------------------------------

typedef enum {
  TY_VOID,
  TY_BOOL,
  TY_CHAR,
  TY_SHORT,
  TY_INT,
  TY_LONG,
  TY_INT128, // __int128, in %rdx:%rax as a value (see cgen.c)
  TY_FLOAT,
  TY_DOUBLE,
  TY_LDOUBLE,
  TY_ENUM,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
  TY_VLA, // variable-length array
  TY_STRUCT,
  TY_UNION,
  TY_VECTOR, // [GNU] vector_size: in %xmm0 as a value (see cgen.c)
} TypeKind;

struct Type {
  TypeKind kind;
  int64_t size;       // sizeof() value
  int align;          // alignment
  bool is_unsigned;   // unsigned or signed
  bool is_atomic;     // true if _Atomic
  bool is_const;
  bool is_volatile;
  bool is_transparent; // a union with attribute transparent_union
  bool is_distinct;   // signed char or long long: char or long, but not
                      // compatible with them
  bool is_complex;    // _Complex: a TY_STRUCT of the real and imaginary
                      // parts (see complex_type())
  Type *origin;       // for type compatibility check

  // Pointer-to or array-of type. We intentionally use the same member
  // to represent pointer/array duality in C.
  //
  // In many contexts in which a pointer is expected, we examine this
  // member instead of "kind" member to determine whether a type is a
  // pointer or not. That means in many contexts "array of T" is
  // naturally handled as if it were "pointer to T", as required by
  // the C spec.
  Type *base;

  // Declaration
  Token *name;
  Token *name_pos;
  Obj *param_var; // parameter: its variable, which later parameters can use

  // Array, or vector: its number of elements
  int64_t array_len;

  // Vector: its element type. (Not `base`, which makes a type a pointer
  // or an array in many places.)
  Type *elem;

  // Variable-length array
  Node *vla_len; // # of elements
  Obj *vla_size; // sizeof() value

  // Struct
  Token *tag; // struct/union tag, for error messages
  Member *members;
  bool is_flexible;
  bool is_packed;
  uint8_t pack; // #pragma pack: the largest member alignment, or 0
  Type *variants; // qualified copies made while incomplete (see qualified())

  // Enum: its constants, in order (for -Wswitch)
  EnumConst *enum_consts;

  // Function type
  Type *return_ty;
  Type *params;
  bool is_variadic;
  bool is_oldstyle; // `int f()` before C23: any arguments, but not `int f(...)`
  Token *kr_names; // `f(a, b)`, as a K&R definition has: its parameter names
  Type *next;
};

// An enum constant
struct EnumConst {
  EnumConst *next;
  Token *name;
  int64_t val;
};

// Struct member
struct Member {
  Member *next;
  Type *ty;
  Token *tok; // for error message
  Token *name;
  int idx;
  int align;
  int attr_align; // aligned(N) or _Alignas on the member, or 0
  int64_t offset;

  // Bitfield
  bool is_bitfield;
  int bit_offset;
  int bit_width;
  int unit; // the bytes at `offset` it's loaded and stored as
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_schar;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;
extern Type *ty_llong;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;
extern Type *ty_ullong;
extern Type *ty_int128;
extern Type *ty_uint128;

extern Type *ty_float;
extern Type *ty_double;
extern Type *ty_ldouble;

bool is_integer(Type *ty);
bool is_int128(Type *ty);
bool is_complex(Type *ty);
Type *complex_type(Type *part);
Type *complex_part(Type *ty);
bool is_flonum(Type *ty);
bool is_numeric(Type *ty);
bool is_vector(Type *ty);
bool has_ldouble(Type *ty);
bool is_ret_in_memory(Type *ty);
bool has_unaligned_member(Type *ty);
bool is_assignable(Type *to, Node *from);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(Type *ty);
Type *qualified(Type *ty, bool is_const, bool is_volatile);
Type *unqual(Type *ty);
void complete_variants(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int64_t len);
Type *vector_of(Type *elem, int size);
Type *vla_of(Type *base, Node *expr);
Type *enum_type(void);
Type *struct_type(void);
bool has_stmt_fields(NodeKind kind);
void add_type(Node *node);
void check_scalar(Node *node);
char *type_name(Type *ty);
void check_assign(Type *to, Node *from, char *what);

//---------- cgen.c: code generator (stage 4) --------------------------------

void codegen(Obj *prog, FILE *out);
int64_t align_to(int64_t n, int64_t align);
extern bool has_inline_asm;

//---------- asm.c: assembler (stage 5) --------------------------------------

bool assemble_text(char *src, char *path, char **why);
int gp_reg_number(char *name);
char *gp_reg_name(int num, int size);

//---------- ar.c: archiver --------------------------------------------------

int run_ar(int argc, char **argv);
int run_ranlib(int argc, char **argv);

//---------- link.c: static linker (stage 6) ---------------------------------

bool link_static(StringArray *inputs, StringArray *names, StringArray *lib_paths,
                 char *path, bool strip, char *map, char **why);
bool link_relocatable(StringArray *inputs, char *path, char **why);

//---------- unicode.c: UTF-8 helpers ----------------------------------------

int encode_utf8(char *buf, uint32_t c);
uint32_t decode_utf8(char **new_pos, char *p);
bool is_ident1(uint32_t c);
bool is_ident2(uint32_t c);
int display_width(char *p, int len);

//---------- hashmap.c: hash table -------------------------------------------

typedef struct {
  char *key;
  int keylen;
  void *val;
} HashEntry;

typedef struct {
  HashEntry *buckets;
  int capacity;
  int used;
} HashMap;

void *hashmap_get(HashMap *map, char *key);
void *hashmap_get2(HashMap *map, char *key, int keylen);
void hashmap_put(HashMap *map, char *key, void *val);
void hashmap_put2(HashMap *map, char *key, int keylen, void *val);
void hashmap_delete(HashMap *map, char *key);
void hashmap_delete2(HashMap *map, char *key, int keylen);
void hashmap_test(void);

//---------- main.c: driver --------------------------------------------------

bool file_exists(char *path);
FILE *open_input_file(char *path);
bool in_system_header(Token *tok);

extern StringArray include_paths;
extern StringArray iquote_paths;
extern bool opt_MG;
extern bool opt_w;
extern bool opt_g;
extern StringArray opt_warnings;
extern bool opt_fpic;
extern bool opt_asm_cpp;
extern bool opt_fcommon;
extern int opt_std;
extern char *base_file;
