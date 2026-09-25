//============================================================================
// mucc.h - SHARED DECLARATIONS
//
// Every .c file includes this. It declares the data that flows through
// the pipeline: Token (token.c) -> Token (preprocess.c) -> Obj/Node/Type
// (parser.c, type.c) -> assembly (cgen.c).
//============================================================================

//---------- System headers and small utilities ------------------------------

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <libgen.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <strings.h>
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

// Token type
typedef struct Token Token;
struct Token {
  TokenKind kind;   // Token kind
  Token *next;      // Next token
  int64_t val;      // If kind is TK_NUM, its value
  long double fval; // If kind is TK_NUM, its value
  char *loc;        // Token location
  int len;          // Token length
  Type *ty;         // Used if TK_NUM or TK_STR
  char *str;        // String literal contents including terminating '\0'

  File *file;       // Source location
  char *filename;   // Filename
  int line_no;      // Line number
  int line_delta;   // Line number
  bool at_bol;      // True if this token is at beginning of line
  bool has_space;   // True if this token follows a space character
  Hideset *hideset; // For macro expansion
  Token *origin;    // If this is expanded from a macro, the original token
};

// Where the parser resumes after an error (see token.c), and how many
// errors have been reported.
extern jmp_buf *error_recovery;
extern int error_count;

noreturn void error(char *fmt, ...) __attribute__((format(printf, 1, 2)));
noreturn void error_at(char *loc, char *fmt, ...) __attribute__((format(printf, 2, 3)));
noreturn void error_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));
void warn_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));
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

#define unreachable() \
  error("internal error at %s:%d", __FILE__, __LINE__)

//---------- preprocess.c: preprocessor (stage 2) ----------------------------

char *search_include_paths(char *filename);
void init_macros(void);
void define_macro(char *name, char *buf);
void undef_macro(char *name);
Token *preprocess(Token *tok);

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

  // Global variable or function
  bool is_function;
  bool is_definition;
  bool is_static;
  bool is_weak;       // __attribute__((weak))
  char *alias_target; // alias("target"): another name for target
  char *section;      // section("name"), or NULL
  char *visibility;   // visibility("hidden") and so on, or NULL

  // Global variable
  bool is_tentative;
  bool is_tls;
  char *init_data;
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
  int offset;
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
  ND_CAS,       // Atomic compare-and-swap
  ND_EXCH,      // Atomic exchange
  ND_UNREACHABLE, // __builtin_unreachable() (C23 unreachable())
} NodeKind;

// An operand of an asm statement: `[name] "constraint" (expr)`. x86
// registers are numbered as the instruction set does: %rax 0, %rcx 1,
// %rdx 2, %rbx 3, %rsp 4, %rbp 5, %rsi 6, %rdi 7, %r8-%r15 8-15.
typedef struct {
  char *name;      // [name], or NULL
  Token *tok;      // the constraint, for errors
  bool is_output;
  bool is_rw;      // '+': an output that is also read
  char kind;       // 'r' register, 'm' memory, 'i' constant
  int reg;         // 'r': its register; 'm' through `addr`: the address's
  Type *ty;        // its type
  Obj *value;      // a temporary holding the input's value, or NULL
  Obj *addr;       // a temporary holding the operand's address, or NULL
  int64_t val;     // 'i': the constant, plus the address of `label`
  char **label;
} AsmOperand;

// AST node type
struct Node {
  NodeKind kind; // Node kind
  Node *next;    // Next node
  Type *ty;      // Type, e.g. int or pointer to int
  Token *tok;    // Representative token

  Node *lhs;     // Left-hand side
  Node *rhs;     // Right-hand side

  // "if" or "for" statement
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

  // Struct member access
  Member *member;

  // Function call
  Type *func_ty;
  Node *args;
  bool pass_by_stack;
  Obj *ret_buffer;

  // Goto or labeled statement, or labels-as-values. A goto, break or
  // continue that leaves the scope of variables with a cleanup calls the
  // cleanups (lhs) first.
  char *label;
  char *unique_label;
  Node *goto_next;
  Cleanup *cleanups; // in scope at a goto or label (parser.c)

  // Switch
  Node *case_next;
  Node *default_case;

  // Case
  long begin;
  long end;

  // "asm" string literal. With operands (or any ':'), `%` in it refers to
  // them; `body` computes their values and addresses first.
  char *asm_str;
  bool asm_extended;
  AsmOperand *asm_ops; // outputs, then inputs
  int asm_nops;
  int asm_scratch;     // a register free after the asm, to store outputs

  // Atomic compare-and-swap
  Node *cas_addr;
  Node *cas_old;
  Node *cas_new;

  // Atomic op= operators
  Obj *atomic_addr;
  Node *atomic_expr;

  // Variable
  Obj *var;

  // Numeric literal
  int64_t val;
  long double fval;
};

Node *new_cast(Node *expr, Type *ty);
int64_t const_expr(Token **rest, Token *tok);
Obj *parse(Token *tok);
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
} TypeKind;

struct Type {
  TypeKind kind;
  int size;           // sizeof() value
  int align;          // alignment
  bool is_unsigned;   // unsigned or signed
  bool is_atomic;     // true if _Atomic
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

  // Array
  int array_len;

  // Variable-length array
  Node *vla_len; // # of elements
  Obj *vla_size; // sizeof() value

  // Struct
  Token *tag; // struct/union tag, for error messages
  Member *members;
  bool is_flexible;
  bool is_packed;

  // Function type
  Type *return_ty;
  Type *params;
  bool is_variadic;
  Type *next;
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
  int offset;

  // Bitfield
  bool is_bitfield;
  int bit_offset;
  int bit_width;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;

extern Type *ty_float;
extern Type *ty_double;
extern Type *ty_ldouble;

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_numeric(Type *ty);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *vla_of(Type *base, Node *expr);
Type *enum_type(void);
Type *struct_type(void);
void add_type(Node *node);
char *type_name(Type *ty);
void check_assign(Type *to, Node *from, char *what);

//---------- cgen.c: code generator (stage 4) --------------------------------

void codegen(Obj *prog, FILE *out);
int align_to(int n, int align);
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
                 char *path, bool strip, char **why);

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
bool in_system_header(Token *tok);

extern StringArray include_paths;
extern bool opt_w;
extern bool opt_fpic;
extern bool opt_fcommon;
extern int opt_std;
extern char *base_file;
