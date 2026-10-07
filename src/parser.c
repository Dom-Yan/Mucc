//============================================================================
// parser.c - STAGE 3 of 6: PARSE
//
// Turns tokens into a typed syntax tree (AST): a list of functions and
// global variables. Type rules live in type.c.
//============================================================================

// This file contains a recursive descent parser for C.
//
// Most functions in this file are named after the symbols they are
// supposed to read from an input token list. For example, stmt() is
// responsible for reading a statement from a token list. The function
// then construct an AST node representing a statement.
//
// Each function conceptually returns two values, an AST node and
// remaining part of the input tokens. Since C doesn't support
// multiple return values, the remaining tokens are returned to the
// caller via a pointer argument.
//
// Input tokens are represented by a linked list. Unlike many recursive
// descent parsers, we don't have the notion of the "input token stream".
// Most parsing functions don't change the global state of the parser.
// So it is very easy to lookahead arbitrary number of tokens in this
// parser.
//
// The sections run in this order: the parser's state and helpers,
// attributes, declarations and types, initializers, statements (asm
// statements first), then expressions from the lowest precedence (comma,
// assignment) to the highest (postfix, GNU builtins, primary), with
// constant evaluation and struct and union declarations among them,
// then warnings, and last the top level (functions, global variables,
// parse()).

#include "mucc.h"

//---------- Parser state ----------------------------------------------------

// Scope for local variables, global variables, typedefs
// or enum constants
typedef struct {
  Obj *var;
  Type *type_def;
  Type *enum_ty; // an enum constant's type: int, or `enum E : type`'s
  int64_t enum_val;
} VarScope;

// Represents a block scope.
typedef struct Scope Scope;
struct Scope {
  Scope *next;

  // C has two block scopes; one is for variables/typedefs and
  // the other is for struct/union/enum tags.
  HashMap vars;
  HashMap tags;
};

// The GNU attributes (`__attribute__((...))`) mucc acts on. The many that
// are only hints are accepted and ignored; see apply_attribute().
typedef struct {
  bool is_noreturn;
  bool is_unused;
  bool is_packed;
  bool is_used;
  int align;         // aligned(N), or 0
  Token *layout_tok; // the first `packed` or `aligned`, for errors
  Token *weak_tok;   // `weak`, or NULL
  Token *ctor_tok;   // `constructor`, or NULL
  Token *dtor_tok;   // `destructor`, or NULL
  int ctor_prio;     // their priorities, or -1 for none
  int dtor_prio;
  Token *cleanup_tok; // cleanup(fn), or NULL
  Obj *cleanup_fn;
  Token *alias_tok;   // alias("target"), or NULL
  char *alias_target;
  Token *section_tok; // section("name"), or NULL
  char *section;
  Token *vis_tok;     // visibility("hidden") and so on, or NULL
  char *visibility;
  Token *gnu_inline_tok;
  Token *asm_label_tok; // `asm("name")` after a declarator, or NULL
  char *asm_label;
  Token *mode_tok;      // the name in mode(name), or NULL
  Token *vector_tok;    // vector_size(N), or NULL
  int vector_size;      // its N
  Token *transparent_tok; // transparent_union, or NULL
  int8_t common;          // common (1) or nocommon (-1), or 0
  Token *common_tok;
} Attrs;

// Variable attributes such as typedef or extern.
typedef struct {
  bool is_typedef;
  bool is_static;
  bool is_extern;
  bool is_inline;
  bool is_tls;
  bool is_noreturn;
  bool is_constexpr;
  bool is_register;
  bool is_unused; // __attribute__((unused)): no unused-variable warning
  int align;
  Attrs gnu;      // all GNU attributes in the declaration specifiers
} VarAttr;

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
typedef struct EmbedRun EmbedRun;
struct EmbedRun {
  EmbedRun *next;
  int64_t idx; // the first element it sets
  Token *tok; // the TK_EMBED token
};

typedef struct Initializer Initializer;
struct Initializer {
  Initializer *next;
  Type *ty;
  Token *tok;
  bool is_flexible;

  // If it's not an aggregate type and has an initializer,
  // `expr` has an initialization expression.
  Node *expr;

  // If it's an initializer for an aggregate type (e.g. array or struct),
  // `children` has initializers for its children, made when one is set
  // (see child_init()): elements nothing sets have none, and are zero.
  Initializer **children;

  // A string literal that sets a char or wide char array's elements, as
  // its bytes: children set after it override some of them.
  Token *str;

  // Runs of an integer array's elements that #embed's bytes set, in
  // order, after `str` and before children (see embed_init()).
  EmbedRun *embeds;

  // Only one member can be initialized for a union.
  // `mem` is used to clarify which member is initialized.
  Member *mem;
};

// For local variable initializer.
typedef struct InitDesg InitDesg;
struct InitDesg {
  InitDesg *next;
  int idx;
  Member *member;
  Obj *var;
};

// All local variable instances created during parsing are
// accumulated to this list.
static Obj *locals;

// The scope new locals are in, inside the function's own (see LexBlock),
// and whether the next compound_stmt() is a function's body, which is the
// function's own scope
static LexBlock *current_block;
static bool is_fn_body;

// Likewise, global variables are accumulated to this list.
static Obj *globals;

static Scope *scope = &(Scope){};

// Points to the function object the parser is currently parsing.
static Obj *current_fn;

// Lists of all goto statements and labels in the curent function.
static Node *gotos;
static Node *labels;

// Current "goto" and "continue" jump targets.
static char *brk_label;
static char *cont_label;

// Points to a node representing a switch if we are parsing
// a switch statement. Otherwise, NULL.
static Node *current_switch;

// Local variables with __attribute__((cleanup(fn))) in scope, innermost
// first, and those in scope where break, continue and the current
// switch's cases jump to. Leaving a variable's scope calls fn(&var).
//
// A variable-length array is one too, with no `fn`: `var` holds the
// stack bottom from before it was allocated, and leaving its scope
// frees it back to there, so a VLA in a loop doesn't use up the stack.
struct Cleanup {
  Cleanup *next;
  Obj *var;
  Obj *fn;
};

static Cleanup *cleanups;
static Cleanup *brk_cleanups;
static Cleanup *cont_cleanups;
static Cleanup *case_cleanups;

StringArray toplevel_asm; // asm("...") at file scope, in order

static Obj *builtin_alloca;
static Type *va_elem_ty; // an element of __builtin_va_list

// declspec() returns this for `auto` with no other type, as in C23's
// `auto x = 1;`. declaration() and global_variable() then take the type
// from the initializer. Anywhere else it acts as the old implicit int.
static Type auto_type = {TY_INT, 4, 4};

//---------- Forward declarations --------------------------------------------

static bool is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename(Token **rest, Token *tok);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *typeof_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty, Attrs *attrs);
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int64_t i);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem,
                                bool after_comma);
static void initializer2(Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static void gvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok, bool is_stmt_expr);
static Node *block_item(Token **rest, Token *tok);
static Node *label_body(Token **rest, Token *tok);
static Token *static_assertion(Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static int64_t eval(Node *node);
static void eval_complex(Node *node, long double *re, long double *im);
static Node *lower_complex(Node *node);
static Token *top_level_item(Token *tok);
static int64_t eval2(Node *node, char ***label);
static int64_t eval_rval(Node *node, char ***label);
static bool is_const_expr(Node *node);
static bool is_const_int128(Node *node);
static __int128 eval128(Node *node);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static long double eval_double(Node *node);
static long double to_flonum(Type *ty, long double val);
static Node *conditional(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Member *get_struct_member(Type *ty, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok, Node *node);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety, VarAttr *attr);
static Type *mode_type(Type *ty, Token *tok);
static Type *vector_type(Type *ty, Attrs *a);
static Node *vector_elem(Node *vec, Node *idx, Token *tok);
static bool is_function(Token *tok, Type *basety);
static bool falls_through(Node *node);
static Token *function(Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(Token *tok, Type *basety, VarAttr *attr);
static void warn_unused_value(Node *stmt);
static void warn_assign_in_condition(Node *cond, Token *start);
static void warn_address_condition(Node *cond);
static void warn_string_compare(Node *lhs, Node *rhs, Token *tok);
static void warn_return_local(Node *exp);
static void warn_div_by_zero(Node *lhs, Node *rhs, Token *tok);
static void warn_shift_count(Node *lhs, Node *rhs, Token *tok);
static void warn_format(Obj *fn, Node *args);
static void warn_switch(Node *sw);

//---------- Scopes and name lookup ------------------------------------------

static int64_t align_down(int64_t n, int64_t align) {
  return align_to(n - align + 1, align);
}

static void enter_scope(void) {
  Scope *sc = arena_alloc(sizeof(Scope));
  sc->next = scope;
  scope = sc;
}

static void leave_scope(void) {
  scope = scope->next;
}

// Find a variable by name.
static VarScope *find_var(Token *tok) {
  for (Scope *sc = scope; sc; sc = sc->next) {
    VarScope *sc2 = hashmap_get2(&sc->vars, tok->loc, tok->len);
    if (sc2)
      return sc2;
  }
  return NULL;
}

static Type *find_tag(Token *tok) {
  for (Scope *sc = scope; sc; sc = sc->next) {
    Type *ty = hashmap_get2(&sc->tags, tok->loc, tok->len);
    if (ty)
      return ty;
  }
  return NULL;
}

//---------- Error recovery --------------------------------------------------

// On an error, error_tok() jumps back to the statement or declaration
// being parsed (see token.c). That item is skipped and parsing goes on,
// so one run reports every independent error. These are the globals a
// half-parsed item can leave changed; they're saved before each item.
typedef struct {
  Scope *scope;
  Obj *current_fn;
  Node *gotos;
  Node *labels;
  char *brk_label;
  char *cont_label;
  Node *current_switch;
  Cleanup *cleanups;
  Cleanup *brk_cleanups;
  Cleanup *cont_cleanups;
  Cleanup *case_cleanups;
} ParserState;

static ParserState save_state(void) {
  return (ParserState){scope, current_fn, gotos, labels,
                       brk_label, cont_label, current_switch,
                       cleanups, brk_cleanups, cont_cleanups, case_cleanups};
}

static void restore_state(ParserState s) {
  scope = s.scope;
  current_fn = s.current_fn;
  gotos = s.gotos;
  labels = s.labels;
  brk_label = s.brk_label;
  cont_label = s.cont_label;
  current_switch = s.current_switch;
  cleanups = s.cleanups;
  brk_cleanups = s.brk_cleanups;
  cont_cleanups = s.cont_cleanups;
  case_cleanups = s.case_cleanups;
}

// Returns the token after the statement or declaration that starts at
// `tok`, found by bracket matching alone: past its `;`, or past the `}`
// of a body such as `if (x) {...}` or a function's. A `)`, `]` or `}`
// that closes something opened before `tok` is left for the caller.
static Token *skip_bad_item(Token *tok) {
  Token *start = tok;
  Token *prev = NULL;
  int depth = 0;
  bool is_body = false; // Is the outermost `{` a statement body?

  for (; tok->kind != TK_EOF; prev = tok, tok = tok->next) {
    if (equal(tok, "(") || equal(tok, "[")) {
      depth++;
      continue;
    }

    // `{` after `)`, `else` or `do`, or starting the item, opens a
    // statement body; otherwise it's a struct or an initializer, and
    // the item goes on to a `;`.
    if (equal(tok, "{")) {
      if (depth == 0)
        is_body = !prev || equal(prev, ")") || equal(prev, "else") ||
                  equal(prev, "do");
      depth++;
      continue;
    }

    if (equal(tok, ")") || equal(tok, "]") || equal(tok, "}")) {
      if (depth == 0)
        break;
      depth--;
      if (depth == 0 && is_body && equal(tok, "}") &&
          !equal(tok->next, "else") && !equal(tok->next, "while"))
        return tok->next;
      continue;
    }

    if (depth == 0 && equal(tok, ";") && !equal(tok->next, "else"))
      return tok->next;
  }

  // Always move forward, or the caller would retry the same token.
  return (tok == start && tok->kind != TK_EOF) ? tok->next : tok;
}

//---------- AST node constructors -------------------------------------------

// The bytes a node of `kind` takes: the fields before Node's union, and
// those of the union's part for its kind. A node's kind never changes,
// and a whole Node is never copied.
static int node_size(NodeKind kind) {
  switch (kind) {
  case ND_VAR: case ND_VLA_PTR: case ND_MEMZERO: case ND_VA_ARG:
    return offsetof(Node, var) + sizeof(Obj *);
  case ND_MEMBER:
    return offsetof(Node, member) + sizeof(Member *);
  case ND_NUM: case ND_FRAME_ADDR:
    return offsetof(Node, fval) + sizeof(long double *);
  case ND_CAS: case ND_OVERFLOW:
    return offsetof(Node, cas_new) + sizeof(Node *);
  case ND_FUNCALL:
    return offsetof(Node, ret_buffer) + sizeof(Obj *);
  }
  return has_stmt_fields(kind) ? sizeof(Node) : offsetof(Node, var);
}

static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = arena_alloc(node_size(kind));
  node->kind = kind;
  node->tok = tok;
  return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = expr;
  return node;
}

static Node *new_num(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  return node;
}

static Node *new_long(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty_long;
  return node;
}

static Node *new_ulong(long val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty_ulong;
  return node;
}

static Node *new_var_node(Obj *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

static Node *new_vla_ptr(Obj *var, Token *tok) {
  Node *node = new_node(ND_VLA_PTR, tok);
  node->var = var;
  return node;
}

Node *new_cast(Node *expr, Type *ty) {
  add_type(expr);

  // Nothing converts to or from a struct or union, except to void. A
  // complex number converts to and from any number.
  Type *from = expr->ty;
  bool from_aggr = from && (from->kind == TY_STRUCT || from->kind == TY_UNION);
  bool to_aggr = ty->kind == TY_STRUCT || ty->kind == TY_UNION;
  if (from && ty->kind != TY_VOID && (is_complex(from) || is_complex(ty))) {
    Type *other = is_complex(from) ? ty : from;
    from_aggr = to_aggr = false;
    if (!is_numeric(other) && !is_complex(other))
      from_aggr = true;
  }
  if (from && ty->kind != TY_VOID && from_aggr != to_aggr)
    error_tok(expr->tok, "cannot convert '%s' to '%s'", type_name(from), type_name(ty));

  Node *node = new_node(ND_CAST, expr->tok);
  node->lhs = expr;
  node->ty = ty;
  return node;
}

//---------- Variables, globals and string literals --------------------------

static VarScope *push_scope(char *name) {
  VarScope *sc = arena_alloc(sizeof(VarScope));
  hashmap_put(&scope->vars, name, sc);
  return sc;
}

// An initializer for type `ty`. With `is_flexible`, an array of unknown
// length takes its length from the initializer, as does a struct's
// flexible array member.
static Initializer *new_initializer(Type *ty, bool is_flexible) {
  Initializer *init = arena_alloc(sizeof(Initializer));
  init->ty = ty;
  if (ty->kind == TY_ARRAY)
    init->is_flexible = is_flexible && ty->size < 0;
  else if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    init->is_flexible = is_flexible && ty->is_flexible;
  return init;
}

// The initializer of element `i` of an array, or of the member with index
// `i` (mem->idx) of a struct or union, made when first needed. `ty` is
// its type; `flex` is for a flexible struct's flexible array member.
static Initializer *child_init(Initializer *init, int64_t i, Type *ty, bool flex) {
  if (!init->children) {
    int64_t len = init->ty->array_len;
    if (init->ty->kind != TY_ARRAY) {
      len = 0;
      for (Member *mem = init->ty->members; mem; mem = mem->next)
        len++;
    }
    init->children = calloc(len, sizeof(Initializer *));
    if (!init->children)
      error("out of memory for the initializer of an array of %ld elements", len);
  }

  if (!init->children[i]) {
    init->children[i] = new_initializer(ty, false);
    init->children[i]->is_flexible = flex;
  }
  return init->children[i];
}

static Initializer *elem_init(Initializer *init, int64_t i) {
  return child_init(init, i, init->ty->base, false);
}

static Initializer *mem_init(Initializer *init, Member *mem) {
  return child_init(init, mem->idx, mem->ty, init->is_flexible && !mem->next);
}

// The child that sets element or member `i`, or NULL if nothing does.
static Initializer *get_child(Initializer *init, int64_t i) {
  return init->children ? init->children[i] : NULL;
}

static Obj *new_var(char *name, Type *ty) {
  Obj *var = arena_alloc(sizeof(Obj));
  var->name = name;
  var->ty = ty;
  var->align = ty->align;
  push_scope(name)->var = var;
  return var;
}

static Obj *new_lvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  var->block = current_block;
  if (current_block && name[0])
    current_block->has_vars = true;
  return var;
}

// A scope of its own inside a function, for `node` (see LexBlock): the
// new current_block, which the caller sets back to the old one after.
static LexBlock *enter_block(Node *node) {
  static int last_id;
  LexBlock *b = arena_alloc(sizeof(LexBlock));
  b->parent = current_block;
  b->id = ++last_id;
  node->block = current_block = b;
  return b;
}

static Obj *new_gvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->next = globals;
  var->is_static = true;
  var->is_definition = true;
  globals = var;
  return var;
}

static char *new_unique_name(void) {
  static int id = 0;
  return format(".L..%d", id++);
}

static Obj *new_anon_gvar(Type *ty) {
  return new_gvar(new_unique_name(), ty);
}

static Obj *new_string_literal(char *p, Type *ty) {
  Obj *var = new_anon_gvar(ty);
  var->init_data = p;
  var->is_string = true;
  return var;
}

static char *get_ident(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected an identifier");
  return strndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok) {
  if (tok->kind == TK_IDENT) {
    VarScope *sc = find_var(tok);
    if (sc)
      return sc->type_def;
  }
  return NULL;
}

static void push_tag_scope(Token *tok, Type *ty) {
  hashmap_put2(&scope->tags, tok->loc, tok->len, ty);
}

//---------- GNU attributes --------------------------------------------------

// Attributes that are only hints: ignoring them can't change what a
// program does. (`unused` also turns off the unused-variable warning.)
static char *ignored_attributes[] = {
  "unused", "maybe_unused", "format", "format_arg", "nonnull",
  "returns_nonnull", "sentinel", "warn_unused_result", "nodiscard",
  "deprecated", "unavailable", "pure", "const", "malloc", "alloc_size",
  "alloc_align", "assume_aligned", "nothrow", "leaf", "noinline", "noclone",
  "noipa", "always_inline", "hot", "cold", "artificial", "flatten",
  "may_alias", "fallthrough", "no_sanitize", "no_sanitize_address",
  "no_sanitize_thread", "no_sanitize_undefined", "no_address_safety_analysis",
  "no_instrument_function", "no_profile_instrument_function",
  "no_stack_protector", "no_split_stack", "stack_protect", "no_icf",
  "no_reorder", "noplt", "optimize", "returns_twice", "access", "nonstring",
  "designated_init", "externally_visible", "counted_by", "error", "warning",
  "tls_model", "warn_if_not_aligned", "null_terminated_string_arg",
  "nonnull_if_nonzero", "fd_arg", "fd_arg_read", "fd_arg_write",
  "zero_call_used_regs", "patchable_function_entry", "strict_flex_array",
  "expected_throw", "flag_enum", "uninitialized", "assume", "musttail",
};

// Attributes gcc has that would change what a program does, which mucc
// doesn't implement (yet): they are errors, never silently ignored.
static char *unsupported_attributes[] = {
  "retain", "weakref", "ifunc", "naked", "target",
  "target_clones", "copy",
  "symver", "scalar_storage_order", "noinit", "persistent", "hardbool",
};

static bool in_list(char *name, char **list, int len) {
  for (int i = 0; i < len; i++)
    if (!strcmp(name, list[i]))
      return true;
  return false;
}

static bool is_attribute(Token *tok) {
  return equal(tok, "__attribute__") || equal(tok, "__attribute");
}

// `__packed__` and `packed` are the same attribute.
static char *attribute_name(Token *tok) {
  if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
    error_expected(tok, "an attribute name");
  char *s = strndup(tok->loc, tok->len);
  int n = tok->len;
  if (n > 4 && !strncmp(s, "__", 2) && !strcmp(s + n - 2, "__")) {
    s[n - 2] = '\0';
    return s + 2;
  }
  return s;
}

// The attributes apply_attribute() acts on, besides the ignored ones.
static char *implemented_attributes[] = {
  "noreturn", "used", "weak", "packed", "aligned", "constructor",
  "destructor", "cleanup", "alias", "section", "visibility", "gnu_inline",
  "mode", "transparent_union", "common", "nocommon", "vector_size",
};

// __has_attribute(name) in the preprocessor: does mucc accept attribute
// `name` (in either spelling), acting on it or ignoring it as harmless?
bool is_known_attribute(char *name) {
  int n = strlen(name);
  if (n > 4 && !strncmp(name, "__", 2) && !strcmp(name + n - 2, "__"))
    name = strndup(name + 2, n - 4);
  return in_list(name, implemented_attributes,
                 sizeof(implemented_attributes) / sizeof(*implemented_attributes)) ||
         in_list(name, ignored_attributes,
                 sizeof(ignored_attributes) / sizeof(*ignored_attributes));
}

// `packed`, `aligned`, `weak`, `constructor` and the like change a layout
// or a symbol, so they are only allowed where the caller applies them
// (`allow_decl`); anywhere else, they're errors.
static void apply_attribute(Token *tok, Token *args, Attrs *a, bool allow_decl) {
  char *name = attribute_name(tok);

  if (!strcmp(name, "noreturn")) {
    a->is_noreturn = true;
    return;
  }
  if (!strcmp(name, "unused") || !strcmp(name, "maybe_unused"))
    a->is_unused = true;

  // Only a static inline function can be left out, and `used` keeps it.
  // Anywhere else it changes nothing.
  if (!strcmp(name, "used")) {
    a->is_used = true;
    return;
  }

  if (!strcmp(name, "constructor") || !strcmp(name, "destructor")) {
    if (!allow_decl)
      error_tok(tok, "attribute '%s' is not supported here", name);
    int prio = -1;
    if (args) {
      prio = const_expr(&args, args);
      skip(args, ")");
      if (prio < 0 || prio > 65535)
        error_tok(tok, "%s priorities must be from 0 to 65535", name);
    }
    if (name[0] == 'c') {
      a->ctor_tok = tok;
      a->ctor_prio = prio;
    } else {
      a->dtor_tok = tok;
      a->dtor_prio = prio;
    }
    return;
  }

  if (!strcmp(name, "alias") || !strcmp(name, "section") ||
      !strcmp(name, "visibility")) {
    if (!allow_decl)
      error_tok(tok, "attribute '%s' is not supported here", name);
    if (!args || args->kind != TK_STR)
      error_tok(tok, "attribute '%s' needs a string", name);
    skip(args->next, ")");
    char *s = args->str;

    if (name[0] == 'a') {
      a->alias_tok = tok;
      a->alias_target = s;
    } else if (name[0] == 's') {
      a->section_tok = tok;
      a->section = s;
    } else {
      if (strcmp(s, "default") && strcmp(s, "hidden") &&
          strcmp(s, "protected") && strcmp(s, "internal"))
        error_tok(args, "visibility must be default, hidden, protected or internal");
      a->vis_tok = tok;
      a->visibility = s;
    }
    return;
  }

  // mode(QI) and the like: an integer or floating type of a given size,
  // as glibc's <sys/types.h> makes int8_t. See mode_type().
  if (!strcmp(name, "mode")) {
    if (!args || args->kind != TK_IDENT)
      error_tok(tok, "attribute 'mode' needs a mode name");
    skip(args->next, ")");
    a->mode_tok = args;
    return;
  }

  // vector_size(N): a vector of N bytes of the declared type. See
  // vector_type().
  if (!strcmp(name, "vector_size")) {
    if (!args)
      error_tok(tok, "attribute 'vector_size' needs a size");
    a->vector_size = const_expr(&args, args);
    skip(args, ")");
    a->vector_tok = tok;
    return;
  }

  // On a global: as -fcommon or -fno-common, for it alone
  if (!strcmp(name, "common") || !strcmp(name, "nocommon")) {
    if (!allow_decl)
      error_tok(tok, "attribute '%s' is not supported here", name);
    a->common = name[0] == 'c' ? 1 : -1;
    a->common_tok = tok;
    return;
  }

  // On a union typedef: see parse_typedef() and funcall().
  if (!strcmp(name, "transparent_union")) {
    a->transparent_tok = tok;
    return;
  }

  // With gcc's meaning of `extern inline`: see function().
  if (!strcmp(name, "gnu_inline")) {
    a->gnu_inline_tok = tok;
    return;
  }

  if (!strcmp(name, "cleanup")) {
    if (!allow_decl)
      error_tok(tok, "attribute 'cleanup' is not supported here");
    if (!args || args->kind != TK_IDENT)
      error_tok(tok, "attribute 'cleanup' needs a function name");
    VarScope *sc = find_var(args);
    if (!sc || !sc->var || !sc->var->is_function)
      error_tok(args, "'%.*s' is not a function", args->len, args->loc);
    skip(args->next, ")");
    a->cleanup_tok = tok;
    a->cleanup_fn = sc->var;
    return;
  }

  if (!strcmp(name, "weak")) {
    if (!allow_decl)
      error_tok(tok, "attribute 'weak' is not supported here");
    if (!a->weak_tok)
      a->weak_tok = tok;
    return;
  }

  if (!strcmp(name, "packed") || !strcmp(name, "aligned")) {
    if (!allow_decl)
      error_tok(tok, "attribute '%s' is not supported here", name);
    if (!a->layout_tok)
      a->layout_tok = tok;
    if (!strcmp(name, "packed")) {
      a->is_packed = true;
      return;
    }

    // Without an argument, the largest alignment any type needs.
    int64_t align = 16;
    if (args) {
      align = const_expr(&args, args);
      skip(args, ")");
    }
    if (align <= 0 || (align & (align - 1)) || align > (1 << 28))
      error_tok(tok, "requested alignment is not a positive power of 2");
    a->align = MAX(a->align, align);
    return;
  }

  if (in_list(name, ignored_attributes,
              sizeof(ignored_attributes) / sizeof(*ignored_attributes)))
    return;
  if (in_list(name, unsupported_attributes,
              sizeof(unsupported_attributes) / sizeof(*unsupported_attributes)))
    error_tok(tok, "attribute '%s' is not supported", name);
  error_tok(tok, "unknown attribute '%s'", name);
}

static void merge_attrs(Attrs *dst, Attrs *src) {
  if (src->mode_tok)
    dst->mode_tok = src->mode_tok;
  if (src->vector_tok) {
    dst->vector_tok = src->vector_tok;
    dst->vector_size = src->vector_size;
  }
  if (src->common) {
    dst->common = src->common;
    dst->common_tok = src->common_tok;
  }
  dst->is_noreturn |= src->is_noreturn;
  dst->is_unused |= src->is_unused;
  dst->is_packed |= src->is_packed;
  dst->is_used |= src->is_used;
  dst->align = MAX(dst->align, src->align);
  if (!dst->layout_tok)
    dst->layout_tok = src->layout_tok;
  if (!dst->weak_tok)
    dst->weak_tok = src->weak_tok;
  if (src->ctor_tok) {
    dst->ctor_tok = src->ctor_tok;
    dst->ctor_prio = src->ctor_prio;
  }
  if (src->dtor_tok) {
    dst->dtor_tok = src->dtor_tok;
    dst->dtor_prio = src->dtor_prio;
  }
  if (src->cleanup_tok) {
    dst->cleanup_tok = src->cleanup_tok;
    dst->cleanup_fn = src->cleanup_fn;
  }
  if (src->alias_tok) {
    dst->alias_tok = src->alias_tok;
    dst->alias_target = src->alias_target;
  }
  if (src->section_tok) {
    dst->section_tok = src->section_tok;
    dst->section = src->section;
  }
  if (src->vis_tok) {
    dst->vis_tok = src->vis_tok;
    dst->visibility = src->visibility;
  }
  if (src->gnu_inline_tok)
    dst->gnu_inline_tok = src->gnu_inline_tok;
  if (src->asm_label_tok) {
    dst->asm_label_tok = src->asm_label_tok;
    dst->asm_label = src->asm_label;
  }
}

// Reports the first of `toks` that is set: an attribute that can't be
// applied to `what`.
static void not_on(Token **toks, int n, char *what) {
  for (int i = 0; i < n; i++)
    if (toks[i])
      error_tok(toks[i], "attribute '%s' is not supported on %s",
                attribute_name(toks[i]), what);
}

// For declarations that aren't functions: `constructor`, `destructor`
// and `gnu_inline` can't be applied.
static void no_fn_attrs(Attrs *a, char *what) {
  Token *toks[] = {a->ctor_tok, a->dtor_tok, a->gnu_inline_tok};
  not_on(toks, 3, what);
}

// For declarations that aren't a function or a global variable: those
// that name a symbol can't be.
static void no_global_attrs(Attrs *a, char *what) {
  Token *toks[] = {a->weak_tok, a->alias_tok, a->section_tok, a->vis_tok, a->common_tok};
  not_on(toks, 5, what);
}

// An asm label names a function's or a global's symbol, or a register
// variable's register. Elsewhere, it's an error.
static void no_asm_label(Attrs *a, char *what) {
  if (a->asm_label_tok)
    error_tok(a->asm_label_tok, "an asm label is not supported on %s", what);
}

// For declarations that aren't a local variable.
static void no_cleanup(Attrs *a, char *what) {
  if (a->cleanup_tok)
    error_tok(a->cleanup_tok, "attribute 'cleanup' is not supported on %s", what);
}

// For declarations that aren't variables or functions (a typedef, a
// struct member, a type, a parameter): none of those can be applied.
static void no_symbol_attrs(Attrs *a, char *what) {
  no_global_attrs(a, what);
  no_fn_attrs(a, what);
  no_cleanup(a, what);
  no_asm_label(a, what);
}

// For declarations where no `packed`, `aligned`, `weak` and so on can be
// applied.
static void no_decl_attrs(Attrs *a) {
  if (a->layout_tok)
    error_tok(a->layout_tok, "attribute '%s' is not supported here",
              attribute_name(a->layout_tok));
  no_symbol_attrs(a, "a parameter");
}

// attributes = (("__attribute__" | "__attribute") "(" "(" attr-list ")" ")")*
// attr-list  = attribute? ("," attribute?)*
// attribute  = name ("(" balanced-tokens ")")?
static Token *attributes(Token *tok, Attrs *a, bool allow_decl) {
  while (is_attribute(tok)) {
    tok = skip(tok->next, "(");
    tok = skip(tok, "(");

    while (!equal(tok, ")")) {
      if (consume(&tok, tok, ","))
        continue;

      if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
        error_expected(tok, "an attribute name");
      Token *name = tok;
      Token *args = NULL;
      tok = tok->next;
      if (equal(tok, "(")) {
        args = tok->next;
        for (int depth = 0; tok->kind != TK_EOF; tok = tok->next) {
          if (equal(tok, "("))
            depth++;
          else if (equal(tok, ")") && --depth == 0)
            break;
        }
        tok = skip(tok, ")");
      }
      apply_attribute(name, args, a, allow_decl);

      if (!equal(tok, ",") && !equal(tok, ")"))
        error_expected(tok, "',' or ')'");
    }
    tok = skip(tok, ")");
    tok = skip(tok, ")");
  }
  return tok;
}

// Attributes that can't change anything where they are, like those on a
// pointer or after a label, are still checked.
static Token *skip_attributes(Token *tok) {
  Attrs a = {};
  return attributes(tok, &a, false);
}

//---------- Declarations: specifiers and declarators ------------------------

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern" | "inline"
//             | "_Thread_local" | "__thread"
//             | "signed" | "unsigned"
//             | struct-decl | union-decl | typedef-name
//             | enum-specifier | typeof-specifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn" | attributes)+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
  // We use a single integer as counters for all typenames.
  // For example, bits 0 and 1 represents how many times we saw the
  // keyword "void" so far. With this, we can use a switch statement
  // as you can see below.
  enum {
    VOID     = 1 << 0,
    BOOL     = 1 << 2,
    CHAR     = 1 << 4,
    SHORT    = 1 << 6,
    INT      = 1 << 8,
    LONG     = 1 << 10,
    FLOAT    = 1 << 12,
    DOUBLE   = 1 << 14,
    OTHER    = 1 << 16,
    SIGNED   = 1 << 17,
    UNSIGNED = 1 << 18,
    INT128   = 1 << 19,
    COMPLEX  = 1 << 20,
  };

  Type *ty = ty_int;
  int counter = 0;
  bool is_atomic = false;
  bool is_const = false;
  bool is_volatile = false;
  bool is_auto = false;

  while (is_typename(tok)) {
    // Without `attr` (a cast, a parameter), nothing applies `aligned`.
    if (is_attribute(tok)) {
      Attrs a = {};
      tok = attributes(tok, &a, attr != NULL);
      if (!attr && a.vector_tok)
        error_tok(a.vector_tok, "attribute 'vector_size' is only supported in a declaration");
      if (attr) {
        attr->is_noreturn |= a.is_noreturn;
        attr->is_unused |= a.is_unused;
        merge_attrs(&attr->gnu, &a);
      }
      continue;
    }

    // Handle storage class specifiers.
    if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern") ||
        equal(tok, "inline") || equal(tok, "_Thread_local") ||
        equal(tok, "__thread") || equal(tok, "constexpr")) {
      if (!attr)
        error_tok(tok, "storage class specifier is not allowed in this context");

      if (equal(tok, "typedef"))
        attr->is_typedef = true;
      else if (equal(tok, "static"))
        attr->is_static = true;
      else if (equal(tok, "extern"))
        attr->is_extern = true;
      else if (equal(tok, "inline"))
        attr->is_inline = true;
      else if (equal(tok, "constexpr"))
        attr->is_constexpr = true;
      else
        attr->is_tls = true;

      if (attr->is_typedef &&
          attr->is_static + attr->is_extern + attr->is_inline + attr->is_tls > 1)
        error_tok(tok, "typedef may not be used together with static,"
                  " extern, inline, __thread or _Thread_local");
      if (attr->is_static + attr->is_extern + attr->is_register > 1)
        error_tok(tok, "multiple storage classes in declaration specifiers");
      tok = tok->next;
      continue;
    }

    // `register` matters with an asm label: `register long x asm("r10")`
    // puts x in that register for asm statements. And x has no address.
    if (equal(tok, "register")) {
      if (attr)
        attr->is_register = true;
      if (attr && attr->is_static + attr->is_extern + attr->is_register > 1)
        error_tok(tok, "multiple storage classes in declaration specifiers");
      tok = tok->next;
      continue;
    }

    if (consume(&tok, tok, "const")) {
      is_const = true;
      continue;
    }
    if (consume(&tok, tok, "volatile")) {
      is_volatile = true;
      continue;
    }

    // These keywords are recognized but ignored.
    if (consume(&tok, tok, "restrict") ||
        consume(&tok, tok, "__restrict") || consume(&tok, tok, "__restrict__"))
      continue;

    // With a type, `auto` is the old storage class and means nothing.
    // Alone, it's C23 type inference (see auto_type), as gcc's
    // __auto_type is.
    if (equal(tok, "auto") || equal(tok, "__auto_type")) {
      is_auto = true;
      tok = tok->next;
      continue;
    }

    // _Noreturn only matters for the missing-return warning.
    if (equal(tok, "_Noreturn")) {
      if (attr)
        attr->is_noreturn = true;
      tok = tok->next;
      continue;
    }

    if (equal(tok, "_Atomic")) {
      tok = tok->next;
      if (equal(tok , "(")) {
        ty = typename(&tok, tok->next);
        tok = skip(tok, ")");
      }
      is_atomic = true;
      continue;
    }

    if (equal(tok, "_Alignas")) {
      if (!attr)
        error_tok(tok, "_Alignas is not allowed in this context");
      tok = skip(tok->next, "(");

      if (is_typename(tok)) {
        attr->align = typename(&tok, tok)->align;
      } else {
        // 0 asks for nothing; otherwise a power of 2, as gcc allows.
        Token *start = tok;
        int64_t align = const_expr(&tok, tok);
        if (align < 0 || (align & (align - 1)) || align > (1 << 28))
          error_tok(start, "requested alignment is not a positive power of 2");
        attr->align = align;
      }
      tok = skip(tok, ")");
      continue;
    }

    // Handle user-defined types.
    Type *ty2 = find_typedef(tok);
    if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") ||
        equal(tok, "typeof") || equal(tok, "__typeof_unqual__") || ty2) {
      if (counter)
        break;

      if (equal(tok, "struct")) {
        ty = struct_decl(&tok, tok->next);
      } else if (equal(tok, "union")) {
        ty = union_decl(&tok, tok->next);
      } else if (equal(tok, "enum")) {
        ty = enum_specifier(&tok, tok->next);
      } else if (equal(tok, "typeof")) {
        ty = typeof_specifier(&tok, tok->next);
      } else if (equal(tok, "__typeof_unqual__")) {
        // C23 typeof_unqual: the type without const, volatile or _Atomic
        ty = unqual(typeof_specifier(&tok, tok->next));
        if (ty->is_atomic) {
          ty = copy_type(ty);
          ty->is_atomic = false;
        }
      } else {
        ty = ty2;
        tok = tok->next;
      }

      counter += OTHER;
      continue;
    }

    // Handle built-in types.
    if (equal(tok, "void"))
      counter += VOID;
    else if (equal(tok, "_Bool"))
      counter += BOOL;
    else if (equal(tok, "char"))
      counter += CHAR;
    else if (equal(tok, "short"))
      counter += SHORT;
    else if (equal(tok, "int"))
      counter += INT;
    else if (equal(tok, "long"))
      counter += LONG;
    else if (equal(tok, "__int128"))
      counter += INT128;
    else if (equal(tok, "_Complex") || equal(tok, "__complex__") ||
             equal(tok, "__complex"))
      counter += COMPLEX;
    else if (equal(tok, "float"))
      counter += FLOAT;
    else if (equal(tok, "double"))
      counter += DOUBLE;
    else if (equal(tok, "signed"))
      counter |= SIGNED;
    else if (equal(tok, "unsigned"))
      counter |= UNSIGNED;
    else
      unreachable();

    switch (counter) {
    case VOID:
      ty = ty_void;
      break;
    case BOOL:
      ty = ty_bool;
      break;
    case CHAR:
      ty = ty_char;
      break;
    case SIGNED + CHAR:
      ty = ty_schar;
      break;
    case UNSIGNED + CHAR:
      ty = ty_uchar;
      break;
    case SHORT:
    case SHORT + INT:
    case SIGNED + SHORT:
    case SIGNED + SHORT + INT:
      ty = ty_short;
      break;
    case UNSIGNED + SHORT:
    case UNSIGNED + SHORT + INT:
      ty = ty_ushort;
      break;
    case INT:
    case SIGNED:
    case SIGNED + INT:
      ty = ty_int;
      break;
    case UNSIGNED:
    case UNSIGNED + INT:
      ty = ty_uint;
      break;
    case LONG:
    case LONG + INT:
    case SIGNED + LONG:
    case SIGNED + LONG + INT:
      ty = ty_long;
      break;
    case LONG + LONG:
    case LONG + LONG + INT:
    case SIGNED + LONG + LONG:
    case SIGNED + LONG + LONG + INT:
      ty = ty_llong;
      break;
    case UNSIGNED + LONG:
    case UNSIGNED + LONG + INT:
      ty = ty_ulong;
      break;
    case UNSIGNED + LONG + LONG:
    case UNSIGNED + LONG + LONG + INT:
      ty = ty_ullong;
      break;
    case FLOAT:
      ty = ty_float;
      break;
    case DOUBLE:
      ty = ty_double;
      break;
    case LONG + DOUBLE:
      ty = ty_ldouble;
      break;
    case INT128:
    case SIGNED + INT128:
      ty = ty_int128;
      break;
    case UNSIGNED + INT128:
      ty = ty_uint128;
      break;
    case COMPLEX + FLOAT:
      ty = complex_type(ty_float);
      break;
    case COMPLEX:
    case COMPLEX + DOUBLE:
      ty = complex_type(ty_double);
      break;
    case COMPLEX + LONG: // on the way to `_Complex long double`
    case COMPLEX + LONG + DOUBLE:
      ty = complex_type(ty_ldouble);
      break;
    default:
      if (counter & COMPLEX)
        error_tok(tok, "only floating-point complex types are supported");
      error_tok(tok, "invalid type");
    }

    tok = tok->next;
  }

  if (is_atomic) {
    ty = copy_type(ty);
    ty->is_atomic = true;
  }
  if (ty->is_atomic && is_int128(ty))
    error_tok(tok, "_Atomic __int128 is not supported");
  if (ty->is_atomic && is_complex(ty))
    error_tok(tok, "_Atomic _Complex is not supported");
  ty = qualified(ty, is_const, is_volatile);

  *rest = tok;
  if (is_auto && counter == 0 && !is_atomic)
    return &auto_type;
  return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param       = declspec declarator
//
// Parameters have their own scope, and each is in scope for the ones after
// it, as in `void f(int n, int a[n][n])`. Each named parameter gets its
// variable here; a function definition then uses those same variables, so
// the sizes in later parameters refer to the real parameters.
static Type *func_params(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "void") && equal(tok->next, ")")) {
    *rest = tok->next->next;
    return func_type(ty);
  }

  // A K&R definition's `f(a, b)`: only names, typed by the declarations
  // before its body (see kr_params()). To a caller, it's `f()`.
  if (tok->kind == TK_IDENT && !is_typename(tok) &&
      (equal(tok->next, ",") || equal(tok->next, ")"))) {
    Token *names = tok;
    while (!equal(tok->next, ")")) {
      tok = skip(tok->next, ",");
      if (tok->kind != TK_IDENT)
        error_tok(tok, "expected a parameter name");
    }
    *rest = tok->next->next;
    ty = func_type(ty);
    ty->is_variadic = ty->is_oldstyle = true;
    ty->kr_names = names;
    return ty;
  }

  Type head = {};
  Type *cur = &head;
  bool is_variadic = false;
  enter_scope();

  while (!equal(tok, ")")) {
    if (cur != &head) {
      if (!equal(tok, ","))
        error_expected(tok, "',' or ')'");
      tok = tok->next;
    }

    if (equal(tok, "...")) {
      is_variadic = true;
      tok = tok->next;
      skip(tok, ")");
      break;
    }

    Token *param_start = tok;
    Type *ty2 = declspec(&tok, tok, NULL);
    ty2 = declarator(&tok, tok, ty2, NULL);
    if (ty2->kind == TY_VOID) {
      // `(V)` with `typedef void V;` is `(void)`.
      if (cur == &head && !ty2->name && equal(tok, ")")) {
        leave_scope();
        *rest = tok->next;
        return func_type(ty);
      }
      error_tok(param_start, "'void' must be the only parameter and unnamed");
    }

    Token *name = ty2->name;

    if (ty2->kind == TY_ARRAY || ty2->kind == TY_VLA) {
      // "array of T" is converted to "pointer to T" only in the parameter
      // context. For example, *argv[] is converted to **argv by this, and
      // `int a[n]` to `int *a`.
      ty2 = pointer_to(ty2->base);
      ty2->name = name;
    } else if (ty2->kind == TY_FUNC) {
      // Likewise, a function is converted to a pointer to a function
      // only in the parameter context.
      ty2 = pointer_to(ty2);
      ty2->name = name;
    }

    cur = cur->next = copy_type(ty2);

    // Not new_lvar(): this may be a prototype, with no function to own it.
    if (name) {
      Obj *var = arena_alloc(sizeof(Obj));
      var->name = get_ident(name);
      var->ty = cur;
      var->align = cur->align;
      var->is_local = true; // no `tok`: unused parameters get no warning
      push_scope(var->name)->var = var;
      cur->param_var = var;
    }
  }

  leave_scope();

  // Before C23, `int f()` says nothing about f's parameters, so any
  // arguments are accepted. In C23 it means `int f(void)`.
  bool is_oldstyle = cur == &head && !is_variadic && opt_std < 2023;

  ty = func_type(ty);
  ty->params = head.next;
  ty->is_variadic = is_variadic || is_oldstyle;
  ty->is_oldstyle = is_oldstyle;
  *rest = tok->next;
  return ty;
}

// An array's elements can't be functions or void. (A void of size 0 is
// the placeholder of a lookahead or a nested declarator, not void.)
static void check_array_element(Type *ty, Token *tok) {
  if (ty->kind == TY_FUNC)
    error_tok(tok, "declaration of an array of functions");
  if (ty->kind == TY_VOID && ty->size)
    error_tok(tok, "declaration of an array of voids");
}

// array-dimensions = ("static" | type-qualifier)* ("*" | const-expr)? "]"
//                    type-suffix
static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
  while (equal(tok, "static") || equal(tok, "restrict") || equal(tok, "const") ||
         equal(tok, "volatile") || equal(tok, "__restrict") ||
         equal(tok, "__restrict__"))
    tok = tok->next;

  // `[*]`, a variable length of unspecified size, is only allowed in a
  // prototype, where the size is never needed: treat it like `[]`.
  if (equal(tok, "*") && equal(tok->next, "]"))
    tok = tok->next;

  if (equal(tok, "]")) {
    ty = type_suffix(rest, tok->next, ty);
    check_array_element(ty, tok);
    return array_of(ty, -1);
  }

  // An assignment expression: `int a[k = 3]` is a VLA that also sets k.
  Token *start = tok;
  Node *expr = assign(&tok, tok);
  tok = skip(tok, "]");
  ty = type_suffix(rest, tok, ty);
  check_array_element(ty, start);

  if (ty->kind == TY_VLA || !is_const_expr(expr))
    return vla_of(ty, expr);

  // A negative size is an error, as C requires: code checks things at
  // compile time this way (BusyBox's `char BUG_...[ok ? 1 : -1]`).
  int64_t len = eval(expr);
  if (len < 0)
    error_tok(start, "size of array is negative");
  // No object can be larger than PTRDIFF_MAX bytes, as gcc has it.
  if (len > INT64_MAX / MAX(ty->size, 1))
    error_tok(start, "size of array is too large");
  return array_of(ty, len);
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "("))
    return func_params(rest, tok->next, ty);

  if (equal(tok, "["))
    return array_dimensions(rest, tok->next, ty);

  *rest = tok;
  return ty;
}

// pointers = ("*" ("const" | "volatile" | "restrict" | attributes)*)*
static Type *pointers(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*")) {
    ty = pointer_to(ty);
    for (;;) {
      // ty is a new type, so these can change it.
      if (consume(&tok, tok, "const"))
        ty->is_const = true;
      else if (consume(&tok, tok, "volatile"))
        ty->is_volatile = true;
      else if (equal(tok, "restrict") || equal(tok, "__restrict") ||
               equal(tok, "__restrict__"))
        tok = tok->next;
      else if (equal(tok, "_Atomic") && !equal(tok->next, "(")) {
        // `T *_Atomic p`: an atomic pointer
        ty->is_atomic = true;
        tok = tok->next;
      } else if (is_attribute(tok))
        tok = skip_attributes(tok);
      else
        break;
    }
  }
  *rest = tok;
  return ty;
}

// asm-label = ("asm" "(" string-literal ")")?
static Token *asm_label(Token *tok, Attrs *attrs) {
  if (!equal(tok, "asm"))
    return tok;
  attrs->asm_label_tok = tok;
  tok = skip(tok->next, "(");
  if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
    error_tok(tok, "expected string literal");
  attrs->asm_label = tok->str;
  return skip(tok->next, ")");
}

// `register long x asm("r10")`: where x is an asm statement's register
// operand, it goes in r10 (musl's system calls need this). Elsewhere x
// is an ordinary variable.
static void set_asm_register(Obj *var, Attrs *a) {
  int reg = gp_reg_number(a->asm_label);
  if (reg < 0 || reg == 4 || reg == 5)
    error_tok(a->asm_label_tok, "invalid register name '%s'", a->asm_label);
  if (!is_integer(var->ty) && var->ty->kind != TY_PTR)
    error_tok(a->asm_label_tok,
              "only an integer or a pointer can be a register variable");
  var->asm_reg = reg + 1;
}

// Does the "(" at `tok` start a parameter list rather than a declarator in
// parentheses? It does if a type or ")" follows, as in the parameter
// `int (int)`, a function taking int, or the type name `int ()`.
static bool is_func_suffix(Token *tok) {
  tok = tok->next;
  return equal(tok, ")") || equal(tok, "...") ||
         (is_typename(tok) && !is_attribute(tok));
}

// declarator = attributes pointers attributes
//              ("(" declarator ")" | ident attributes)? type-suffix asm-label
//              attributes
//
// The declarator's attributes, like `noreturn` in
// `void f(void) __attribute__((noreturn));`, go to `attrs`. With NULL
// (a parameter), `packed` and `aligned` are errors: nothing applies them.
static Type *declarator(Token **rest, Token *tok, Type *ty, Attrs *attrs) {
  Attrs scratch = {};
  if (!attrs)
    attrs = &scratch;

  tok = attributes(tok, attrs, true);
  ty = pointers(&tok, tok, ty);
  tok = attributes(tok, attrs, true);

  if (equal(tok, "(") && !is_func_suffix(tok)) {
    Token *start = tok;
    Type dummy = {};
    Attrs ignored = {};
    declarator(&tok, start->next, &dummy, &ignored);
    tok = skip(tok, ")");
    ty = type_suffix(&tok, tok, ty);
    tok = asm_label(tok, attrs);
    *rest = attributes(tok, attrs, true);
    ty = declarator(&tok, start->next, ty, attrs);
  } else {
    Token *name = NULL;
    Token *name_pos = tok;

    if (tok->kind == TK_IDENT) {
      name = tok;
      tok = attributes(tok->next, attrs, true);
    }

    ty = type_suffix(&tok, tok, ty);
    tok = asm_label(tok, attrs);
    *rest = attributes(tok, attrs, true);
    if (attrs->mode_tok)
      ty = copy_type(mode_type(ty, attrs->mode_tok));
    if (attrs->vector_tok)
      ty = vector_type(ty, attrs);
    ty->name = name;
    ty->name_pos = name_pos;
  }

  if (attrs == &scratch)
    no_decl_attrs(&scratch);
  return ty;
}

// The type `ty` becomes with attribute mode(name): the integer type of
// that size with ty's sign, as `int __attribute__((mode(QI)))` is signed
// char, or the floating type.
static Type *mode_type(Type *ty, Token *tok) {
  char *m = attribute_name(tok);
  if (is_integer(ty) && ty->kind != TY_BOOL) {
    int size = !strcmp(m, "QI") || !strcmp(m, "byte") ? 1 : !strcmp(m, "HI") ? 2 :
               !strcmp(m, "SI") ? 4 :
               !strcmp(m, "DI") || !strcmp(m, "word") || !strcmp(m, "pointer") ? 8 :
               !strcmp(m, "TI") ? 16 : 0;
    bool u = ty->is_unsigned;
    switch (size) {
    case 1: return u ? ty_uchar : ty_schar;
    case 2: return u ? ty_ushort : ty_short;
    case 4: return u ? ty_uint : ty_int;
    case 8: return u ? ty_ulong : ty_long;
    case 16: return u ? ty_uint128 : ty_int128;
    }
  }
  if (is_flonum(ty)) {
    if (!strcmp(m, "SF"))
      return ty_float;
    if (!strcmp(m, "DF"))
      return ty_double;
    if (!strcmp(m, "XF"))
      return ty_ldouble;
  }
  error_tok(tok, "mode '%s' is not supported for type '%s'", m, type_name(ty));
}

// [GNU] The type `ty` becomes with attribute vector_size(N): a vector of
// N bytes of ty, which must be a number other than _Bool and __int128. Of
// a pointer or an array, as gcc has it, the elements are vectors:
// `int __attribute__((vector_size(16))) *p` points to one. SSE's registers
// hold 16 bytes, so mucc takes vectors of 4, 8 or 16.
static Type *vector_type(Type *ty, Attrs *a) {
  if (ty->kind == TY_PTR)
    return pointer_to(vector_type(ty->base, a));
  if (ty->kind == TY_ARRAY)
    return array_of(vector_type(ty->base, a), ty->array_len);

  int n = a->vector_size;
  if ((!is_integer(ty) && ty->kind != TY_FLOAT && ty->kind != TY_DOUBLE) ||
      ty->kind == TY_BOOL || is_int128(ty))
    error_tok(a->vector_tok, "invalid vector type for attribute 'vector_size'");
  if (n <= 0 || n % ty->size || (n & (n - 1)))
    error_tok(a->vector_tok, "vector size must be a power of 2 and a multiple of the "
              "element size");
  if (n < 4 || n > 16)
    error_tok(a->vector_tok, "vectors of %d bytes are not supported (only 4, 8 or 16)", n);
  Type *vec = vector_of(ty, n);
  vec->is_const = ty->is_const;
  vec->is_volatile = ty->is_volatile;
  return vec;
}

// abstract-declarator = attributes pointers attributes
//                       ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  tok = skip_attributes(tok);
  ty = pointers(&tok, tok, ty);
  tok = skip_attributes(tok);

  if (equal(tok, "(") && !is_func_suffix(tok)) {
    Token *start = tok;
    Type dummy = {};
    abstract_declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return abstract_declarator(&tok, start->next, ty);
  }

  return type_suffix(rest, tok, ty);
}

// type-name = declspec abstract-declarator
static Type *typename(Token **rest, Token *tok) {
  Type *ty = declspec(&tok, tok, NULL);
  return abstract_declarator(rest, tok, ty);
}

//---------- enum and typeof -------------------------------------------------

static bool is_end(Token *tok) {
  return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static bool consume_end(Token **rest, Token *tok) {
  if (equal(tok, "}")) {
    *rest = tok->next;
    return true;
  }

  if (equal(tok, ",") && equal(tok->next, "}")) {
    *rest = tok->next->next;
    return true;
  }

  return false;
}

// Can integer type `ty` hold `val`? (For 8-byte types, any value
// const_expr() returns.)
static bool fits_in(int64_t val, Type *ty) {
  if (ty->kind == TY_BOOL)
    return val == 0 || val == 1;
  if (ty->size == 8)
    return true;
  int bits = ty->size * 8;
  if (ty->is_unsigned)
    return val >= 0 && val < ((int64_t)1 << bits);
  return val >= -((int64_t)1 << (bits - 1)) && val < ((int64_t)1 << (bits - 1));
}

// Without a fixed underlying type, an enum is unsigned int if none of
// its values is negative and int otherwise, as with gcc. [GNU] Values that
// don't fit 32 bits make it a long or unsigned long.
static void set_enum_type(Type *ty, int64_t min, int64_t max) {
  ty->is_unsigned = min >= 0;
  if (min >= 0 ? max <= UINT32_MAX : (min >= INT32_MIN && max <= INT32_MAX))
    ty->size = ty->align = 4;
  else
    ty->size = ty->align = 8;
}

// enum-specifier = attributes ident? (":" type)? "{" enum-list? "}" attributes
//                | attributes ident ((":" type)? "{" enum-list? "}" attributes)?
//                | attributes ident ":" type
//
// enum-list      = enumerator ("," enumerator)* ","?
// enumerator     = ident attributes ("=" num)?
static Type *enum_specifier(Token **rest, Token *tok) {
  Type *ty = enum_type();
  Attrs a = {};
  tok = attributes(tok, &a, true);

  // Read an enum tag.
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }
  ty->tag = tag;

  // C23 `enum E : type` fixes the underlying type, which gives the enum
  // its size and sign, and its constants their type. (In a struct,
  // `enum E : 3` is a bit-field instead.)
  Type *fixed = NULL;
  if (equal(tok, ":") && is_typename(tok->next)) {
    Token *start = tok->next;
    fixed = declspec(&tok, tok->next, NULL);
    if (!is_integer(fixed) || fixed->kind == TY_ENUM)
      error_tok(start, "an enum's underlying type must be an integer type");
    ty->size = ty->align = fixed->size;
    ty->is_unsigned = fixed->is_unsigned || fixed->kind == TY_BOOL;
  }

  if (tag && !equal(tok, "{")) {
    Type *ty2 = find_tag(tag);
    if (!ty2 && fixed) {
      // `enum E : type;` declares a complete type.
      push_tag_scope(tag, ty);
      *rest = tok;
      return ty;
    }
    if (!ty2) {
      // [GNU] `enum E;` or `enum E *p;` before E is defined. E is
      // incomplete until its list, as with gcc and clang.
      ty->size = -1;
      push_tag_scope(tag, ty);
      *rest = tok;
      return ty;
    }
    if (ty2->kind != TY_ENUM)
      error_tok(tag, "not an enum tag");
    *rest = tok;
    return ty2;
  }

  tok = skip(tok, "{");

  // An `enum E` declared earlier in this scope is the same type, completed
  // here. Before C23, which allows the same list again, a second list is
  // an error.
  if (tag) {
    Type *prev = hashmap_get2(&scope->tags, tag->loc, tag->len);
    if (prev && opt_std < 2023 && (prev->kind != TY_ENUM || prev->enum_consts))
      error_tok(tag, "redefinition of '%.*s'", tag->len, tag->loc);
    if (prev && prev->kind == TY_ENUM && prev->size < 0) {
      *prev = *ty;
      ty = prev;
    }
  }

  // Read an enum-list.
  int i = 0;
  int64_t val = 0;
  int64_t min = 0, max = 0;
  bool huge = false; // a value of 2^63 or more, as unsigned long
  EnumConst head = {};
  EnumConst *last = &head;
  while (!consume_end(rest, tok)) {
    if (i++ > 0)
      tok = skip(tok, ",");

    // An enumerator can't reuse a name declared in the same scope, as in
    // `enum A { X }; enum B { X };`.
    Token *name_tok = tok;
    char *name = get_ident(tok);
    if (hashmap_get(&scope->vars, name))
      error_tok(tok, "redeclaration of '%s'", name);
    Token *val_tok = tok;
    tok = skip_attributes(tok->next);

    if (equal(tok, "=")) {
      val_tok = tok->next;
      Node *e = conditional(&tok, tok->next);
      add_type(e);
      val = eval(e);
      huge |= e->ty->is_unsigned && e->ty->size == 8 && val < 0;
    }

    if (fixed && !fits_in(val, fixed))
      error_tok(val_tok, "enumerator value %ld is outside the range of '%s'",
                (long)val, type_name(fixed));

    min = (i == 1) ? val : MIN(min, val);
    max = (i == 1) ? val : MAX(max, val);
    if (!fixed)
      set_enum_type(ty, min, max);
    if (!fixed && huge) {
      ty->size = ty->align = 8;
      ty->is_unsigned = true;
    }

    // A constant is an int if its value fits one, and otherwise has the
    // enum's type, as with gcc.
    VarScope *sc = push_scope(name);
    sc->enum_ty = (fixed || huge || val != (int)val) ? ty : ty_int;
    sc->enum_val = val;

    last = last->next = arena_alloc(sizeof(EnumConst));
    last->name = name_tok;
    last->val = val++;
  }
  ty->enum_consts = head.next;

  *rest = attributes(*rest, &a, true);
  no_symbol_attrs(&a, "an enum");
  if (a.align)
    error_tok(a.layout_tok, "attribute 'aligned' is not supported on an enum");

  // A packed enum is the smallest integer type that holds its values, as
  // with gcc.
  if (a.is_packed && fixed)
    error_tok(a.layout_tok, "attribute 'packed' is not supported on an enum "
              "with a fixed underlying type");
  if (a.is_packed) {
    if (min >= 0 ? max <= 0xff : (min >= -128 && max <= 127))
      ty->size = ty->align = 1;
    else if (min >= 0 ? max <= 0xffff : (min >= -32768 && max <= 32767))
      ty->size = ty->align = 2;
  }

  if (tag)
    push_tag_scope(tag, ty);
  return ty;
}

// typeof-specifier = "(" (expr | typename) ")"
static Type *typeof_specifier(Token **rest, Token *tok) {
  tok = skip(tok, "(");

  Type *ty;
  if (is_typename(tok)) {
    ty = typename(&tok, tok);
  } else {
    Node *node = expr(&tok, tok);
    add_type(node);
    ty = node->ty;
  }
  *rest = skip(tok, ")");
  return ty;
}

//---------- C23 auto and constexpr ------------------------------------------

// The type `auto x = init;` gives x: init's type, with arrays and
// functions turned into pointers and _Atomic, const and volatile dropped.
static Type *auto_type_of(Node *init) {
  add_type(init);
  Type *ty = unqual(init->ty);

  if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
    return pointer_to(ty->base);
  if (ty->kind == TY_FUNC)
    return pointer_to(ty);
  if (ty->kind == TY_VOID)
    error_tok(init->tok, "cannot infer a type from a void expression");
  if (ty->is_atomic) {
    ty = copy_type(ty);
    ty->is_atomic = false;
  }
  return ty;
}

// For `auto` at file scope or with `static`, where the initializer is
// read by gvar_initializer(): parses it once just to learn its type.
static Type *peek_auto_type(Token *tok) {
  return auto_type_of(assign(&tok, tok));
}

// `auto *p = ...` and the like aren't allowed; `auto` needs a bare name.
static void check_auto_declarator(Type *ty, Token *tok) {
  if (ty != &auto_type && equal(tok, "="))
    error_tok(ty->name ? ty->name : tok,
              "'auto' can only declare a plain variable, as in 'auto x = 1'");
}

// C23 constexpr: `init` must be a constant whose value fits var's type
// exactly. Records the value, so later constant expressions (array sizes,
// case labels, static_assert, other constexprs) can use var. Arrays and
// structs are accepted but act as ordinary initialized objects.
static void set_constexpr_value(Obj *var, Node *init) {
  Type *ty = var->ty;
  if (!is_numeric(ty) && ty->kind != TY_PTR)
    return;

  add_type(init);
  if (!is_const_expr(init))
    error_tok(init->tok, "constexpr '%s' needs a constant initializer", var->name);
  var->is_constexpr = true;

  if (is_flonum(ty)) {
    var->constexpr_fval = to_flonum(ty, eval_double(init));
    return;
  }

  if (is_flonum(init->ty))
    error_tok(init->tok, "constexpr '%s' of type '%s' can't be initialized "
              "with a floating value", var->name, type_name(ty));

  int64_t val = eval(init);
  if (ty->kind == TY_PTR && val != 0)
    error_tok(init->tok, "a constexpr pointer can only be null");

  // The value must survive conversion unchanged, sign included.
  int64_t conv = eval(new_cast(init, ty));
  bool sign_flip = val < 0 && ty->is_unsigned != init->ty->is_unsigned &&
                   ty->kind != TY_BOOL;
  if (conv != val || sign_flip)
    error_tok(init->tok, "value doesn't fit in constexpr '%s' of type '%s'",
              var->name, type_name(ty));
  var->constexpr_val = conv;
}

// Like set_constexpr_value(), reading the initializer at `tok` (just
// after the '='), which the caller then parses again as usual.
static void peek_constexpr_value(Obj *var, Token *tok) {
  if (!is_numeric(var->ty) && var->ty->kind != TY_PTR)
    return;
  consume(&tok, tok, "{"); // as in `constexpr int x = {1};`
  set_constexpr_value(var, assign(&tok, tok));
}

//---------- Cleanup variables -----------------------------------------------

// `fn(&var)`, for the cleanup of `var`, or freeing a VLA.
static Node *cleanup_call(Cleanup *c, Token *tok) {
  if (!c->fn)
    return new_unary(ND_VLA_FREE, new_var_node(c->var, tok), tok);

  Node *fn = new_var_node(c->fn, tok);
  Node *arg = new_unary(ND_ADDR, new_var_node(c->var, tok), tok);
  add_type(fn);
  add_type(arg);
  Node *node = new_unary(ND_FUNCALL, fn, tok);
  node->func_ty = c->fn->ty;
  node->ty = c->fn->ty->return_ty;
  node->args = new_cast(arg, c->fn->ty->params);
  return new_unary(ND_EXPR_STMT, node, tok);
}

// The cleanups of the variables in `from` but not in `to`, innermost
// first: what leaving their scopes runs. `to` must be `from` or a scope
// around it.
static Node *cleanup_calls(Cleanup *from, Cleanup *to, Token *tok) {
  Node head = {};
  Node *cur = &head;
  for (Cleanup *c = from; c != to; c = c->next)
    cur = cur->next = cleanup_call(c, tok);
  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;
  add_type(node);
  return node;
}

// Do the cleanups of the variables in `from` but not in `to` call a
// function, and not only free VLAs?
static bool runs_cleanup_fn(Cleanup *from, Cleanup *to) {
  for (Cleanup *c = from; c != to; c = c->next)
    if (c->fn)
      return true;
  return false;
}

// A jump into the scope of `c`, the innermost variable in scope at the
// jump's target, is an error: it would skip the variable's
// initialization or allocation.
static void jump_into_scope(Cleanup *c, Token *tok) {
  error_tok(tok, "jump into the scope of %s",
            c->fn ? "a variable with a cleanup" : "a variable-length array");
}

// `return` in the scope of cleanup variables: the value is computed
// first, as it may use them, then the cleanups run, as with gcc.
static Node *return_with_cleanups(Node *ret) {
  if (!runs_cleanup_fn(cleanups, NULL)) // returning frees VLAs anyway
    return ret;

  Token *tok = ret->tok;
  Node *node = new_node(ND_BLOCK, tok);
  Node head = {};
  Node *cur = &head;
  Node *exp = ret->lhs;
  if (exp) {
    add_type(exp);
    if (exp->ty->kind == TY_VOID) {
      // `return f();` in a void function
      cur = cur->next = new_unary(ND_EXPR_STMT, exp, tok);
      ret->lhs = NULL;
    } else {
      Obj *tmp = new_lvar("", exp->ty);
      Node *set = new_binary(ND_ASSIGN, new_var_node(tmp, tok), exp, tok);
      cur = cur->next = new_unary(ND_EXPR_STMT, set, tok);
      ret->lhs = new_var_node(tmp, tok);
    }
  }
  cur = cur->next = cleanup_calls(cleanups, NULL, tok);
  cur->next = ret;
  node->body = head.next;
  return node;
}

// Is `outer` `inner` or a scope around it?
static bool is_scope_of(Cleanup *outer, Cleanup *inner) {
  for (Cleanup *c = inner; c; c = c->next)
    if (c == outer)
      return true;
  return !outer;
}

// `var` has __attribute__((cleanup(fn))): check that fn(&var) is a valid
// call, and put it in scope.
static void push_cleanup(Obj *var, Attrs *a) {
  Obj *fn = a->cleanup_fn;
  Type *param = fn->ty->params;
  if (!param || param->next)
    error_tok(a->cleanup_tok, "cleanup function '%s' must take one parameter",
              fn->name);
  Node *arg = new_unary(ND_ADDR, new_var_node(var, a->cleanup_tok), a->cleanup_tok);
  add_type(arg);
  check_assign(param, arg, format("the argument of cleanup function '%s'", fn->name));
  if (fn->ty->return_ty->kind == TY_STRUCT || fn->ty->return_ty->kind == TY_UNION)
    error_tok(a->cleanup_tok, "a cleanup function returning a struct is not supported");

  Cleanup *c = arena_alloc(sizeof(Cleanup));
  c->var = var;
  c->fn = fn;
  c->next = cleanups;
  cleanups = c;
  var->is_used = true;
  strarray_push(&current_fn->refs, fn->name); // keeps a static inline fn
}

//---------- Variable declarations and VLAs ----------------------------------

// True if `ty` is or contains a VLA, as `int (*)[n]` does.
static bool is_variably_modified(Type *ty) {
  for (; ty; ty = ty->base)
    if (ty->kind == TY_VLA)
      return true;
  return false;
}

// Generate code for computing a VLA size.
static Node *compute_vla_size(Type *ty, Token *tok) {
  Node *node = new_node(ND_NULL_EXPR, tok);
  if (ty->base)
    node = new_binary(ND_COMMA, node, compute_vla_size(ty->base, tok), tok);

  if (ty->kind != TY_VLA)
    return node;

  Node *base_sz;
  if (ty->base->kind == TY_VLA)
    base_sz = new_var_node(ty->base->vla_size, tok);
  else
    base_sz = new_long(ty->base->size, tok);

  ty->vla_size = new_lvar("", ty_ulong);
  Node *expr = new_binary(ND_ASSIGN, new_var_node(ty->vla_size, tok),
                          new_binary(ND_MUL, ty->vla_len, base_sz, tok),
                          tok);
  return new_binary(ND_COMMA, node, expr, tok);
}

static Node *new_alloca(Node *sz) {
  Node *node = new_unary(ND_FUNCALL, new_var_node(builtin_alloca, sz->tok), sz->tok);
  node->func_ty = builtin_alloca->ty;
  node->ty = builtin_alloca->ty->return_ty;
  node->args = sz;
  add_type(sz);
  return node;
}

// Allocates `var`, a VLA of `size` bytes, saving the stack bottom first
// so leaving its scope can free it.
static Node *push_vla(Obj *var, Obj *size, Token *tok) {
  Cleanup *c = arena_alloc(sizeof(Cleanup));
  c->var = new_lvar("", pointer_to(ty_char));
  c->next = cleanups;
  cleanups = c;

  Node *save = new_binary(ND_ASSIGN, new_var_node(c->var, tok),
                          new_var_node(current_fn->alloca_bottom, tok), tok);
  Node *alloc = new_binary(ND_ASSIGN, new_vla_ptr(var, tok),
                           new_alloca(new_var_node(size, tok)), tok);
  return new_binary(ND_COMMA, save, alloc, tok);
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
// Skips the ',' between two declarators, as in `int a, b;`. Anything
// else there means the ';' ending the declaration is missing.
static Token *skip_decl_comma(Token *tok) {
  if (!equal(tok, ","))
    error_expected(tok, "';'");
  return tok->next;
}

static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr) {
  Node head = {};
  Node *cur = &head;
  int i = 0;

  while (!equal(tok, ";")) {
    if (i++ > 0) {
      tok = skip_decl_comma(tok);

      // `int x, f(void);`: a function declaration among the variables
      // (see function())
      if (is_function(tok, basety)) {
        tok = function(tok, basety, attr ? attr : &(VarAttr){});
        if (equal(tok, ","))
          continue;
        Node *node = new_node(ND_BLOCK, tok);
        node->body = head.next;
        *rest = tok;
        return node;
      }
    }

    Attrs da = {};
    Type *ty = declarator(&tok, tok, basety, &da);
    if (ty->kind == TY_VOID)
      error_tok(tok, "variable declared void");
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");

    // __attribute__((unused)) turns off the unused-variable warning.
    bool is_unused = da.is_unused || (attr && attr->is_unused);

    // aligned(N) raises the variable's alignment. (Above 16, see
    // assign_lvar_offsets() in cgen.c.)
    Attrs all = attr ? attr->gnu : (Attrs){};
    merge_attrs(&all, &da);
    no_global_attrs(&all, "a local variable");
    no_fn_attrs(&all, "a local variable");
    if (all.is_packed)
      error_tok(all.layout_tok, "attribute 'packed' is not supported on a variable");
    Token *name = ty->name;
    bool is_constexpr = attr && attr->is_constexpr;
    if (is_constexpr && !equal(tok, "="))
      error_tok(name, "constexpr '%s' needs an initializer", get_ident(name));
    if (basety == &auto_type)
      check_auto_declarator(ty, tok);

    if (all.asm_label_tok && !(attr && attr->is_register))
      error_tok(all.asm_label_tok,
                "an asm label is only supported on a register variable");

    // A name in a block is declared once there, as C requires (a local
    // variable has no linkage).
    VarScope *prev = hashmap_get(&scope->vars, get_ident(name));
    if (prev && prev->type_def)
      error_tok(name, "'%s' redeclared as a different kind of symbol", get_ident(name));
    if (prev)
      error_tok(name, "redeclaration of '%s' with no linkage", get_ident(name));

    if (attr && attr->is_static) {
      // static local variable
      no_cleanup(&all, "a static variable");
      if (ty == &auto_type && equal(tok, "="))
        ty = peek_auto_type(tok->next);
      Obj *var = new_anon_gvar(ty);
      var->align = MAX(var->align, all.align);
      if (attr->align) // _Alignas
        var->align = MAX(var->align, attr->align);
      push_scope(get_ident(name))->var = var;
      if (is_constexpr)
        peek_constexpr_value(var, tok->next);
      if (equal(tok, "="))
        gvar_initializer(&tok, tok->next, var);
      continue;
    }

    // C23 `auto x = init;`: x takes init's type.
    if (ty == &auto_type && equal(tok, "=")) {
      no_asm_label(&all, "an auto variable");
      Node *init = assign(&tok, tok->next);
      Obj *var = new_lvar(get_ident(name), auto_type_of(init));
      var->tok = name;
      var->is_used = is_unused;
      var->align = MAX(var->align, all.align);
      if (is_constexpr)
        set_constexpr_value(var, init);
      Node *set = new_binary(ND_ASSIGN, new_var_node(var, name), init, name);
      cur = cur->next = new_unary(ND_EXPR_STMT, set, name);
      if (all.cleanup_tok)
        push_cleanup(var, &all);
      continue;
    }

    // Generate code for computing a VLA size. We need to do this
    // even if ty is not VLA because ty may be a pointer to VLA
    // (e.g. int (*foo)[n][m] where n and m are variables.)
    cur = cur->next = new_unary(ND_EXPR_STMT, compute_vla_size(ty, tok), tok);

    if (ty->kind == TY_VLA) {
      if (equal(tok, "="))
        error_tok(tok, "variable-sized object may not be initialized");
      no_cleanup(&all, "a variable-length array");
      no_asm_label(&all, "a variable-length array");

      // Variable length arrays (VLAs) are translated to alloca() calls.
      // For example, `int x[n+2]` is translated to `tmp = n + 2,
      // x = alloca(tmp)`.
      Obj *var = new_lvar(get_ident(ty->name), ty);
      var->tok = ty->name;
      var->is_used = is_unused;
      Token *tok = ty->name;
      Node *expr = push_vla(var, ty->vla_size, tok);
      cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
      continue;
    }

    Obj *var = new_lvar(get_ident(ty->name), ty);
    var->tok = ty->name;
    var->is_used = is_unused;
    var->is_register = attr && attr->is_register;
    if (all.asm_label_tok)
      set_asm_register(var, &all);
    if (attr && attr->align)
      var->align = attr->align;
    var->align = MAX(var->align, all.align);
    if (is_constexpr)
      peek_constexpr_value(var, tok->next);

    if (equal(tok, "=")) {
      Node *expr = lvar_initializer(&tok, tok->next, var);
      cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
    }

    if (var->ty->size < 0)
      error_tok(ty->name, "variable has incomplete type");
    if (var->ty->kind == TY_VOID)
      error_tok(ty->name, "variable declared void");
    if (all.cleanup_tok)
      push_cleanup(var, &all);
  }

  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;
  *rest = tok->next;
  return node;
}

//---------- Initializers ----------------------------------------------------

static Token *skip_excess_element(Token *tok) {
  if (equal(tok, "{")) {
    tok = skip_excess_element(tok->next);
    return skip(tok, "}");
  }

  assign(&tok, tok);
  return tok;
}

// string-initializer = string-literal
static void string_initializer(Token **rest, Token *tok, Initializer *init) {
  Type *base = init->ty->base;
  if (!is_integer(base) || base->size != tok->ty->base->size)
    error_tok(tok, "array of inappropriate type initialized from string constant");
  if (init->is_flexible)
    *init = *new_initializer(array_of(init->ty->base, tok->ty->array_len), false);

  int sz = init->ty->base->size;
  if (sz != 1 && sz != 2 && sz != 4)
    unreachable();

  // It sets every element: those past the string to zero.
  init->str = tok;
  init->embeds = NULL;
  init->children = NULL;
  *rest = tok->next;
}

// #embed's bytes (TK_EMBED `tok`) as elements i, i+1, ... of integer
// array `init`, with no node for each: a byte's value converted to the
// element type is the same bytes but for a _Bool. False if they don't
// fit or an operator follows the last: they are a list then.
static bool embed_init(Token **rest, Token *tok, Initializer *init, int64_t i) {
  if (tok->kind != TK_EMBED || !is_integer(init->ty->base) ||
      i + tok->embed->len > init->ty->array_len || !(equal(tok->next, ",") || equal(tok->next, "}")))
    return false;

  // It overrides elements set before it.
  for (int j = 0; init->children && j < tok->embed->len; j++)
    init->children[i + j] = NULL;

  EmbedRun *run = arena_alloc(sizeof(EmbedRun));
  run->idx = i;
  run->tok = tok;
  EmbedRun **p = &init->embeds;
  while (*p)
    p = &(*p)->next;
  *p = run;

  *rest = tok->next;
  return true;
}

// Byte `j` of #embed run `run` as an element of type `ty`
static uint8_t embed_elem(EmbedRun *run, int j, Type *ty) {
  uint8_t b = run->tok->embed->data[j];
  return ty->kind == TY_BOOL ? b != 0 : b;
}

// Element `i` of string initializer `init->str` (0 past its end).
static uint32_t str_elem(Initializer *init, int i) {
  Token *tok = init->str;
  if (i >= tok->ty->array_len)
    return 0;
  switch (init->ty->base->size) {
  case 1:
    return init->ty->base->kind == TY_BOOL ? tok->str[i] != 0 : (uint8_t)tok->str[i];
  case 2:
    return ((uint16_t *)tok->str)[i];
  default:
    return ((uint32_t *)tok->str)[i];
  }
}

// array-designator = "[" const-expr "]"
//
// C99 added the designated initializer to the language, which allows
// programmers to move the "cursor" of an initializer to any element.
// The syntax looks like this:
//
//   int x[10] = { 1, 2, [5]=3, 4, 5, 6, 7 };
//
// `[5]` moves the cursor to the 5th element, so the 5th element of x
// is set to 3. Initialization then continues forward in order, so
// 6th, 7th, 8th and 9th elements are initialized with 4, 5, 6 and 7,
// respectively. Unspecified elements (in this case, 3rd and 4th
// elements) are initialized with zero.
//
// Nesting is allowed, so the following initializer is valid:
//
//   int x[5][10] = { [5][8]=1, 2, 3 };
//
// It sets x[5][8], x[5][9] and x[6][0] to 1, 2 and 3, respectively.
//
// Use `.fieldname` to move the cursor for a struct initializer. E.g.
//
//   struct { int a, b, c; } x = { .c=5 };
//
// The above initializer sets x.c to 5.
static void array_designator(Token **rest, Token *tok, Type *ty, int64_t *begin,
                             int64_t *end) {
  *begin = const_expr(&tok, tok->next);
  if (*begin < 0)
    error_tok(tok, "array designator index is negative");
  if (*begin >= ty->array_len)
    error_tok(tok, "array designator index exceeds array bounds");

  if (equal(tok, "...")) {
    *end = const_expr(&tok, tok->next);
    if (*end >= ty->array_len)
      error_tok(tok, "array designator index exceeds array bounds");
    if (*end < *begin)
      error_tok(tok, "array designator range [%ld, %ld] is empty", *begin, *end);
  } else {
    *end = *begin;
  }

  *rest = skip(tok, "]");
}

// The first member from `mem` on that an initializer sets: unnamed
// bit-fields are padding, which initializers skip.
static Member *init_member(Member *mem) {
  while (mem && mem->is_bitfield && !mem->name)
    mem = mem->next;
  return mem;
}

// struct-designator = "." ident
static Member *struct_designator(Token **rest, Token *tok, Type *ty) {
  Token *start = tok;
  tok = skip(tok, ".");
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected a field designator");

  for (Member *mem = ty->members; mem; mem = mem->next) {
    // Anonymous struct or union member, or an unnamed bit-field
    if (!mem->name) {
      if (!mem->is_bitfield && get_struct_member(mem->ty, tok)) {
        *rest = start;
        return mem;
      }
      continue;
    }

    // Regular struct member
    if (mem->name->len == tok->len && !strncmp(mem->name->loc, tok->loc, tok->len)) {
      *rest = tok->next;
      return mem;
    }
  }

  error_tok(tok, "struct has no such member");
}

// designation = ("[" const-expr "]" | "." ident)* "="? initializer
static void designation(Token **rest, Token *tok, Initializer *init) {
  if (equal(tok, "[")) {
    if (init->ty->kind != TY_ARRAY)
      error_tok(tok, "array index in non-array initializer");

    int64_t begin, end;
    array_designator(&tok, tok, init->ty, &begin, &end);

    Token *tok2;
    for (int64_t i = begin; i <= end; i++)
      designation(&tok2, tok, elem_init(init, i));
    array_initializer2(rest, tok2, init, begin + 1);
    return;
  }

  if (equal(tok, ".") && init->ty->kind == TY_STRUCT) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    designation(&tok, tok, mem_init(init, mem));
    init->expr = NULL;
    struct_initializer2(rest, tok, init, init_member(mem->next), true);
    return;
  }

  if (equal(tok, ".") && init->ty->kind == TY_UNION) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    init->mem = mem;
    designation(rest, tok, mem_init(init, mem));
    return;
  }

  if (equal(tok, "."))
    error_tok(tok, "field name not in struct or union initializer");

  if (equal(tok, "="))
    tok = tok->next;
  initializer2(rest, tok, init);
}

// The token after a scalar's initializer at `tok`: the "," or "}" after
// it, outside any brackets.
static Token *skip_scalar_init(Token *tok) {
  int depth = 0;
  for (; tok->kind != TK_EOF; tok = tok->next) {
    if (depth == 0 && (equal(tok, ",") || equal(tok, "}")))
      return tok;
    if (equal(tok, "(") || equal(tok, "[") || equal(tok, "{"))
      depth++;
    else if (equal(tok, ")") || equal(tok, "]") || equal(tok, "}"))
      depth--;
  }
  return tok;
}

// An array length can be omitted if an array has an initializer
// (e.g. `int x[] = {1,2,3}`). If it's omitted, count the number
// of initializer elements.
static int64_t count_array_init_elements(Token *tok, Type *ty) {
  bool first = true;
  Initializer *dummy = new_initializer(ty->base, true);

  // A scalar's initializer is an expression, or one in braces: skip its
  // tokens rather than parse it, which would make its nodes twice.
  Type *base = ty->base;
  bool scalar = is_complex(base) ||
                (base->kind != TY_ARRAY && base->kind != TY_STRUCT && base->kind != TY_UNION);

  int64_t i = 0, max = 0;

  while (!consume_end(&tok, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[")) {
      i = const_expr(&tok, tok->next);
      if (equal(tok, "..."))
        i = const_expr(&tok, tok->next);
      tok = skip(tok, "]");
      if (scalar && equal(tok, "="))
        tok = tok->next;
      else if (!scalar)
        designation(&tok, tok, dummy);
    } else if (!scalar) {
      initializer2(&tok, tok, dummy);
    }

    if (scalar) {
      // #embed's bytes are as many elements (see embed_init()).
      if (tok->kind == TK_EMBED) {
        i += tok->embed->len - 1;
        tok = tok->next;
      }
      tok = skip_scalar_init(tok);
    }

    i++;
    max = MAX(max, i);
  }
  return max;
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void array_initializer1(Token **rest, Token *tok, Initializer *init) {
  tok = skip(tok, "{");

  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len), false);
  }

  bool first = true;

  for (int64_t i = 0; !consume_end(rest, tok); i++) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[")) {
      int64_t begin, end;
      array_designator(&tok, tok, init->ty, &begin, &end);

      Token *tok2;
      for (int64_t j = begin; j <= end; j++)
        designation(&tok2, tok, elem_init(init, j));
      tok = tok2;
      i = end;
      continue;
    }

    Token *embed = tok;
    if (embed_init(&tok, tok, init, i)) {
      i += embed->embed->len - 1;
      continue;
    }

    if (i < init->ty->array_len)
      initializer2(&tok, tok, elem_init(init, i));
    else
      tok = skip_excess_element(tok);
  }
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int64_t i) {
  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len), false);
  }

  for (; i < init->ty->array_len && !is_end(tok); i++) {
    Token *start = tok;
    if (i > 0)
      tok = skip(tok, ",");

    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }

    Token *embed = tok;
    if (embed_init(&tok, tok, init, i)) {
      i += embed->embed->len - 1;
      continue;
    }

    initializer2(&tok, tok, elem_init(init, i));
  }
  *rest = tok;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
  tok = skip(tok, "{");

  Member *mem = init_member(init->ty->members);
  bool first = true;

  while (!consume_end(rest, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, ".")) {
      mem = struct_designator(&tok, tok, init->ty);
      designation(&tok, tok, mem_init(init, mem));
      mem = init_member(mem->next);
      continue;
    }

    if (mem) {
      initializer2(&tok, tok, mem_init(init, mem));
      mem = init_member(mem->next);
    } else {
      tok = skip_excess_element(tok);
    }
  }
}

// struct-initializer2 = initializer ("," initializer)*
//
// For a struct without braces, from its first member, or the members
// after a designated one (`.a.x = 1, 2`), where a "," comes first.
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem,
                                bool after_comma) {
  bool first = !after_comma;

  for (mem = init_member(mem); mem && !is_end(tok); mem = init_member(mem->next)) {
    Token *start = tok;

    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }

    initializer2(&tok, tok, mem_init(init, mem));
  }
  *rest = tok;
}

static void union_initializer(Token **rest, Token *tok, Initializer *init) {
  // Unlike structs, union initializers take only one initializer,
  // and that initializes the first union member by default.
  // You can initialize other member using a designated initializer.
  // With several designators, the last one names the member, as in
  // `{.b = 8, .a = 7}`, which sets a.
  if (equal(tok, "{") && equal(tok->next, ".")) {
    tok = tok->next;
    do {
      Member *mem = struct_designator(&tok, tok, init->ty);
      init->mem = mem;
      designation(&tok, tok, mem_init(init, mem));
    } while (consume(&tok, tok, ",") && equal(tok, "."));
    *rest = skip(tok, "}");
    return;
  }

  init->mem = init->ty->members;

  // {} (C23) leaves it all zero; it's all an empty union (GNU) can have.
  if (equal(tok, "{") && consume(rest, tok->next, "}"))
    return;
  if (!init->mem)
    error_tok(tok, "an empty union takes no value");

  if (equal(tok, "{")) {
    initializer2(&tok, tok->next, mem_init(init, init->mem));
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
  } else {
    initializer2(rest, tok, mem_init(init, init->mem));
  }
}

// initializer = string-initializer | array-initializer
//             | struct-initializer | union-initializer
//             | assign
static void initializer2(Token **rest, Token *tok, Initializer *init) {
  if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR) {
    string_initializer(rest, tok, init);
    return;
  }

  // [GNU] or in parentheses, as gettext's N_("abc") expands to.
  if (init->ty->kind == TY_ARRAY && equal(tok, "(") &&
      tok->next->kind == TK_STR && equal(tok->next->next, ")")) {
    Token *ignore;
    string_initializer(&ignore, tok->next, init);
    *rest = tok->next->next->next;
    return;
  }

  // The string may also be in braces, as in `char s[] = {"abc"};`.
  if (init->ty->kind == TY_ARRAY && is_integer(init->ty->base) &&
      equal(tok, "{") && tok->next->kind == TK_STR) {
    Token *end = tok->next->next;
    if (equal(end, ","))
      end = end->next;
    if (equal(end, "}")) {
      Token *ignore;
      string_initializer(&ignore, tok->next, init);
      *rest = end->next;
      return;
    }
  }

  if (init->ty->kind == TY_ARRAY) {
    if (equal(tok, "{"))
      array_initializer1(rest, tok, init);
    else
      array_initializer2(rest, tok, init, 0);
    return;
  }

  // [GNU] A vector, from another or from its elements in braces, in
  // order (gcc takes no designators): init's children are its elements.
  if (is_vector(init->ty)) {
    if (equal(tok, "{")) {
      Type *ty = init->ty;
      init->ty = array_of(ty->elem, ty->array_len); // for elem_init()
      tok = tok->next;
      for (int i = 0; !consume_end(rest, tok); i++) {
        if (i > 0)
          tok = skip(tok, ",");
        if (equal(tok, "[") || equal(tok, "."))
          error_tok(tok, "a designator in a vector initializer");
        if (i < ty->array_len)
          initializer2(&tok, tok, elem_init(init, i));
        else
          tok = skip_excess_element(tok);
      }
      init->ty = ty;
      return;
    }
    init->expr = assign(rest, tok);
    check_assign(init->ty, init->expr, "initialization");
    return;
  }

  // A complex number, from any number. Braces are a scalar's, as with
  // gcc: `{1, 2}` is not the parts.
  if (is_complex(init->ty)) {
    if (equal(tok, "{")) {
      initializer2(&tok, tok->next, init);
      *rest = skip(tok, "}");
      return;
    }
    Node *expr = assign(rest, tok);
    add_type(expr);
    if (!is_numeric(expr->ty) && !is_complex(expr->ty))
      error_tok(expr->tok, "cannot convert '%s' to '%s' in initialization",
                type_name(expr->ty), type_name(init->ty));
    init->expr = new_cast(expr, init->ty);
    return;
  }

  if (init->ty->kind == TY_STRUCT) {
    if (equal(tok, "{")) {
      struct_initializer1(rest, tok, init);
      return;
    }

    // A struct can be initialized with another struct. E.g.
    // `struct T x = y;` where y is a variable of type `struct T`.
    // Handle that case first.
    Node *expr = assign(rest, tok);
    add_type(expr);
    if (expr->ty->kind == TY_STRUCT) {
      check_assign(init->ty, expr, "initialization");
      init->expr = expr;
      return;
    }

    struct_initializer2(rest, tok, init, init->ty->members, false);
    return;
  }

  if (init->ty->kind == TY_UNION) {
    // Likewise, a union with another union
    if (!equal(tok, "{")) {
      Node *expr = assign(rest, tok);
      add_type(expr);
      if (expr->ty->kind == TY_UNION) {
        check_assign(init->ty, expr, "initialization");
        init->expr = expr;
        return;
      }
    }
    union_initializer(rest, tok, init);
    return;
  }

  if (equal(tok, "{")) {
    // An initializer for a scalar variable can be surrounded by
    // braces. E.g. `int x = {3};`. Handle that case. More values after
    // the first are skipped, as an array's excess elements are.
    initializer2(&tok, tok->next, init);
    while (equal(tok, ",") && !equal(tok->next, "}"))
      tok = skip_excess_element(tok->next);
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
    return;
  }

  init->expr = assign(rest, tok);
  check_assign(init->ty, init->expr, "initialization");
}

static Type *copy_struct_type(Type *ty) {
  ty = copy_type(ty);

  Member head = {};
  Member *cur = &head;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    Member *m = arena_alloc(sizeof(Member));
    *m = *mem;
    cur = cur->next = m;
  }

  ty->members = head.next;
  return ty;
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty) {
  Initializer *init = new_initializer(ty, true);
  initializer2(rest, tok, init);

  if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
    ty = copy_struct_type(ty);

    Member *mem = ty->members;
    while (mem->next)
      mem = mem->next;
    mem->ty = mem_init(init, mem)->ty;
    ty->size += mem->ty->size;

    *new_ty = ty;
    return init;
  }

  *new_ty = init->ty;
  return init;
}

//---------- Local variable initializers -------------------------------------

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
  if (desg->var)
    return new_var_node(desg->var, tok);

  if (desg->member) {
    Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
    node->member = desg->member;
    return node;
  }

  Node *lhs = init_desg_expr(desg->next, tok);
  Node *rhs = new_num(desg->idx, tok);
  return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

// Assignments of what `init` sets. What it doesn't set is zeroed first
// (see lvar_initializer()), so it makes none for that.
static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
  if (!init)
    return new_node(ND_NULL_EXPR, tok);

  if (ty->kind == TY_ARRAY) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    if (init->str) {
      int len = MIN(ty->array_len, init->str->ty->array_len);
      for (int i = 0; i < len; i++) {
        if (!str_elem(init, i))
          continue;
        InitDesg desg2 = {desg, i};
        Node *lhs = init_desg_expr(&desg2, tok);
        Node *rhs = new_binary(ND_ASSIGN, lhs, new_num(str_elem(init, i), tok), tok);
        node = new_binary(ND_COMMA, node, rhs, tok);
      }
    }
    for (EmbedRun *run = init->embeds; run; run = run->next) {
      for (int j = 0; j < run->tok->embed->len; j++) {
        InitDesg desg2 = {desg, run->idx + j};
        Node *lhs = init_desg_expr(&desg2, tok);
        Node *rhs = new_binary(ND_ASSIGN, lhs, new_num(embed_elem(run, j, ty->base), tok), tok);
        node = new_binary(ND_COMMA, node, rhs, tok);
      }
    }
    for (int i = 0; init->children && i < ty->array_len; i++) {
      if (!init->children[i])
        continue;
      InitDesg desg2 = {desg, i};
      Node *rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  // A vector's elements, through a pointer to the first
  if (is_vector(ty) && !init->expr) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    for (int i = 0; init->children && i < ty->array_len; i++) {
      if (!init->children[i] || !init->children[i]->expr)
        continue;
      Node *addr = new_cast(new_unary(ND_ADDR, init_desg_expr(desg, tok), tok),
                            pointer_to(ty->elem));
      Node *lhs = new_unary(ND_DEREF, new_add(addr, new_num(i, tok), tok), tok);
      Node *rhs = new_binary(ND_ASSIGN, lhs, init->children[i]->expr, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_STRUCT && !init->expr) {
    Node *node = new_node(ND_NULL_EXPR, tok);

    for (Member *mem = ty->members; mem; mem = mem->next) {
      InitDesg desg2 = {desg, 0, mem};
      Node *rhs = create_lvar_init(get_child(init, mem->idx), mem->ty, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_UNION && !init->expr) {
    Member *mem = init->mem ? init->mem : ty->members;
    if (!mem) // an empty union (GNU)
      return new_node(ND_NULL_EXPR, tok);
    InitDesg desg2 = {desg, 0, mem};
    return create_lvar_init(get_child(init, mem->idx), mem->ty, &desg2, tok);
  }

  if (!init->expr)
    return new_node(ND_NULL_EXPR, tok);

  Node *lhs = init_desg_expr(desg, tok);
  return new_binary(ND_ASSIGN, lhs, init->expr, tok);
}

// A variable definition with an initializer is a shorthand notation
// for a variable definition followed by assignments. This function
// generates assignment expressions for an initializer. For example,
// `int x[2][2] = {{6, 7}, {8, 9}}` is converted to the following
// expressions:
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var) {
  Initializer *init = initializer(rest, tok, var->ty, &var->ty);
  InitDesg desg = {NULL, 0, NULL, var};

  // A scalar with a value, like `int x = 5`, or a struct from another,
  // is just assigned it.
  if (init->expr && !init->children)
    return create_lvar_init(init, var->ty, &desg, tok);

  // If a partial initializer list is given, the standard requires
  // that unspecified elements are set to 0. Here, we simply
  // zero-initialize the entire memory region of a variable before
  // initializing it with user-supplied values.
  Node *lhs = new_node(ND_MEMZERO, tok);
  lhs->var = var;

  Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
  return new_binary(ND_COMMA, lhs, rhs, tok);
}

//---------- Global variable initializers ------------------------------------

// The `sz`-byte little-endian integer at `buf` (sz is 1 to 8).
static uint64_t read_buf(char *buf, int sz) {
  uint64_t val = 0;
  for (int i = sz - 1; i >= 0; i--)
    val = val << 8 | (uint8_t)buf[i];
  return val;
}

static void write_buf(char *buf, uint64_t val, int sz) {
  for (int i = 0; i < sz; i++)
    buf[i] = val >> (i * 8);
}

// Puts bit-field `mem`'s value from `init` into the struct at `buf`,
// converted to its type first: 2 in a _Bool bit-field is 1.
static void write_bitfield(Initializer *init, Member *mem, char *buf) {
  if (!init || !init->expr)
    return;
  Node *expr = new_cast(init->expr, mem->ty);
  unsigned __int128 val;
  if (is_int128(mem->ty)) {
    if (!is_const_int128(expr))
      error_tok(init->expr->tok, "not a compile-time constant");
    val = eval128(expr);
  } else {
    val = (uint64_t)eval(expr);
  }

  // A unit is up to 16 bytes (see is_wide() in cgen.c).
  unsigned __int128 mask = ~(unsigned __int128)0 >> (128 - mem->bit_width);
  val = (val & mask) << mem->bit_offset;
  char *loc = buf + mem->offset;
  for (int i = 0; i < mem->unit; i++)
    loc[i] |= val >> (i * 8);
}

static Relocation *
write_gvar_data(Relocation *cur, Initializer *init, Type *ty, char *buf, int64_t offset) {
  // Nothing sets it: it stays zero.
  if (!init)
    return cur;

  if (ty->kind == TY_ARRAY) {
    int64_t sz = ty->base->size;
    if (init->str) {
      int len = MIN(ty->array_len, init->str->ty->array_len);
      for (int i = 0; i < len; i++)
        write_buf(buf + offset + sz * i, str_elem(init, i), sz);
    }
    for (EmbedRun *run = init->embeds; run; run = run->next)
      for (int j = 0; j < run->tok->embed->len; j++)
        write_buf(buf + offset + sz * (run->idx + j), embed_elem(run, j, ty->base), sz);
    for (int64_t i = 0; init->children && i < ty->array_len; i++)
      cur = write_gvar_data(cur, init->children[i], ty->base, buf, offset + sz * i);
    return cur;
  }

  if (is_vector(ty)) {
    if (init->expr)
      error_tok(init->expr->tok, "a vector initializer must be its elements in braces");
    for (int i = 0; init->children && i < ty->array_len; i++)
      cur = write_gvar_data(cur, init->children[i], ty->elem, buf,
                            offset + ty->elem->size * i);
    return cur;
  }

  if (is_complex(ty) && init->expr) {
    long double re, im;
    eval_complex(init->expr, &re, &im);
    Type *part = complex_part(ty);
    char *p = buf + offset;
    if (part->kind == TY_FLOAT) {
      ((float *)p)[0] = re;
      ((float *)p)[1] = im;
    } else if (part->kind == TY_DOUBLE) {
      ((double *)p)[0] = re;
      ((double *)p)[1] = im;
    } else {
      ((long double *)p)[0] = re;
      ((long double *)p)[1] = im;
    }
    return cur;
  }

  if (ty->kind == TY_STRUCT) {
    for (Member *mem = ty->members; mem; mem = mem->next) {
      if (mem->is_bitfield)
        write_bitfield(get_child(init, mem->idx), mem, buf + offset);
      else
        cur = write_gvar_data(cur, get_child(init, mem->idx), mem->ty, buf,
                              offset + mem->offset);
    }
    return cur;
  }

  if (ty->kind == TY_UNION) {
    if (!init->mem)
      return cur;
    if (init->mem->is_bitfield) {
      write_bitfield(get_child(init, init->mem->idx), init->mem, buf + offset);
      return cur;
    }
    return write_gvar_data(cur, get_child(init, init->mem->idx),
                           init->mem->ty, buf, offset);
  }

  if (!init->expr)
    return cur;

  // A complex number as a real one is its real part (see eval2's ND_CAST).
  add_type(init->expr);
  if (is_complex(init->expr->ty))
    init->expr = new_cast(init->expr, ty);

  if (ty->kind == TY_FLOAT) {
    *(float *)(buf + offset) = eval_double(init->expr);
    return cur;
  }

  if (ty->kind == TY_DOUBLE) {
    *(double *)(buf + offset) = eval_double(init->expr);
    return cur;
  }

  if (ty->kind == TY_LDOUBLE) {
    *(long double *)(buf + offset) = eval_double(init->expr);
    return cur;
  }

  // A bool is 0 or 1, not the low byte of the value (see eval2's ND_CAST).
  if (ty->kind == TY_BOOL) {
    char **label = NULL;
    buf[offset] = eval2(new_cast(init->expr, ty_bool), &label);
    return cur;
  }

  // A 128-bit integer, of any value (see is_const_int128())
  add_type(init->expr);
  if (is_int128(ty)) {
    if (!is_const_int128(init->expr))
      error_tok(init->expr->tok, "not a compile-time constant");
    unsigned __int128 val = eval128(init->expr);
    write_buf(buf + offset, (uint64_t)val, 8);
    write_buf(buf + offset + 8, (uint64_t)(val >> 64), 8);
    return cur;
  }

  // A floating value converted as its type says: 1e19 to unsigned long.
  if (is_flonum(init->expr->ty)) {
    write_buf(buf + offset, eval(new_cast(init->expr, ty)), ty->size);
    return cur;
  }

  char **label = NULL;
  uint64_t val = eval2(init->expr, &label);

  if (!label) {
    write_buf(buf + offset, val, ty->size);
    return cur;
  }

  Relocation *rel = arena_alloc(sizeof(Relocation));
  rel->offset = offset;
  rel->label = label;
  rel->addend = val;
  cur->next = rel;
  return cur->next;
}

// Initializers for global variables are evaluated at compile-time and
// embedded to .data section. This function serializes Initializer
// objects to a flat byte array. It is a compile error if an
// initializer list contains a non-constant expression.
static void gvar_initializer(Token **rest, Token *tok, Obj *var) {
  Type *decl_ty = var->ty;
  Type *ty;
  Initializer *init = initializer(rest, tok, decl_ty, &ty);

  Relocation head = {};
  char *buf = calloc(1, ty->size);
  write_gvar_data(&head, init, ty, buf, 0);
  var->init_data = buf;
  var->rel = head.next;

  // An initialized flexible array member makes the object larger than
  // its type, which sizeof still gives, as with gcc.
  if ((decl_ty->kind == TY_STRUCT || decl_ty->kind == TY_UNION) && decl_ty->is_flexible)
    var->flex_size = ty->size - decl_ty->size;
  else
    var->ty = ty;
}

//---------- Type names and _Static_assert -----------------------------------

// Returns true if a given token represents a type.
static bool is_typename(Token *tok) {
  static HashMap map;

  if (map.capacity == 0) {
    static char *kw[] = {
      "void", "_Bool", "char", "short", "int", "long", "__int128", "struct",
      "union", "typedef", "enum", "static", "extern", "_Alignas", "signed",
      "unsigned", "const", "volatile", "auto", "__auto_type", "register", "restrict",
      "__restrict", "__restrict__", "_Noreturn", "float", "double", "typeof",
      "_Complex", "__complex__", "__complex",
      "__typeof_unqual__", "inline",
      "_Thread_local", "__thread", "_Atomic", "constexpr", "__attribute__",
      "__attribute",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
      hashmap_put(&map, kw[i], (void *)1);
  }

  // Before C23, `constexpr` is an ordinary name.
  if (equal(tok, "constexpr") && tok->kind != TK_KEYWORD)
    return find_typedef(tok);
  return hashmap_get2(&map, tok->loc, tok->len) || find_typedef(tok);
}

// static-assert = "_Static_assert" "(" const-expr ("," string-literal)? ")" ";"
//
// Checked at compile time and generates no code. Allowed at file scope,
// in blocks and among struct members. (The message is optional since
// C23, which also spells it static_assert; see init_macros().)
static Token *static_assertion(Token *tok) {
  Token *start = tok;
  tok = skip(tok->next, "(");
  int64_t val = const_expr(&tok, tok);

  char *msg = NULL;
  if (consume(&tok, tok, ",")) {
    if (tok->kind != TK_STR)
      error_tok(tok, "expected a string literal");
    msg = tok->str;
    tok = tok->next;
  }
  tok = skip(tok, ")");
  tok = skip(tok, ";");

  if (!val) {
    if (msg)
      error_tok(start, "static assertion failed: %s", msg);
    error_tok(start, "static assertion failed");
  }
  return tok;
}

//---------- asm statements with operands ------------------------------------
//
// GNU extended asm: `asm("add %1, %0" : "+r"(x) : "r"(y))`. Every
// operand's value or address is first computed into a temporary local
// (a function with an asm keeps all its variables in memory, see cgen.c),
// then loaded into the register it gets here, and outputs are stored
// back after the asm. No two operands share a register, which also
// keeps early-clobber ('&') outputs apart from the inputs.

#define ASM_GENERAL 0xffcf // every register but %rsp and %rbp

// The registers constraint letter `c` allows, or 0 if it isn't a
// register constraint.
static int asm_reg_class(char c) {
  switch (c) {
  case 'a': return 1 << 0;
  case 'b': return 1 << 3;
  case 'c': return 1 << 1;
  case 'd': return 1 << 2;
  case 'S': return 1 << 6;
  case 'D': return 1 << 7;
  case 'q': case 'Q': return 0xf;
  case 'R': return 0xcf;
  case 'r': case 'g': case 'X': return ASM_GENERAL;
  }
  return 0;
}

// The order registers are handed out in: the caller-saved ones first,
// since the callee-saved ones have to be saved in the prologue.
static int asm_reg_order[] = {0, 1, 2, 6, 7, 8, 9, 10, 11, 3, 12, 13, 14, 15};

static int asm_pick_reg(int allowed, int used) {
  for (int i = 0; i < sizeof(asm_reg_order) / sizeof(*asm_reg_order); i++) {
    int r = asm_reg_order[i];
    if ((allowed & (1 << r)) && !(used & (1 << r)))
      return r;
  }
  return -1;
}

static bool is_one_reg(int regs) {
  return regs && !(regs & (regs - 1));
}

static int reg_of(int regs) {
  int r = 0;
  while (!(regs & (1 << r)))
    r++;
  return r;
}

static bool is_asm_lvalue(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    return node->ty->kind != TY_FUNC;
  case ND_DEREF:
    return true;
  case ND_MEMBER:
    return !node->member->is_bitfield;
  }
  return false;
}

// A constant: an integer constant expression, or the address of a
// global, as in "i"(&x).
static bool is_asm_constant(Node *node) {
  if (is_const_expr(node))
    return true;
  while (node->kind == ND_CAST)
    node = node->lhs;
  if (node->kind == ND_ADDR)
    node = node->lhs;
  return node->kind == ND_VAR && !node->var->is_local;
}

// A hidden local that holds an operand's value or address; `*init` gets
// the statement that sets it to `val`.
static Obj *asm_temp(Type *ty, Node *val, Node **init) {
  Obj *var = new_lvar("", ty);
  var->is_used = true;
  Node *set = new_binary(ND_ASSIGN, new_var_node(var, val->tok), val, val->tok);
  add_type(set);
  *init = (*init)->next = new_unary(ND_EXPR_STMT, set, val->tok);
  return var;
}

// asm-operands = (asm-operand ("," asm-operand)*)?
// asm-operand  = ("[" ident "]")? string-literal "(" expr ")"
static Token *asm_operands(Token *tok, AsmOperand *ops, Node **exprs, int *n,
                           bool is_output) {
  for (bool first = true; !equal(tok, ":") && !equal(tok, ")"); first = false) {
    if (!first)
      tok = skip(tok, ",");
    if (*n == 30)
      error_tok(tok, "an asm statement can have at most 30 operands");
    AsmOperand *op = &ops[*n];
    if (equal(tok, "[")) {
      op->name = get_ident(tok->next);
      tok = skip(tok->next->next, "]");
    }
    if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
      error_tok(tok, "expected an asm constraint string");
    op->tok = tok;
    op->is_output = is_output;
    tok = skip(tok->next, "(");
    exprs[(*n)++] = expr(&tok, tok);
    tok = skip(tok, ")");
  }
  return tok;
}

// Reads operand `op`'s constraint: sets its kind, and returns the
// registers it allows; `*match` is the output an input shares, or -1.
static int asm_constraint(AsmOperand *op, int noutputs, int *match) {
  char *s = op->tok->str;
  if (op->is_output) {
    if (*s == '+')
      op->is_rw = true;
    else if (*s != '=')
      error_tok(op->tok, "an output operand's constraint must start with '=' or '+'");
    s++;
  }

  int regs = 0;
  bool mem = false, imm = false, sse = false;
  *match = -1;
  for (; *s; s++) {
    if (strchr("&%?!", *s))
      continue;
    if (*s == '*') {
      if (s[1])
        s++;
      continue;
    }
    if (isdigit(*s)) {
      *match = strtol(s, &s, 10);
      s--;
      continue;
    }
    if (*s == ',')
      error_tok(op->tok, "alternative asm constraints are not supported");
    if (*s == '=' || *s == '+')
      error_tok(op->tok, "'%c' must come first in an output's constraint", *s);
    if (asm_reg_class(*s)) {
      regs |= asm_reg_class(*s);
      continue;
    }
    if (*s == 'x' || *s == 'v') {
      sse = true;
      continue;
    }
    if (strchr("moV<>", *s)) {
      mem = true;
      continue;
    }
    if (strchr("insIJKLMNOeZ", *s)) {
      imm = true;
      continue;
    }
    error_tok(op->tok, "asm constraint '%c' is not supported", *s);
  }

  if (*match >= 0) {
    if (op->is_output || *match >= noutputs)
      error_tok(op->tok, "a matching constraint must name an output");
    op->kind = 'r';
    return 0;
  }
  op->kind = regs ? 'r' : sse ? 'x' : mem ? 'm' : imm ? 'i' : 0;
  if (!op->kind)
    error_tok(op->tok, "empty asm constraint");
  if (op->kind == 'i' && op->is_output)
    error_tok(op->tok, "an output can't be a constant");
  return regs;
}

// clobbers = (string-literal ("," string-literal)*)?
// Returns the general registers named, and in `*xmm` the SSE ones. mucc
// keeps nothing in memory caches, flags or other registers from one
// statement to the next, so "memory", "cc" and the rest need nothing.
static int asm_clobbers(Token **rest, Token *tok, int *xmm) {
  int regs = 0;
  for (bool first = true; !equal(tok, ":") && !equal(tok, ")"); first = false) {
    if (!first)
      tok = skip(tok, ",");
    if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
      error_tok(tok, "expected a string literal");
    char *name = tok->str + (tok->str[0] == '%');
    int r = gp_reg_number(name);
    if (r == 4 || r == 5)
      error_tok(tok, "an asm statement can't clobber %s", name);
    if (r >= 0)
      regs |= 1 << r;
    else if (!strncmp(name, "xmm", 3) && isdigit(name[3]) && atoi(name + 3) < 16)
      *xmm |= 1 << atoi(name + 3);
    else if (strcmp(name, "memory") && strcmp(name, "cc") &&
             strcmp(name, "dirflag") && strcmp(name, "fpsr") &&
             strcmp(name, "flags") && strncmp(name, "xmm", 3) &&
             strncmp(name, "ymm", 3) && strncmp(name, "mm", 2) &&
             strncmp(name, "st", 2))
      error_tok(tok, "unknown register name '%s' in asm", name);
    tok = tok->next;
  }
  *rest = tok;
  return regs;
}

// asm-labels = ":" ident ("," ident)*
// asm goto's labels, each a goto, matched with its label and given the
// cleanups to run on the way there in resolve_goto_labels(). Each node's
// token is the one before the label, as for `goto label`.
static Token *asm_labels(Token *tok, Node *node) {
  Node head = {};
  Node *cur = &head;
  do {
    Node *g = cur = cur->next = new_node(ND_GOTO, tok);
    g->label = get_ident(tok->next);
    g->cleanups = cleanups;
    g->goto_next = gotos;
    gotos = g;
    tok = tok->next->next;
  } while (equal(tok, ","));
  node->asm_labels = head.next;
  return tok;
}

// asm-stmt = "asm" ("volatile" | "inline" | "goto")* "(" string-literal
//            (":" asm-operands (":" asm-operands (":" clobbers
//            asm-labels?)?)?)? ")"
static Node *asm_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_ASM, tok);
  tok = tok->next;

  bool is_goto = false;
  while (equal(tok, "volatile") || equal(tok, "inline") || equal(tok, "goto")) {
    is_goto |= equal(tok, "goto");
    tok = tok->next;
  }

  tok = skip(tok, "(");
  if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
    error_tok(tok, "expected string literal");
  node->asm_str = tok->str;
  tok = tok->next;

  if (!equal(tok, ":")) {
    *rest = skip(tok, ")");
    return node;
  }
  node->asm_extended = true;

  AsmOperand ops[30] = {};
  Node *exprs[30];
  int n = 0, clobbered = 0, xmm_used = 0;
  tok = asm_operands(tok->next, ops, exprs, &n, true);
  int noutputs = n;
  if (consume(&tok, tok, ":")) {
    tok = asm_operands(tok, ops, exprs, &n, false);
    if (consume(&tok, tok, ":"))
      clobbered = asm_clobbers(&tok, tok, &xmm_used);
  }
  if (equal(tok, ":")) {
    if (!is_goto)
      error_tok(tok, "labels in an asm statement need 'asm goto'");
    tok = asm_labels(tok, node);
  }
  *rest = skip(tok, ")");

  // Each operand's kind and type, and the temporaries its value or
  // address goes in.
  int allowed[30], match[30];
  Node head = {};
  Node *init = &head;
  for (int i = 0; i < n; i++) {
    AsmOperand *op = &ops[i];
    Node *e = exprs[i];
    add_type(e);
    allowed[i] = asm_constraint(op, noutputs, &match[i]);
    if (match[i] >= 0 && ops[match[i]].kind != 'r' && ops[match[i]].kind != 'x')
      error_tok(op->tok, "a matching constraint must name a register output");
    if (match[i] >= 0)
      op->kind = ops[match[i]].kind;

    // `register long x asm("r10")` as a register operand goes in r10.
    if (allowed[i] == ASM_GENERAL && e->kind == ND_VAR && e->var->asm_reg)
      allowed[i] = 1 << (e->var->asm_reg - 1);

    Type *ty = e->ty;
    if (!op->is_output && (ty->kind == TY_ARRAY || ty->kind == TY_FUNC)) {
      ty = pointer_to(ty->kind == TY_ARRAY ? ty->base : ty);
      e = new_cast(new_unary(ND_ADDR, e, e->tok), ty);
    }
    op->ty = ty;

    if (op->kind == 'r' && !is_integer(ty) && ty->kind != TY_PTR)
      error_tok(op->tok, "an operand of type '%s' can't go in a general register",
                type_name(ty));
    if (op->kind == 'x' && ty->kind != TY_FLOAT && ty->kind != TY_DOUBLE && !is_vector(ty) &&
        !((is_integer(ty) || ty->kind == TY_PTR) && (ty->size == 4 || ty->size == 8)))
      error_tok(op->tok, "an operand of type '%s' can't go in an SSE register",
                type_name(ty));

    if (op->kind == 'i') {
      if (!is_asm_constant(e))
        error_tok(e->tok, "an asm constant operand must be a constant");
      op->val = eval2(e, &op->label);
    } else if (op->is_output || op->kind == 'm') {
      if (!is_asm_lvalue(e))
        error_tok(e->tok, op->is_output ? "an asm output must be an lvalue"
                                        : "an asm memory operand must be an lvalue");
      op->addr = asm_temp(pointer_to(ty), new_unary(ND_ADDR, e, e->tok), &init);
    } else {
      op->value = asm_temp(ty, e, &init);
    }
  }
  node->body = head.next;

  // Registers: first the operands that need a particular one, then the
  // rest, then inputs that share an output's. An input may be in the
  // same register as an output, as in `"=a"(ret) : "a"(nr)`, unless the
  // output is early-clobber ('&'): written before the inputs are read.
  int outs = 0, ins = 0, early = 0;
  for (int i = 0; i < n; i++) {
    if (ops[i].kind != 'r' || !is_one_reg(allowed[i]))
      continue;
    int r = reg_of(allowed[i]);
    int taken = clobbered | (ops[i].is_output ? outs : ins | early);
    if (taken & (1 << r))
      error_tok(ops[i].tok, "register %s is used twice in this asm statement",
                gp_reg_name(r, 8));
    ops[i].reg = r;
    if (!ops[i].is_output || ops[i].is_rw)
      ins |= 1 << r;
    if (ops[i].is_output)
      outs |= 1 << r;
    if (ops[i].is_output && strchr(ops[i].tok->str, '&'))
      early |= 1 << r;
  }
  int used = clobbered | outs | ins;
  for (int i = 0; i < n; i++) {
    bool needs = (ops[i].kind == 'r' && match[i] < 0 && !is_one_reg(allowed[i])) ||
                 (ops[i].kind == 'm' && ops[i].addr);
    if (!needs)
      continue;
    int r = asm_pick_reg(ops[i].kind == 'm' ? ASM_GENERAL : allowed[i], used);
    if (r < 0)
      error_tok(ops[i].tok, "not enough registers for this asm statement's operands");
    ops[i].reg = r;
    used |= 1 << r;
  }
  // SSE registers: one each, but for inputs that share an output's
  for (int i = 0; i < n; i++) {
    if (ops[i].kind != 'x' || match[i] >= 0)
      continue;
    int r = 0;
    while (r < 16 && (xmm_used & (1 << r)))
      r++;
    if (r == 16)
      error_tok(ops[i].tok, "not enough SSE registers for this asm statement's operands");
    ops[i].reg = r;
    xmm_used |= 1 << r;
  }

  for (int i = 0; i < n; i++)
    if (match[i] >= 0)
      ops[i].reg = ops[match[i]].reg;

  // After the asm, outputs are stored through a register none of them is in.
  int outputs = 0;
  for (int i = 0; i < noutputs; i++)
    if (ops[i].kind == 'r')
      outputs |= 1 << ops[i].reg;
  node->asm_scratch = asm_pick_reg(ASM_GENERAL, outputs);
  current_fn->asm_regs |= used | (1 << node->asm_scratch);

  node->asm_ops = calloc(n, sizeof(AsmOperand));
  memcpy(node->asm_ops, ops, n * sizeof(AsmOperand));
  node->asm_nops = n;
  return node;
}

//---------- Statements: if, switch, loops, jumps and blocks -----------------

// The condition of an if, a loop or ?:, which starts at `start`: a number
// or a pointer, and the warnings about conditions.
static void check_condition(Node *cond, Token *start) {
  check_scalar(cond);
  warn_assign_in_condition(cond, start);
  warn_address_condition(cond);
}

// A case label's value converted to the promoted type of the switch's
// controlling expression `ty`, as C requires: all 64 bits for a 64-bit
// switch, 32 otherwise.
// `val` converted to a switch's type `ty`, as a case's value is stored
static int64_t to_case_type(Type *ty, int64_t val) {
  if (ty->size >= 8)
    return val;
  return (ty->is_unsigned && ty->size == 4) ? (int64_t)(uint32_t)val : (int32_t)val;
}

// A case's value, converted to the switch's type `ty`. For __int128, *hi
// gets its high half.
static int64_t case_value(Token **rest, Token *tok, Type *ty, int64_t *hi) {
  Node *node = conditional(rest, tok);
  *hi = 0;
  if (!is_int128(ty))
    return to_case_type(ty, eval(node));

  if (!is_const_int128(node))
    error_tok(node->tok, "not a compile-time constant");
  unsigned __int128 val = eval128(node);
  *hi = val >> 64;
  return val;
}

// Is case value a <= b, in a switch on type `ty`? (hi: see case_value())
static bool case_le(Type *ty, int64_t a, int64_t a_hi, int64_t b, int64_t b_hi) {
  bool uns = ty->is_unsigned && ty->size >= 4;
  if (!is_int128(ty))
    return uns ? (uint64_t)a <= (uint64_t)b : a <= b;
  unsigned __int128 ua = (unsigned __int128)(uint64_t)a_hi << 64 | (uint64_t)a;
  unsigned __int128 ub = (unsigned __int128)(uint64_t)b_hi << 64 | (uint64_t)b;
  return uns ? ua <= ub : (__int128)ua <= (__int128)ub;
}

// Does `node` hold a label or case that a jump could reach from outside?
static bool has_label(Node *node) {
  if (!node)
    return false;
  if (node->kind == ND_LABEL || node->kind == ND_CASE)
    return true;
  if (has_label(node->lhs) || has_label(node->rhs))
    return true;
  if (!has_stmt_fields(node->kind))
    return false;
  if (has_label(node->cond) || has_label(node->then) || has_label(node->els) ||
      has_label(node->init) || has_label(node->inc))
    return true;
  for (Node *n = node->body; n; n = n->next)
    if (has_label(n))
      return true;
  return false;
}

// stmt = "return" expr? ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" const-expr ("..." const-expr)? ":" stmt
//      | "default" ":" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "do" stmt "while" "(" expr ")" ";"
//      | "asm" asm-stmt
//      | "goto" (ident | "*" expr) ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt
//      | expr-stmt
static Node *stmt(Token **rest, Token *tok) {
  if (equal(tok, "return")) {
    Node *node = new_node(ND_RETURN, tok);
    Type *ty = current_fn->ty->return_ty;

    if (consume(rest, tok->next, ";")) {
      if (ty->kind != TY_VOID)
        error_tok(tok, "'return' needs a value in function returning '%s'",
                  type_name(ty));
      return return_with_cleanups(node);
    }

    Node *exp = expr(&tok, tok->next);
    *rest = skip(tok, ";");

    add_type(exp);
    if (ty->kind == TY_VOID) {
      // `return f();` is fine when f() itself returns void.
      if (exp->ty->kind != TY_VOID)
        error_tok(exp->tok, "void function '%s' should not return a value",
                  current_fn->name);
    } else {
      check_assign(ty, exp, "return");
      warn_return_local(exp);
    }

    if ((ty->kind != TY_STRUCT && ty->kind != TY_UNION) || is_complex(ty))
      exp = new_cast(exp, current_fn->ty->return_ty);

    node->lhs = exp;
    return return_with_cleanups(node);
  }

  if (equal(tok, "if")) {
    Node *node = new_node(ND_IF, tok);
    tok = skip(tok->next, "(");
    Token *cond_start = tok;
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(&tok, tok);
    if (equal(tok, "else"))
      node->els = stmt(&tok, tok->next);
    *rest = tok;

    // With a constant condition, only the branch that can run is
    // compiled, as gcc does even without optimizing: code like BusyBox's
    // `if (ENABLE_FEATURE) f();` names functions that don't exist when
    // the feature is off. A branch with a label stays, since a goto or a
    // case can still reach it.
    check_condition(node->cond, cond_start);
    if (is_integer(node->cond->ty) && is_const_expr(node->cond)) {
      if (eval(node->cond)) {
        if (node->els && !has_label(node->els))
          node->els = NULL;
      } else if (!has_label(node->then)) {
        node->then = new_node(ND_BLOCK, tok);
      }
    }
    return node;
  }

  if (equal(tok, "switch")) {
    Node *node = new_node(ND_SWITCH, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    add_type(node->cond);
    if (!is_integer(node->cond->ty))
      error_tok(node->cond->tok, "switch on '%s', which is not an integer",
                type_name(node->cond->ty));
    tok = skip(tok, ")");

    Node *sw = current_switch;
    current_switch = node;

    char *brk = brk_label;
    brk_label = node->brk_label = new_unique_name();
    Cleanup *brk_c = brk_cleanups, *case_c = case_cleanups;
    brk_cleanups = case_cleanups = cleanups;

    node->then = stmt(rest, tok);
    warn_switch(node);

    current_switch = sw;
    brk_label = brk;
    brk_cleanups = brk_c;
    case_cleanups = case_c;
    return node;
  }

  if (equal(tok, "case")) {
    if (!current_switch)
      error_tok(tok, "stray case");
    if (cleanups != case_cleanups)
      jump_into_scope(cleanups, tok);

    Node *node = new_node(ND_CASE, tok);
    Type *ty = current_switch->cond->ty;
    Token *start = tok->next;
    int64_t begin_hi, end_hi;
    int64_t begin = case_value(&tok, tok->next, ty, &begin_hi);
    int64_t end = begin;
    end_hi = begin_hi;

    // [GNU] Case ranges, e.g. "case 1 ... 5:"
    bool is_range = equal(tok, "...");
    if (is_range)
      end = case_value(&tok, tok->next, ty, &end_hi);

    if (is_range && !case_le(ty, begin, begin_hi, end, end_hi))
      error_tok(tok, "empty case range specified");

    // A value that an earlier case has too is an error, as C requires.
    // (configure scripts rely on it: Tcl's tests sizeof(long) that way.)
    for (Node *n = current_switch->case_next; n; n = n->case_next)
      if (case_le(ty, begin, begin_hi, n->end, n->end_hi) &&
          case_le(ty, n->begin, n->begin_hi, end, end_hi))
        error_tok(start, "duplicate case value");

    tok = skip(tok, ":");
    node->label = new_unique_name();
    node->begin = begin;
    node->end = end;
    node->begin_hi = begin_hi;
    node->end_hi = end_hi;
    node->case_next = current_switch->case_next;
    current_switch->case_next = node;
    node->lhs = label_body(rest, tok);
    return node;
  }

  if (equal(tok, "default")) {
    if (!current_switch)
      error_tok(tok, "stray default");
    if (cleanups != case_cleanups)
      jump_into_scope(cleanups, tok);

    if (current_switch->default_case)
      error_tok(tok, "multiple default labels in one switch");

    Node *node = new_node(ND_CASE, tok);
    tok = skip(tok->next, ":");
    node->label = new_unique_name();
    current_switch->default_case = node;
    node->lhs = label_body(rest, tok);
    return node;
  }

  if (equal(tok, "for")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");

    enter_scope();
    LexBlock *outer_block = current_block;
    enter_block(node);

    char *brk = brk_label;
    char *cont = cont_label;
    Cleanup *brk_c = brk_cleanups, *cont_c = cont_cleanups;
    Cleanup *outer = cleanups;
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();

    if (is_typename(tok)) {
      Type *basety = declspec(&tok, tok, NULL);
      node->init = declaration(&tok, tok, basety, NULL);
    } else {
      node->init = expr_stmt(&tok, tok);
    }
    brk_cleanups = cont_cleanups = cleanups;

    if (!equal(tok, ";")) {
      Token *cond_start = tok;
      node->cond = expr(&tok, tok);
      check_condition(node->cond, cond_start);
    }
    tok = skip(tok, ";");

    if (!equal(tok, ")"))
      node->inc = expr(&tok, tok);
    tok = skip(tok, ")");

    node->then = stmt(rest, tok);

    leave_scope();
    current_block = outer_block;
    brk_label = brk;
    cont_label = cont;
    brk_cleanups = brk_c;
    cont_cleanups = cont_c;

    // The loop's own variables, as in `for (int i ...; ...)`
    if (cleanups != outer) {
      Node *block = new_node(ND_BLOCK, tok);
      block->body = node;
      if (falls_through(node))
        node->next = cleanup_calls(cleanups, outer, tok);
      cleanups = outer;
      return block;
    }
    return node;
  }

  if (equal(tok, "while")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");
    Token *cond_start = tok;
    node->cond = expr(&tok, tok);
    check_condition(node->cond, cond_start);
    tok = skip(tok, ")");

    char *brk = brk_label;
    char *cont = cont_label;
    Cleanup *brk_c = brk_cleanups, *cont_c = cont_cleanups;
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();
    brk_cleanups = cont_cleanups = cleanups;

    node->then = stmt(rest, tok);

    brk_label = brk;
    cont_label = cont;
    brk_cleanups = brk_c;
    cont_cleanups = cont_c;
    return node;
  }

  if (equal(tok, "do")) {
    Node *node = new_node(ND_DO, tok);

    char *brk = brk_label;
    char *cont = cont_label;
    Cleanup *brk_c = brk_cleanups, *cont_c = cont_cleanups;
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();
    brk_cleanups = cont_cleanups = cleanups;

    node->then = stmt(&tok, tok->next);

    brk_label = brk;
    cont_label = cont;
    brk_cleanups = brk_c;
    cont_cleanups = cont_c;

    tok = skip(tok, "while");
    tok = skip(tok, "(");
    Token *cond_start = tok;
    node->cond = expr(&tok, tok);
    check_condition(node->cond, cond_start);
    tok = skip(tok, ")");
    *rest = skip(tok, ";");
    return node;
  }

  if (equal(tok, "asm"))
    return asm_stmt(rest, tok);

  if (equal(tok, "goto")) {
    if (equal(tok->next, "*")) {
      // [GNU] `goto *ptr` jumps to the address specified by `ptr`.
      if (runs_cleanup_fn(cleanups, NULL)) // VLAs are just not freed
        error_tok(tok, "'goto *' in the scope of a variable with a cleanup is not supported");
      Node *node = new_node(ND_GOTO_EXPR, tok);
      node->lhs = expr(&tok, tok->next->next);
      *rest = skip(tok, ";");
      return node;
    }

    // Its cleanups are found with the label, in resolve_goto_labels().
    Node *node = new_node(ND_GOTO, tok);
    node->label = get_ident(tok->next);
    node->cleanups = cleanups;
    node->goto_next = gotos;
    gotos = node;
    *rest = skip(tok->next->next, ";");
    return node;
  }

  if (equal(tok, "break")) {
    if (!brk_label)
      error_tok(tok, "stray break");
    Node *node = new_node(ND_GOTO, tok);
    node->unique_label = brk_label;
    if (cleanups != brk_cleanups)
      node->lhs = cleanup_calls(cleanups, brk_cleanups, tok);
    *rest = skip(tok->next, ";");
    return node;
  }

  if (equal(tok, "continue")) {
    if (!cont_label)
      error_tok(tok, "stray continue");
    Node *node = new_node(ND_GOTO, tok);
    node->unique_label = cont_label;
    if (cleanups != cont_cleanups)
      node->lhs = cleanup_calls(cleanups, cont_cleanups, tok);
    *rest = skip(tok->next, ";");
    return node;
  }

  if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
    Node *node = new_node(ND_LABEL, tok);
    node->label = strndup(tok->loc, tok->len);
    for (Node *l = labels; l; l = l->goto_next)
      if (!strcmp(l->label, node->label))
        error_tok(tok, "duplicate label '%s'", node->label);
    node->unique_label = new_unique_name();
    node->cleanups = cleanups;
    // Attributes right after a label, like `unused`, are the label's.
    node->lhs = label_body(rest, skip_attributes(tok->next->next));
    node->goto_next = labels;
    labels = node;
    return node;
  }

  if (equal(tok, "{"))
    return compound_stmt(rest, tok->next, false);

  return expr_stmt(rest, tok);
}

// block-item = static-assert | typedef | declaration | stmt
//
// Returns NULL for items that generate no code, like a typedef.
static Node *block_item(Token **rest, Token *tok) {
  if (equal(tok, "_Static_assert")) {
    *rest = static_assertion(tok);
    return NULL;
  }

  if (!is_typename(tok) || equal(tok->next, ":"))
    return stmt(rest, tok);

  VarAttr attr = {};
  Type *basety = declspec(&tok, tok, &attr);

  if (attr.is_typedef) {
    *rest = parse_typedef(tok, basety, &attr);
    return NULL;
  }

  // Function declarations, maybe followed by variables: `int f(void), x;`
  while (is_function(tok, basety)) {
    tok = function(tok, basety, &attr);
    if (!equal(tok, ",")) {
      *rest = tok;
      return NULL;
    }
    tok = tok->next;
  }

  if (attr.is_extern) {
    *rest = global_variable(tok, basety, &attr);
    return NULL;
  }

  return declaration(rest, tok, basety, &attr);
}

// What follows `label:`, `case 1:` or `default:`. Before C23 that had to
// be a statement; C23 also allows a declaration, or the end of the
// block, as in `default: }`.
static Node *label_body(Token **rest, Token *tok) {
  if (equal(tok, "}")) {
    *rest = tok;
    return new_node(ND_BLOCK, tok);
  }

  Node *node = block_item(rest, tok);
  return node ? node : new_node(ND_BLOCK, tok);
}

// Parses a block item like block_item(). If it has an error, that's
// reported, the item is skipped and NULL returned, so the rest of the
// block is still checked. (See "Error recovery".)
static Node *block_item_or_skip(Token **rest, Token *tok) {
  ParserState saved = save_state();
  jmp_buf *outer = error_recovery;
  jmp_buf here;

  if (setjmp(here)) {
    error_recovery = outer;
    restore_state(saved);
    *rest = skip_bad_item(tok);
    return NULL;
  }

  error_recovery = &here;
  Node *node = block_item(rest, tok);
  if (node)
    add_type(node);
  error_recovery = outer;
  return node;
}

// compound-stmt = block-item* "}"
//
// The body of a statement expression (`is_stmt_expr`) has the value of
// its last statement, so it can't end with cleanups.
static Node *compound_stmt(Token **rest, Token *tok, bool is_stmt_expr) {
  Node *node = new_node(ND_BLOCK, tok);
  Node head = {};
  Node *cur = &head;
  Cleanup *outer = cleanups;

  enter_scope();
  LexBlock *outer_block = current_block;
  if (!is_fn_body)
    enter_block(node);
  is_fn_body = false;

  // Statements after one that can't finish (a return, a goto, `if (1)
  // return x;`) run only if a jump reaches them, through a label. Until
  // one, they're dropped, as gcc does: they may name functions that don't
  // exist (see the `if` in stmt()).
  bool dead = false;
  while (!equal(tok, "}") && tok->kind != TK_EOF) {
    Node *item = block_item_or_skip(&tok, tok);
    if (!item || (dead && !is_stmt_expr && !has_label(item)))
      continue;
    cur = cur->next = item;
    dead = !falls_through(item);
  }

  leave_scope();
  current_block = outer_block;

  if (!error_count)
    for (Node *n = head.next; n; n = n->next)
      if (!is_stmt_expr || n->next)
        warn_unused_value(n);

  node->body = head.next;
  if (cleanups != outer) {
    if (falls_through(node) && !is_stmt_expr)
      cur->next = cleanup_calls(cleanups, outer, tok);

    // A statement expression's value is kept in a variable while the
    // cleanup functions run, as with gcc. (Its VLAs are freed when the
    // function returns.)
    if (falls_through(node) && is_stmt_expr && runs_cleanup_fn(cleanups, outer)) {
      Obj *tmp = NULL;
      if (cur != &head && cur->kind == ND_EXPR_STMT)
        add_type(cur->lhs);
      if (cur != &head && cur->kind == ND_EXPR_STMT && cur->lhs->ty->kind != TY_VOID) {
        Type *ty = cur->lhs->ty;
        if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
          ty = pointer_to(ty->base);
        else if (ty->kind == TY_FUNC)
          ty = pointer_to(ty);
        tmp = new_lvar("", unqual(ty));
        cur->lhs = new_binary(ND_ASSIGN, new_var_node(tmp, tok), cur->lhs, tok);
      }
      for (Cleanup *c = cleanups; c != outer; c = c->next)
        if (c->fn)
          cur = cur->next = cleanup_call(c, tok);
      // Its value: the variable, or none, as before
      if (tmp)
        cur = cur->next = new_unary(ND_EXPR_STMT, new_var_node(tmp, tok), tok);
      else
        cur = cur->next = new_node(ND_BLOCK, tok);
      node->body = head.next;
    }
    cleanups = outer;
  }
  *rest = skip(tok, "}");
  return node;
}

// expr-stmt = expr? ";"
static Node *expr_stmt(Token **rest, Token *tok) {
  if (equal(tok, ";")) {
    *rest = tok->next;
    return new_node(ND_BLOCK, tok);
  }

  Node *node = new_node(ND_EXPR_STMT, tok);
  node->lhs = expr(&tok, tok);
  *rest = skip(tok, ";");
  return node;
}

//---------- Expressions -----------------------------------------------------

// expr = assign ("," expr)?
static Node *expr(Token **rest, Token *tok) {
  Node *node = assign(&tok, tok);

  if (equal(tok, ","))
    return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);

  *rest = tok;
  return node;
}

//---------- Constant expression evaluation ----------------------------------

static int64_t eval(Node *node) {
  return eval2(node, NULL);
}

// Whether constant `node` is nonzero: 0.5 is, though eval() would make it
// the integer 0.
static bool eval_truth(Node *node) {
  add_type(node);
  if (is_flonum(node->ty))
    return eval_double(node) != 0;
  if (is_int128(node->ty))
    return eval128(node) != 0;
  return eval(node) != 0;
}

// Is `node` an integer constant expression, with __int128 arithmetic of
// any value? (is_const_expr() takes a 128-bit one only if its value fits
// in 64 bits, as eval() computes it; static initializers and case labels
// of __int128 take any, by eval128().)
static bool is_const_int128(Node *node) {
  add_type(node);
  if (!is_int128(node->ty))
    return is_integer(node->ty) && is_const_expr(node);

  switch (node->kind) {
  case ND_CAST:
    return is_int128(node->lhs->ty) ? is_const_int128(node->lhs)
                                    : is_integer(node->lhs->ty) && is_const_expr(node->lhs);
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_MOD:
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR:
  case ND_SHL:
  case ND_SHR:
  case ND_COMMA:
    return is_const_int128(node->lhs) && is_const_int128(node->rhs);
  case ND_NEG:
  case ND_BITNOT:
    return is_const_int128(node->lhs);
  case ND_COND:
    if (!is_const_int128(node->cond))
      return false;
    return is_const_int128(eval_truth(node->cond) ? node->then : node->els);
  }
  return false;
}

// The value of `node`, for which is_const_int128() is true, in 128 bits:
// a narrower one is extended as its type says.
static __int128 eval128(Node *node) {
  add_type(node);
  if (!is_int128(node->ty)) {
    int64_t v = eval(node);
    return node->ty->is_unsigned ? (__int128)(uint64_t)v : v;
  }

  // Arithmetic as unsigned, which wraps
  bool u = node->ty->is_unsigned;
  unsigned __int128 a = 0, b = 0;
  if (node->lhs)
    a = eval128(node->lhs);
  if (node->rhs && node->kind != ND_COMMA)
    b = eval128(node->rhs);

  switch (node->kind) {
  case ND_CAST:
    return a;
  case ND_ADD:
    return a + b;
  case ND_SUB:
    return a - b;
  case ND_MUL:
    return a * b;
  case ND_DIV:
  case ND_MOD:
    if (b == 0)
      error_tok(node->tok, "division by zero in a constant");
    if (node->kind == ND_DIV)
      return u ? a / b : (unsigned __int128)((__int128)a / (__int128)b);
    return u ? a % b : (unsigned __int128)((__int128)a % (__int128)b);
  case ND_BITAND:
    return a & b;
  case ND_BITOR:
    return a | b;
  case ND_BITXOR:
    return a ^ b;
  case ND_SHL:
    return a << (b & 127);
  case ND_SHR:
    return u ? a >> (b & 127) : (unsigned __int128)((__int128)a >> (b & 127));
  case ND_NEG:
    return -a;
  case ND_BITNOT:
    return ~a;
  case ND_COMMA:
    return eval128(node->rhs);
  case ND_COND:
    return eval_truth(node->cond) ? eval128(node->then) : eval128(node->els);
  }
  error_tok(node->tok, "not a compile-time constant");
}

// `val` as integer type `ty` holds it: cut to its width and sign- or
// zero-extended, as the arithmetic would wrap at run time
static int64_t wrap_int(Type *ty, int64_t val) {
  switch (ty->size) {
  case 1: return ty->is_unsigned ? (uint8_t)val : (int8_t)val;
  case 2: return ty->is_unsigned ? (uint16_t)val : (int16_t)val;
  case 4:
    // Not with `?:`, which would make both arms unsigned int.
    if (ty->is_unsigned)
      return (uint32_t)val;
    return (int32_t)val;
  }
  return val;
}

static int64_t eval_wide(Node *node, char ***label);

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr+n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
//
// The arithmetic is done in 64 bits by eval_wide(), and the result cut
// to its type here, so `UINT32_MAX + 1u` is 0 and `~0u >> 4` is
// 0x0fffffff.
static int64_t eval2(Node *node, char ***label) {
  int64_t val = eval_wide(node, label);
  if (is_integer(node->ty) && node->ty->kind != TY_BOOL)
    return wrap_int(node->ty, val);
  return val;
}

// The string literal that pointer `node` points into, as `"xyz" + 1`
// does, with the byte offset in `*off`, or NULL. (The offset is converted
// to the pointer's type, by add_type().)
static Obj *string_at(Node *node, int64_t *off) {
  *off = 0;
  for (;;) {
    if (node->kind == ND_CAST && node->ty->base) {
      node = node->lhs;
    } else if ((node->kind == ND_ADD || node->kind == ND_SUB) && node->lhs->ty->base &&
               is_const_expr(node->rhs)) {
      *off += node->kind == ND_ADD ? eval(node->rhs) : -eval(node->rhs);
      node = node->lhs;
    } else {
      break;
    }
  }
  return node->kind == ND_VAR && node->var->is_string ? node->var : NULL;
}

static int64_t eval_wide(Node *node, char ***label) {
  add_type(node);

  // A 128-bit value's low 64 bits, as converting it to a narrower type
  // takes (see is_const_int128())
  if (is_int128(node->ty)) {
    if (!is_const_int128(node))
      error_tok(node->tok, "not a compile-time constant");
    return (int64_t)eval128(node);
  }

  if (is_flonum(node->ty))
    return eval_double(node);

  switch (node->kind) {
  case ND_ADD:
    return eval2(node->lhs, label) + eval(node->rhs);
  case ND_SUB: {
    // p - q is a number if both point into the same object, or neither
    // does (as in `(char *)&((T *)0)->m - (char *)0`, an offsetof), also
    // as integers: `(long)&a[1] - (long)&a[0]`.
    char **l1 = NULL, **l2 = NULL;
    int64_t val = eval2(node->lhs, &l1) - eval2(node->rhs, &l2);
    if (l2) {
      if (l1 != l2)
        error_tok(node->tok, "not a compile-time constant");
      return val;
    }
    if (l1) {
      if (!label)
        error_tok(node->tok, "not a compile-time constant");
      *label = l1;
    }
    return val;
  }
  case ND_MUL:
    return eval(node->lhs) * eval(node->rhs);
  case ND_DIV:
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) / eval(node->rhs);
    return eval(node->lhs) / eval(node->rhs);
  case ND_NEG:
    return -eval(node->lhs);
  case ND_MOD:
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) % eval(node->rhs);
    return eval(node->lhs) % eval(node->rhs);
  case ND_BITAND:
    return eval(node->lhs) & eval(node->rhs);
  case ND_BITOR:
    return eval(node->lhs) | eval(node->rhs);
  case ND_BITXOR:
    return eval(node->lhs) ^ eval(node->rhs);
  case ND_SHL:
    return eval(node->lhs) << eval(node->rhs);
  case ND_SHR:
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) >> eval(node->rhs);
    return eval(node->lhs) >> eval(node->rhs);
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    // Floating operands (as in `2.5 > 2.0`) are compared as such.
    if (is_flonum(node->lhs->ty)) {
      long double a = eval_double(node->lhs), b = eval_double(node->rhs);
      return node->kind == ND_EQ ? a == b : node->kind == ND_NE ? a != b :
             node->kind == ND_LT ? a < b : a <= b;
    }
    if (is_int128(node->lhs->ty)) {
      __int128 a = eval128(node->lhs), b = eval128(node->rhs);
      if (node->lhs->ty->is_unsigned)
        return node->kind == ND_EQ ? a == b : node->kind == ND_NE ? a != b :
               node->kind == ND_LT ? (unsigned __int128)a < (unsigned __int128)b
                                   : (unsigned __int128)a <= (unsigned __int128)b;
      return node->kind == ND_EQ ? a == b : node->kind == ND_NE ? a != b :
             node->kind == ND_LT ? a < b : a <= b;
    }
    if (node->kind == ND_EQ)
      return eval(node->lhs) == eval(node->rhs);
    if (node->kind == ND_NE)
      return eval(node->lhs) != eval(node->rhs);
    if (node->lhs->ty->is_unsigned)
      return node->kind == ND_LT ? (uint64_t)eval(node->lhs) < eval(node->rhs)
                                 : (uint64_t)eval(node->lhs) <= eval(node->rhs);
    return node->kind == ND_LT ? eval(node->lhs) < eval(node->rhs)
                               : eval(node->lhs) <= eval(node->rhs);
  case ND_COND:
    return eval_truth(node->cond) ? eval2(node->then, label) : eval2(node->els, label);
  case ND_COMMA:
    return eval2(node->rhs, label);
  case ND_NOT:
    return !eval_truth(node->lhs);
  case ND_BITNOT:
    return ~eval(node->lhs);
  case ND_LOGAND:
    return eval_truth(node->lhs) && eval_truth(node->rhs);
  case ND_LOGOR:
    return eval_truth(node->lhs) || eval_truth(node->rhs);
  case ND_CAST: {
    // A complex number as a real one is its real part. As bool, it's 1
    // if either part is nonzero.
    if (is_complex(node->lhs->ty)) {
      long double re, im;
      eval_complex(node->lhs, &re, &im);
      if (node->ty->kind == TY_BOOL)
        return re != 0 || im != 0;
      Node tmp = {.kind = ND_NUM, .tok = node->tok, .ty = complex_part(node->lhs->ty), .fval = &re};
      Node cast = {.kind = ND_CAST, .tok = node->tok, .ty = node->ty, .lhs = &tmp};
      return eval2(&cast, label);
    }

    // To bool, anything nonzero is 1: 2, 256, 0.5 and an address too.
    if (node->ty->kind == TY_BOOL) {
      if (is_flonum(node->lhs->ty))
        return eval_double(node->lhs) != 0;
      int64_t val = eval2(node->lhs, label);
      if (label && *label) {
        *label = NULL;
        return 1;
      }
      return val != 0;
    }

    int64_t val = eval2(node->lhs, label);
    // 1e19 is out of int64_t's range but not of unsigned long's.
    if (is_flonum(node->lhs->ty) && node->ty->is_unsigned)
      val = (uint64_t)eval_double(node->lhs);
    if (is_integer(node->ty))
      return wrap_int(node->ty, val);
    return val;
  }
  case ND_ADDR:
    return eval_rval(node->lhs, label);
  case ND_DEREF: {
    // An element that is itself an array, as `a[3]` of `int a[4][8]`, is
    // its address.
    if (node->ty->kind == TY_ARRAY)
      return eval2(node->lhs, label);

    // [GNU] An element of a string literal, as in `"xyz"[1]`
    int64_t off;
    Obj *str = string_at(node->lhs, &off);
    if (str && is_integer(node->ty) && off >= 0 && off + node->ty->size <= str->ty->size)
      return read_buf(str->init_data + off, node->ty->size);
    break;
  }
  case ND_LABEL_VAL:
    *label = &node->unique_label;
    return 0;
  case ND_MEMBER: {
    // An array member, as an address: of a global (a label is needed)
    // or at a constant address, as offsetof takes it.
    if (node->ty->kind != TY_ARRAY)
      error_tok(node->tok, "invalid initializer");
    char **l = NULL;
    int64_t val = eval_rval(node->lhs, &l) + node->member->offset;
    if (l && !label)
      error_tok(node->tok, "not a compile-time constant");
    if (l)
      *label = l;
    return val;
  }
  case ND_VAR:
    if (node->var->is_constexpr)
      return node->var->constexpr_val;
    if (!label || node->var->is_local)
      error_tok(node->tok, "not a compile-time constant");
    if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
      error_tok(node->tok, "invalid initializer");
    *label = &node->var->name;
    return 0;
  case ND_NUM:
    return node->val;
  }

  error_tok(node->tok, "not a compile-time constant");
}

static int64_t eval_rval(Node *node, char ***label) {
  switch (node->kind) {
  case ND_VAR:
    // An address is a constant only in an initializer (`label`), not in
    // an integer constant expression.
    if (!label || node->var->is_local)
      error_tok(node->tok, "not a compile-time constant");
    *label = &node->var->name;
    return 0;
  case ND_DEREF:
    return eval2(node->lhs, label);
  case ND_MEMBER:
    return eval_rval(node->lhs, label) + node->member->offset;
  }

  error_tok(node->tok, "invalid initializer");
}

// Is `node` an lvalue at a constant address: a member of, or element
// through, a constant pointer, as in offsetof's `&((T *)0)->a.b[2]`?
static bool is_const_lvalue(Node *node) {
  if (node->kind == ND_MEMBER)
    return !node->member->is_bitfield && is_const_lvalue(node->lhs);
  if (node->kind == ND_DEREF)
    return is_const_expr(node->lhs);
  return false;
}

// Is `node`, a complex number, a constant eval_complex() can compute?
static bool is_const_complex(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    return true;
  case ND_COMPLEX:
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
    return (is_complex(node->lhs->ty) ? is_const_complex(node->lhs) : is_const_expr(node->lhs)) &&
           (is_complex(node->rhs->ty) ? is_const_complex(node->rhs) : is_const_expr(node->rhs));
  case ND_CAST:
  case ND_NEG:
  case ND_BITNOT:
    return is_complex(node->lhs->ty) ? is_const_complex(node->lhs) : is_const_expr(node->lhs);
  }
  return false;
}

static bool is_const_expr(Node *node) {
  add_type(node);

  // (A complex constant is computed by eval_complex().)
  if (is_complex(node->ty))
    return false;

  // A complex constant as a real number
  if (node->kind == ND_CAST && is_complex(node->lhs->ty))
    return is_const_complex(node->lhs);

  // A 128-bit constant counts if its value fits in 64 bits, which eval()
  // returns exactly (see is_const_int128()).
  if (is_int128(node->ty)) {
    if (!is_const_int128(node))
      return false;
    __int128 v = eval128(node);
    return v == (int64_t)v;
  }

  switch (node->kind) {
  case ND_ADDR:
    return is_const_lvalue(node->lhs);
  case ND_MEMBER: // an array member, which is its address
    return node->ty->kind == TY_ARRAY && is_const_lvalue(node);
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR:
  case ND_SHL:
  case ND_SHR:
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
  case ND_LOGAND:
  case ND_LOGOR:
    return is_const_expr(node->lhs) && is_const_expr(node->rhs);
  case ND_COND:
    if (!is_const_expr(node->cond))
      return false;
    return is_const_expr(eval(node->cond) ? node->then : node->els);
  case ND_COMMA:
    // `(x = 0, 1)` is not a constant: folding it would drop `x = 0`.
    return is_const_expr(node->lhs) && is_const_expr(node->rhs);
  case ND_NEG:
  case ND_NOT:
  case ND_BITNOT:
  case ND_CAST:
    return is_const_expr(node->lhs);
  case ND_NUM:
    return true;
  case ND_VAR:
    return node->var->is_constexpr;
  }

  return false;
}

int64_t const_expr(Token **rest, Token *tok) {
  Node *node = conditional(rest, tok);
  return eval(node);
}

// `val` rounded to floating type `ty`.
static long double to_flonum(Type *ty, long double val) {
  if (ty->kind == TY_FLOAT)
    return (float)val;
  if (ty->kind == TY_DOUBLE)
    return (double)val;
  return val;
}

// `a op b` done in floating type `ty`, so it rounds as it would at run
// time. (Done in long double and then rounded, it could round twice.)
static long double fold_flonum(Type *ty, NodeKind op, long double a, long double b) {
  if (ty->kind == TY_FLOAT) {
    float x = a, y = b;
    return op == ND_ADD ? x + y : op == ND_SUB ? x - y : op == ND_MUL ? x * y : x / y;
  }
  if (ty->kind == TY_DOUBLE) {
    double x = a, y = b;
    return op == ND_ADD ? x + y : op == ND_SUB ? x - y : op == ND_MUL ? x * y : x / y;
  }
  return op == ND_ADD ? a + b : op == ND_SUB ? a - b : op == ND_MUL ? a * b : a / b;
}

// Evaluates a floating-point constant expression, in the precision of
// each part's type (float, double or long double).
static long double eval_double(Node *node) {
  add_type(node);

  if (is_integer(node->ty)) {
    if (node->ty->is_unsigned)
      return (unsigned long)eval(node);
    return eval(node);
  }

  switch (node->kind) {
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
    return fold_flonum(node->ty, node->kind, eval_double(node->lhs),
                       eval_double(node->rhs));
  case ND_NEG:
    return -eval_double(node->lhs);
  case ND_COND:
    return eval_double(node->cond) ? eval_double(node->then) : eval_double(node->els);
  case ND_COMMA:
    return eval_double(node->rhs);
  case ND_CAST:
    if (is_complex(node->lhs->ty)) {
      long double re, im;
      eval_complex(node->lhs, &re, &im);
      return to_flonum(node->ty, to_flonum(complex_part(node->lhs->ty), re));
    }
    return to_flonum(node->ty, eval_double(node->lhs));
  case ND_NUM:
    return to_flonum(node->ty, *node->fval);
  case ND_VAR:
    if (node->var->is_constexpr)
      return node->var->constexpr_fval;
    break;
  }

  error_tok(node->tok, "not a compile-time constant");
}

//---------- Assignment ------------------------------------------------------

// Convert op= operators to expressions containing an assignment.
//
// In general, `A op= C` is converted to ``tmp = &A, *tmp = *tmp op B`.
// However, if a given expression is of form `A.x op= C`, the input is
// converted to `tmp = &A, (*tmp).x = (*tmp).x op C` to handle assignments
// to bitfields.

// Does struct or union `ty` have a const member, at any depth?
static bool has_const_member(Type *ty) {
  for (Member *mem = ty->members; mem; mem = mem->next) {
    Type *t = mem->ty;
    while (t->kind == TY_ARRAY)
      t = t->base;
    if (t->is_const ||
        ((t->kind == TY_STRUCT || t->kind == TY_UNION) && has_const_member(t)))
      return true;
  }
  return false;
}

// Nothing const can change: a const variable, what a pointer to const
// points to, a member of a const struct (const too, see add_type), or a
// struct with a const member. Nor can a constexpr, whose value is also
// baked into constant expressions.
static void check_modifiable(Node *lhs) {
  add_type(lhs);
  if (lhs->kind == ND_VAR && lhs->var->is_constexpr)
    error_tok(lhs->tok, "cannot modify constexpr '%s'", lhs->var->name);

  Type *ty = lhs->ty;
  if (!ty->is_const &&
      !((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && has_const_member(ty)))
    return;
  if (lhs->kind == ND_VAR)
    error_tok(lhs->tok, "cannot modify read-only variable '%s'", lhs->var->name);
  error_tok(lhs->tok, "cannot modify a read-only location");
}

// `*P op= B` done atomically, returning the old value of *P if
// `return_old` and the new one otherwise:
//
// ({
//   T *addr = P; T2 val = (B); T old = *addr; T new;
//   do {
//    new = old op val;
//   } while (!atomic_compare_exchange_strong(addr, &old, new));
//   new; // or old
// })
static Node *atomic_rmw(Node *ptr, Node *rhs, NodeKind op, bool return_old,
                        Token *tok) {
  add_type(ptr);
  add_type(rhs);
  if (!ptr->ty->base)
    error_tok(tok, "pointer expected");

  // old and new are plain copies; only *addr is shared.
  Type *ty = ptr->ty->base;
  if (ty->is_atomic) {
    ty = copy_type(ty);
    ty->is_atomic = false;
  }

  Node head = {};
  Node *cur = &head;

  Obj *addr = new_lvar("", pointer_to(ptr->ty->base));
  Obj *val = new_lvar("", rhs->ty);
  Obj *old = new_lvar("", ty);
  Obj *new = new_lvar("", ty);

  cur = cur->next =
    new_unary(ND_EXPR_STMT,
              new_binary(ND_ASSIGN, new_var_node(addr, tok), ptr, tok), tok);

  cur = cur->next =
    new_unary(ND_EXPR_STMT,
              new_binary(ND_ASSIGN, new_var_node(val, tok), rhs, tok), tok);

  cur = cur->next =
    new_unary(ND_EXPR_STMT,
              new_binary(ND_ASSIGN, new_var_node(old, tok),
                         new_unary(ND_DEREF, new_var_node(addr, tok), tok), tok),
              tok);

  Node *loop = new_node(ND_DO, tok);
  loop->brk_label = new_unique_name();
  loop->cont_label = new_unique_name();

  Node *body = new_binary(ND_ASSIGN,
                          new_var_node(new, tok),
                          new_binary(op, new_var_node(old, tok),
                                     new_var_node(val, tok), tok),
                          tok);

  loop->then = new_node(ND_BLOCK, tok);
  loop->then->body = new_unary(ND_EXPR_STMT, body, tok);

  Node *cas = new_node(ND_CAS, tok);
  cas->cas_addr = new_var_node(addr, tok);
  cas->cas_old = new_unary(ND_ADDR, new_var_node(old, tok), tok);
  cas->cas_new = new_var_node(new, tok);
  loop->cond = new_unary(ND_NOT, cas, tok);

  cur = cur->next = loop;
  cur = cur->next = new_unary(ND_EXPR_STMT,
                              new_var_node(return_old ? old : new, tok), tok);

  Node *node = new_node(ND_STMT_EXPR, tok);
  node->body = head.next;
  return node;
}

static Node *to_assign(Node *binary) {
  add_type(binary->lhs);
  add_type(binary->rhs);
  check_modifiable(binary->lhs);
  Token *tok = binary->tok;

  // Convert `A.x op= C` to `tmp = &A, (*tmp).x = (*tmp).x op C`.
  if (binary->lhs->kind == ND_MEMBER) {
    Obj *var = new_lvar("", pointer_to(binary->lhs->lhs->ty));

    Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                             new_unary(ND_ADDR, binary->lhs->lhs, tok), tok);

    Node *expr2 = new_unary(ND_MEMBER,
                            new_unary(ND_DEREF, new_var_node(var, tok), tok),
                            tok);
    expr2->member = binary->lhs->member;

    Node *expr3 = new_unary(ND_MEMBER,
                            new_unary(ND_DEREF, new_var_node(var, tok), tok),
                            tok);
    expr3->member = binary->lhs->member;

    Node *expr4 = new_binary(ND_ASSIGN, expr2,
                             new_binary(binary->kind, expr3, binary->rhs, tok),
                             tok);

    return new_binary(ND_COMMA, expr1, expr4, tok);
  }

  // `A op= B` for an atomic A is a compare-and-swap loop.
  if (binary->lhs->ty->is_atomic)
    return atomic_rmw(new_unary(ND_ADDR, binary->lhs, tok), binary->rhs,
                      binary->kind, false, tok);

  // `x op= B` for a plain variable x is just `x = x op B`: reading x
  // has no side effects. (Taking &x, as below, would also keep x out
  // of a register; see "Register variables" in cgen.c.)
  if (binary->lhs->kind == ND_VAR) {
    Node *lhs = new_var_node(binary->lhs->var, tok);
    return new_binary(ND_ASSIGN, lhs, binary, tok);
  }

  // Convert `A op= B` to ``tmp = &A, *tmp = *tmp op B`.
  Obj *var = new_lvar("", pointer_to(binary->lhs->ty));

  Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                           new_unary(ND_ADDR, binary->lhs, tok), tok);

  Node *expr2 =
    new_binary(ND_ASSIGN,
               new_unary(ND_DEREF, new_var_node(var, tok), tok),
               new_binary(binary->kind,
                          new_unary(ND_DEREF, new_var_node(var, tok), tok),
                          binary->rhs,
                          tok),
               tok);

  return new_binary(ND_COMMA, expr1, expr2, tok);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//           | "<<=" | ">>="
static Node *assign(Token **rest, Token *tok) {
  Node *node = conditional(&tok, tok);

  if (equal(tok, "=")) {
    Node *rhs = assign(rest, tok->next);
    add_type(node);
    check_modifiable(node);
    check_assign(node->ty, rhs, "assignment");
    return new_binary(ND_ASSIGN, node, rhs, tok);
  }

  if (equal(tok, "+="))
    return to_assign(new_add(node, assign(rest, tok->next), tok));

  if (equal(tok, "-="))
    return to_assign(new_sub(node, assign(rest, tok->next), tok));

  if (equal(tok, "*="))
    return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

  if (equal(tok, "/="))
    return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));

  if (equal(tok, "%="))
    return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

  if (equal(tok, "&="))
    return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

  if (equal(tok, "|="))
    return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

  if (equal(tok, "^="))
    return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

  if (equal(tok, "<<="))
    return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));

  if (equal(tok, ">>="))
    return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

  *rest = tok;
  return node;
}

//---------- Binary operators, lowest precedence first -----------------------

// A null pointer constant: an integer constant expression of value 0, or
// one cast to void *.
static bool is_null_pointer(Node *node) {
  Type *ty = node->ty;
  if (node->kind == ND_CAST && ty->kind == TY_PTR && ty->base->kind == TY_VOID &&
      !ty->base->is_const && !ty->base->is_volatile)
    node = node->lhs;
  return is_integer(node->ty) && is_const_expr(node) && !eval(node);
}

// The type of `c ? a : b` if a or b is a pointer (C11 6.5.15p6), or NULL:
// a null pointer constant takes the other's type, void * wins over other
// pointers, and the pointed-to type has the qualifiers of both. musl's
// <tgmath.h> picks its result types this way.
static Type *cond_pointer_type(Node *node) {
  Node *then = node->then, *els = node->els;
  Type *t1 = then->ty, *t2 = els->ty;
  if (t1->kind == TY_FUNC)
    t1 = pointer_to(t1);
  if (t2->kind == TY_FUNC)
    t2 = pointer_to(t2);
  if ((!t1->base && !t2->base) || t1->kind == TY_VOID || t2->kind == TY_VOID)
    return NULL;
  if ((!t1->base && !is_integer(t1)) || (!t2->base && !is_integer(t2)))
    error_tok(node->tok, "type mismatch in conditional expression");
  if (t1->base)
    t1 = pointer_to(t1->base);
  if (t2->base)
    t2 = pointer_to(t2->base);

  if (!t1->base || is_null_pointer(then))
    return t2->base ? t2 : t1;
  if (!t2->base || is_null_pointer(els))
    return t1;

  Type *b1 = t1->base, *b2 = t2->base;
  Type *base = b2->kind == TY_VOID ? b2 : b1;
  return pointer_to(qualified(base, b1->is_const || b2->is_const,
                              b1->is_volatile || b2->is_volatile));
}

// add_type() for `c ? a : b`, which would give pointer arms a's type.
static void add_cond_type(Node *node) {
  add_type(node->then);
  add_type(node->els);
  Type *ptr = cond_pointer_type(node);
  if (ptr) {
    check_scalar(node->cond);
    node->then = new_cast(node->then, ptr);
    node->els = new_cast(node->els, ptr);
    node->ty = ptr;
  }
  add_type(node);
}

// conditional = logor ("?" expr? ":" conditional)?
static Node *conditional(Token **rest, Token *tok) {
  Node *cond = logor(&tok, tok);

  if (!equal(tok, "?")) {
    *rest = tok;
    return cond;
  }
  warn_address_condition(cond);

  if (equal(tok->next, ":")) {
    // [GNU] Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
    add_type(cond);
    Obj *var = new_lvar("", cond->ty);
    Node *lhs = new_binary(ND_ASSIGN, new_var_node(var, tok), cond, tok);
    Node *rhs = new_node(ND_COND, tok);
    rhs->cond = new_var_node(var, tok);
    rhs->then = new_var_node(var, tok);
    rhs->els = conditional(rest, tok->next->next);
    add_cond_type(rhs);
    return new_binary(ND_COMMA, lhs, rhs, tok);
  }

  Node *node = new_node(ND_COND, tok);
  node->cond = cond;
  node->then = expr(&tok, tok->next);
  tok = skip(tok, ":");
  node->els = conditional(rest, tok);

  // A constant condition picks the arm at compile time, as gcc does; the
  // other may name functions that don't exist (see the `if` in stmt()).
  Node *then = node->then, *els = node->els; // before add_type converts them
  add_cond_type(node);
  if (is_integer(cond->ty) && is_const_expr(cond)) {
    Node *arm = eval(cond) ? then : els;
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
      return arm;
    return new_cast(arm, node->ty);
  }
  return node;
}

// Is `node` an integer constant equal to `val` (0, or 1 for any nonzero)?
static bool is_const_truth(Node *node, int val) {
  add_type(node);
  return is_integer(node->ty) && is_const_expr(node) && !eval(node) == !val;
}

// logor = logand ("||" logand)*
//
// `1 || x` is 1 without x, as `0 && x` is 0: x isn't compiled, as with
// gcc (see the `if` in stmt()).
static Node *logor(Token **rest, Token *tok) {
  Node *node = logand(&tok, tok);
  while (equal(tok, "||")) {
    Token *start = tok;
    Node *rhs = logand(&tok, tok->next);
    node = is_const_truth(node, 1) ? new_num(1, start)
                                   : new_binary(ND_LOGOR, node, rhs, start);
  }
  *rest = tok;
  return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(Token **rest, Token *tok) {
  Node *node = bitor(&tok, tok);
  while (equal(tok, "&&")) {
    Token *start = tok;
    Node *rhs = bitor(&tok, tok->next);
    node = is_const_truth(node, 0) ? new_num(0, start)
                                   : new_binary(ND_LOGAND, node, rhs, start);
  }
  *rest = tok;
  return node;
}

// bitor = bitxor ("|" bitxor)*
static Node *bitor(Token **rest, Token *tok) {
  Node *node = bitxor(&tok, tok);
  while (equal(tok, "|")) {
    Token *start = tok;
    node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// bitxor = bitand ("^" bitand)*
static Node *bitxor(Token **rest, Token *tok) {
  Node *node = bitand(&tok, tok);
  while (equal(tok, "^")) {
    Token *start = tok;
    node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// bitand = equality ("&" equality)*
static Node *bitand(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);
  while (equal(tok, "&")) {
    Token *start = tok;
    node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **rest, Token *tok) {
  Node *node = relational(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "==") || equal(tok, "!=")) {
      Node *rhs = relational(&tok, tok->next);
      warn_string_compare(node, rhs, start);
      node = new_binary(equal(start, "==") ? ND_EQ : ND_NE, node, rhs, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **rest, Token *tok) {
  Node *node = shift(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "<")) {
      node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "<=")) {
      node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, ">")) {
      node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
      continue;
    }

    if (equal(tok, ">=")) {
      node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "<<") || equal(tok, ">>")) {
      Node *rhs = add(&tok, tok->next);
      warn_shift_count(node, rhs, start);
      node = new_binary(equal(start, "<<") ? ND_SHL : ND_SHR, node, rhs, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

//---------- + and - (with pointer math), *, casts and unary -----------------

// In C, `+` operator is overloaded to perform the pointer arithmetic.
// If p is a pointer, p+n adds not n but sizeof(*p)*n to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
// Integer `n` times `size`, as a long: how many bytes `ptr + n` moves.
// For 1-byte elements that's just n converted to long.
static Node *scale(Node *n, int64_t size, Token *tok) {
  if (size == 1)
    return new_cast(n, ty_long);
  return new_binary(ND_MUL, n, new_long(size, tok), tok);
}

// A real or complex number
static bool is_number(Type *ty) {
  return is_numeric(ty) || is_complex(ty);
}

// A number or a vector, which `+` and `-` take as they are (see
// vector_binary() in type.c)
static bool is_arith(Type *ty) {
  return is_number(ty) || is_vector(ty);
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_arith(lhs->ty) && is_arith(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);

  if (lhs->ty->base && rhs->ty->base)
    error_tok(tok, "invalid operands");

  // Canonicalize `num + ptr` to `ptr + num`.
  if (!lhs->ty->base && rhs->ty->base) {
    Node *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
  }

  // Neither is a pointer (a struct, say, as in `s[0]` on a struct s), or
  // the number isn't an integer.
  if (!lhs->ty->base || !is_integer(rhs->ty))
    error_tok(tok, "invalid operands");

  // VLA + num
  if (lhs->ty->base->kind == TY_VLA) {
    rhs = new_binary(ND_MUL, rhs, new_var_node(lhs->ty->base->vla_size, tok), tok);
    return new_binary(ND_ADD, lhs, rhs, tok);
  }

  // ptr + num
  rhs = scale(rhs, lhs->ty->base->size, tok);
  return new_binary(ND_ADD, lhs, rhs, tok);
}

// Like `+`, `-` is overloaded for the pointer type.
static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num - num
  if (is_arith(lhs->ty) && is_arith(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);

  if (!lhs->ty->base)
    error_tok(tok, "invalid operands");

  // VLA + num
  if (lhs->ty->base->kind == TY_VLA) {
    rhs = new_binary(ND_MUL, rhs, new_var_node(lhs->ty->base->vla_size, tok), tok);
    add_type(rhs);
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    node->ty = lhs->ty;
    return node;
  }

  // ptr - num
  if (lhs->ty->base && is_integer(rhs->ty)) {
    rhs = scale(rhs, lhs->ty->base->size, tok);
    add_type(rhs);
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    node->ty = lhs->ty;
    return node;
  }

  // ptr - ptr, which returns how many elements are between the two.
  if (lhs->ty->base && rhs->ty->base) {
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    node->ty = ty_long;
    return new_binary(ND_DIV, node, new_long(lhs->ty->base->size, tok), tok);
  }

  error_tok(tok, "invalid operands");
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "+")) {
      node = new_add(node, mul(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "-")) {
      node = new_sub(node, mul(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **rest, Token *tok) {
  Node *node = cast(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "*")) {
      node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "/") || equal(tok, "%")) {
      Node *rhs = cast(&tok, tok->next);
      warn_div_by_zero(node, rhs, start);
      node = new_binary(equal(start, "/") ? ND_DIV : ND_MOD, node, rhs, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// [GNU] A cast to or from a vector keeps the bits, so the other type
// must be a vector or an integer of the same size.
static void check_vector_cast(Node *node) {
  Type *from = node->lhs->ty, *to = node->ty;
  if ((!is_vector(from) && !is_vector(to)) || to->kind == TY_VOID)
    return;
  Type *other = is_vector(from) ? to : from;
  if ((!is_vector(other) && !is_integer(other)) || from->size != to->size)
    error_tok(node->tok, "cannot convert '%s' to '%s': vector casts keep the bits, "
              "so the sizes must match", type_name(from), type_name(to));
}

// cast = "(" type-name ")" cast | unary
static Node *cast(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");

    // compound literal
    if (equal(tok, "{"))
      return unary(rest, start);

    // type cast, whose result has no qualifiers
    Node *node = new_cast(cast(rest, tok), unqual(ty));
    node->tok = start;
    check_vector_cast(node);
    return node;
  }

  return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | "&&" ident
//       | postfix
static Node *unary(Token **rest, Token *tok) {
  // +x is a value, promoted as for -x: sizeof(+c) is sizeof(int).
  if (equal(tok, "+")) {
    Node *node = cast(rest, tok->next);
    add_type(node);
    if (!is_arith(node->ty))
      error_tok(tok, "invalid argument type to unary '+'");
    if (is_integer(node->ty) && node->ty->size < 4)
      return new_cast(node, ty_int);
    return new_cast(node, node->ty);
  }

  if (equal(tok, "-"))
    return new_unary(ND_NEG, cast(rest, tok->next), tok);

  if (equal(tok, "&")) {
    Node *lhs = cast(rest, tok->next);
    add_type(lhs);
    if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
      error_tok(tok, "cannot take address of bitfield");
    if (lhs->kind == ND_VAR && lhs->var->is_register)
      error_tok(tok, "address of register variable '%s' requested", lhs->var->name);
    return new_unary(ND_ADDR, lhs, tok);
  }

  if (equal(tok, "*")) {
    // [https://www.sigbus.info/n1570#6.5.3.2p4] This is an oddity
    // in the C spec, but dereferencing a function shouldn't do
    // anything. If foo is a function, `*foo`, `**foo` or `*****foo`
    // are all equivalent to just `foo`.
    Node *node = cast(rest, tok->next);
    add_type(node);
    if (node->ty->kind == TY_FUNC)
      return node;
    return new_unary(ND_DEREF, node, tok);
  }

  if (equal(tok, "!"))
    return new_unary(ND_NOT, cast(rest, tok->next), tok);

  // [GNU] __real__ z and __imag__ z: a part of a complex number, an
  // lvalue if z is. Of a real number, the number itself and 0.
  if (equal(tok, "__real__") || equal(tok, "__real") ||
      equal(tok, "__imag__") || equal(tok, "__imag")) {
    bool is_imag = tok->loc[2] == 'i';
    Node *node = cast(rest, tok->next);
    add_type(node);
    if (!is_complex(node->ty)) {
      if (!is_numeric(node->ty))
        error_tok(tok, "'%s' is not a number", type_name(node->ty));
      if (!is_imag)
        return node;
      return new_binary(ND_COMMA, new_cast(node, ty_void),
                        new_cast(new_num(0, tok), node->ty), tok);
    }
    Node *part = new_unary(ND_MEMBER, node, tok);
    part->member = is_imag ? node->ty->members->next : node->ty->members;
    return part;
  }

  if (equal(tok, "~"))
    return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

  // Read ++i as i+=1
  if (equal(tok, "++"))
    return to_assign(new_add(unary(rest, tok->next), new_num(1, tok), tok));

  // Read --i as i-=1
  if (equal(tok, "--"))
    return to_assign(new_sub(unary(rest, tok->next), new_num(1, tok), tok));

  // [GNU] labels-as-values
  if (equal(tok, "&&")) {
    Node *node = new_node(ND_LABEL_VAL, tok);
    node->label = get_ident(tok->next);
    node->goto_next = gotos;
    gotos = node;
    *rest = tok->next->next;
    return node;
  }

  return postfix(rest, tok);
}

//---------- Structs and unions ----------------------------------------------

// A member's alignment: its type's, or _Alignas's. `packed` lowers it to 1
// and aligned(N) raises it, as with gcc. attr_align keeps an explicit
// alignment, which is the only one that counts in a packed struct.
static void set_member_align(Member *mem, VarAttr *attr, Attrs *gnu) {
  no_symbol_attrs(gnu, "a struct member");
  if (gnu->is_packed && mem->is_bitfield)
    error_tok(gnu->layout_tok, "attribute 'packed' on a bit-field is not supported");

  mem->align = attr->align ? attr->align : mem->ty->align;
  if (gnu->is_packed)
    mem->align = 1;
  mem->align = MAX(mem->align, gnu->align);
  mem->attr_align = MAX(attr->align, gnu->align);
}

// Member names are unique, an anonymous member's members' too.
static void check_member_names(HashMap *seen, Member *mem) {
  for (; mem; mem = mem->next) {
    if (!mem->name) {
      if (!mem->is_bitfield && (mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION))
        check_member_names(seen, mem->ty->members);
      continue;
    }
    if (hashmap_get2(seen, mem->name->loc, mem->name->len))
      error_tok(mem->name, "duplicate member '%s'", get_ident(mem->name));
    hashmap_put2(seen, mem->name->loc, mem->name->len, mem);
  }
}

// struct-members = (declspec declarator attributes (","  declarator)* ";")*
static void struct_members(Token **rest, Token *tok, Type *ty) {
  Member head = {};
  Member *cur = &head;
  int idx = 0;

  while (!equal(tok, "}")) {
    if (equal(tok, "_Static_assert")) {
      tok = static_assertion(tok);
      continue;
    }

    // An array of unknown length is only the last member.
    if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0)
      error_tok(cur->name ? cur->name : tok, "flexible array member not at end of struct");

    VarAttr attr = {};
    Type *basety = declspec(&tok, tok, &attr);
    bool first = true;

    // Anonymous struct member
    if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
        consume(&tok, tok, ";")) {
      Member *mem = arena_alloc(sizeof(Member));
      mem->ty = basety;
      mem->idx = idx++;
      set_member_align(mem, &attr, &attr.gnu);
      cur = cur->next = mem;
      continue;
    }

    // Regular struct members
    while (!consume(&tok, tok, ";")) {
      if (!first)
        tok = skip_decl_comma(tok);
      if (!first && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0)
        error_tok(cur->name, "flexible array member not at end of struct");
      first = false;

      Member *mem = arena_alloc(sizeof(Member));
      Attrs all = attr.gnu;
      mem->ty = declarator(&tok, tok, basety, &all);
      if (mem->ty->kind == TY_VLA)
        error_tok(mem->ty->name ? mem->ty->name : tok,
                  "a variable length array in a struct is not supported");
      mem->name = mem->ty->name;
      mem->idx = idx++;

      if (consume(&tok, tok, ":")) {
        if (is_complex(mem->ty))
          error_tok(tok, "a bit-field can't be complex");
        mem->is_bitfield = true;

        // Its width: an integer constant from 0 to its type's width, and
        // 0 only for an unnamed one, as C requires
        char *name = mem->name ? get_ident(mem->name) : "(anonymous)";
        Token *width_tok = tok;
        Node *width = conditional(&tok, tok);
        add_type(width);
        if (!is_integer(width->ty) || !is_const_expr(width))
          error_tok(width_tok, "bit-field '%s' width not an integer constant", name);
        mem->bit_width = eval(width);
        if (mem->bit_width < 0)
          error_tok(width_tok, "negative width in bit-field '%s'", name);
        if (mem->bit_width > (mem->ty->kind == TY_BOOL ? 1 : mem->ty->size * 8))
          error_tok(width_tok, "width of '%s' exceeds its type", name);
        if (mem->bit_width == 0 && mem->name)
          error_tok(width_tok, "zero width for bit-field '%s'", name);
        tok = attributes(tok, &all, true);
      }

      // Only a bit-field (`int : 3`) or an anonymous struct or union may
      // have no name; member lookup looks inside the nameless ones.
      if (!mem->name && !mem->is_bitfield && mem->ty->kind != TY_STRUCT &&
          mem->ty->kind != TY_UNION)
        error_tok(mem->ty->name_pos ? mem->ty->name_pos : tok, "member name omitted");

      set_member_align(mem, &attr, &all);
      cur = cur->next = mem;
    }
  }

  // If the last element is an array of incomplete type, it's
  // called a "flexible array member". It should behave as if
  // if were a zero-sized array.
  if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
    cur->ty = array_of(cur->ty->base, 0);
    ty->is_flexible = true;
  }

  *rest = tok->next;
  ty->members = head.next;

  HashMap seen = {};
  check_member_names(&seen, ty->members);
  free(seen.buckets);
}

// Attributes on a struct or union type: `packed` and `aligned(N)`.
static Token *attribute_list(Token *tok, Type *ty) {
  Attrs a = {};
  tok = attributes(tok, &a, true);
  no_symbol_attrs(&a, "a type");
  if (a.is_packed)
    ty->is_packed = true;
  if (a.align)
    ty->align = a.align;
  return tok;
}

// struct-union-decl = attributes ident? ("{" struct-members "}" attributes)?
static Type *struct_union_decl(Token **rest, Token *tok) {
  Type *ty = struct_type();
  tok = attribute_list(tok, ty);

  // Read a tag.
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    ty->tag = tag;
    tok = tok->next;
  }

  if (tag && !equal(tok, "{")) {
    *rest = tok;

    Type *ty2 = find_tag(tag);
    if (ty2)
      return ty2;

    ty->size = -1;
    push_tag_scope(tag, ty);
    return ty;
  }

  ty->pack = tok->pack;
  tok = skip(tok, "{");

  // The tag is in scope from the `{` on, so a member can refer to its own
  // struct, even in a function pointer's parameters, as in
  // `struct S { int (*f)(struct S *); };`. It is incomplete until the `}`.
  // A struct already declared in this scope (`struct S;`) is the same type,
  // completed here. Before C23, which allows the same members again,
  // defining it twice is an error.
  Type *prev = NULL;
  if (tag) {
    prev = hashmap_get2(&scope->tags, tag->loc, tag->len);
    if (prev && opt_std < 2023 && (prev->size >= 0 || prev->kind == TY_ENUM))
      error_tok(tag, "redefinition of '%.*s'", tag->len, tag->loc);
    if (!prev)
      push_tag_scope(tag, ty);
  }

  // Construct a struct object.
  ty->size = -1;
  struct_members(&tok, tok, ty);
  ty->size = 0;
  *rest = attribute_list(tok, ty);

  // Its qualified copies are completed once it's laid out (see
  // struct_decl).
  if (prev) {
    Type *variants = prev->variants;
    *prev = *ty;
    prev->variants = variants;
    return prev;
  }
  return ty;
}

// In a packed struct, only an explicit aligned(N) or _Alignas on a member
// counts; every other member is 1-byte aligned. #pragma pack caps any
// member's alignment, an explicit one too, and under it a bit-field's
// type counts even in a packed struct, as with gcc.
static int member_align(Type *ty, Member *mem) {
  bool packed = ty->is_packed && !(ty->pack && mem->is_bitfield);
  int align = packed ? MAX(1, mem->attr_align) : mem->align;
  return ty->pack ? MIN(align, ty->pack) : align;
}

// In a packed struct or union, or under #pragma pack, a bit-field may
// straddle units of its type, as with gcc.
static bool is_loose(Type *ty) {
  return ty->is_packed || ty->pack;
}

// Then each named bit-field's unit is the smallest one (1, 2, 4 or 8
// bytes, or 16 for __int128, unaligned if need be) that holds all of it
// and lies inside the struct, or else the odd number of bytes it covers,
// which cgen.c loads and stores a byte at a time.
static void place_loose_bitfields(Type *ty) {
  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (!mem->is_bitfield || !mem->name)
      continue;

    int64_t start = mem->offset * 8 + mem->bit_offset;
    mem->offset = start / 8;
    mem->bit_offset = start % 8;
    mem->unit = (mem->bit_offset + mem->bit_width + 7) / 8;
    if (mem->unit > 16)
      error_tok(mem->name, "a packed bit-field spanning more than 16 bytes"
                " is not supported");

    int max = is_int128(mem->ty) ? 16 : 8;
    for (int sz = 1; sz <= max; sz *= 2) {
      int64_t off = MIN(start / 8, ty->size - sz);
      if (off >= 0 && start + mem->bit_width <= (off + sz) * 8) {
        mem->offset = off;
        mem->bit_offset = start - off * 8;
        mem->unit = sz;
        break;
      }
    }
  }
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_STRUCT;

  if (ty->size < 0)
    return ty;

  // Assign offsets within the struct to members.
  int64_t bits = 0;

  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (mem->is_bitfield && mem->bit_width == 0) {
      // A zero-width bit-field starts the next member at a unit of its
      // type, even under #pragma pack. It leaves the struct's alignment
      // alone, as with gcc.
      bits = align_to(bits, mem->ty->size * 8);
      continue;
    }

    if (mem->is_bitfield) {
      // One that would straddle a unit of its type starts at the next
      // unit, unless is_loose().
      int sz = mem->ty->size;
      if (!is_loose(ty) && bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8))
        bits = align_to(bits, sz * 8);

      mem->offset = align_down(bits / 8, sz);
      mem->bit_offset = bits % (sz * 8);
      mem->unit = sz;
      bits += mem->bit_width;
    } else {
      bits = align_to(bits, member_align(ty, mem) * 8);
      mem->offset = bits / 8;
      bits += mem->ty->size * 8;
    }

    if (ty->align < member_align(ty, mem))
      ty->align = member_align(ty, mem);
  }

  ty->size = align_to(bits, ty->align * 8) / 8;
  if (is_loose(ty))
    place_loose_bitfields(ty);
  complete_variants(ty);
  return ty;
}

// union-decl = struct-union-decl
static Type *union_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_UNION;

  if (ty->size < 0)
    return ty;

  // If union, we don't have to assign offsets because they
  // are already initialized to zero. We need to compute the
  // alignment and the size though. A bit-field takes the bytes its
  // width needs, and a zero-width one nothing.
  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (mem->is_bitfield && mem->bit_width == 0)
      continue;
    mem->unit = mem->ty->size;
    ty->align = MAX(ty->align, member_align(ty, mem));
    int64_t size = mem->is_bitfield ? (mem->bit_width + 7) / 8 : mem->ty->size;
    ty->size = MAX(ty->size, size);
  }
  ty->size = align_to(ty->size, ty->align);
  if (is_loose(ty))
    place_loose_bitfields(ty);
  complete_variants(ty);
  return ty;
}

// Find a struct member by name.
static Member *get_struct_member(Type *ty, Token *tok) {
  for (Member *mem = ty->members; mem; mem = mem->next) {
    // Anonymous struct or union member, or an unnamed bit-field
    if (!mem->name) {
      if (!mem->is_bitfield && get_struct_member(mem->ty, tok))
        return mem;
      continue;
    }

    // Regular struct member
    if (mem->name->len == tok->len &&
        !strncmp(mem->name->loc, tok->loc, tok->len))
      return mem;
  }
  return NULL;
}

// Create a node representing a struct member access, such as foo.bar
// where foo is a struct and bar is a member name.
//
// C has a feature called "anonymous struct" which allows a struct to
// have another unnamed struct as a member like this:
//
//   struct { struct { int a; }; int b; } x;
//
// The members of an anonymous struct belong to the outer struct's
// member namespace. Therefore, in the above example, you can access
// member "a" of the anonymous struct as "x.a".
//
// This function takes care of anonymous structs.
static Node *struct_ref(Node *node, Token *tok) {
  add_type(node);
  if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION)
    error_tok(node->tok, "not a struct nor a union");

  Type *ty = node->ty;

  for (;;) {
    Member *mem = get_struct_member(ty, tok);
    if (!mem)
      error_tok(tok, "no such member");
    node = new_unary(ND_MEMBER, node, tok);
    node->member = mem;
    if (mem->name)
      break;
    ty = mem->ty;
  }
  return node;
}

//---------- Postfix expressions, calls and _Generic -------------------------

// Convert A++ to `(typeof A)((A += 1) - 1)`
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
  add_type(node);
  bool is_bitfield = node->kind == ND_MEMBER && node->member->is_bitfield;
  if (!is_bitfield && (node->ty->kind != TY_BOOL || node->ty->is_atomic))
    return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
                            new_num(-addend, tok), tok),
                    node->ty);

  // What a bool or a bit-field holds after `+ 1` may not be old + 1 (it's
  // cut to 0 or 1, or wraps), so keep the old value instead:
  // `(p = &A, old = *p, *p = old + 1, old)`, with (*p).x for a bit-field A.x.
  check_modifiable(node);
  Node *base = is_bitfield ? node->lhs : node;
  Obj *p = new_lvar("", pointer_to(base->ty));
  Obj *old = new_lvar("", node->ty);
  Node *place[2];
  for (int i = 0; i < 2; i++) {
    place[i] = new_unary(ND_DEREF, new_var_node(p, tok), tok);
    if (is_bitfield) {
      place[i] = new_unary(ND_MEMBER, place[i], tok);
      place[i]->member = node->member;
    }
  }
  Node *expr = new_binary(ND_ASSIGN, new_var_node(p, tok),
                          new_unary(ND_ADDR, base, tok), tok);
  Node *get = new_binary(ND_ASSIGN, new_var_node(old, tok), place[0], tok);
  Node *set = new_binary(ND_ASSIGN, place[1],
                         new_add(new_var_node(old, tok), new_num(addend, tok), tok),
                         tok);
  expr = new_binary(ND_COMMA, expr, get, tok);
  expr = new_binary(ND_COMMA, expr, set, tok);
  return new_binary(ND_COMMA, expr, new_var_node(old, tok), tok);
}

// postfix = "(" type-name ")" "{" initializer-list "}" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static Node *postfix(Token **rest, Token *tok) {
  Node *node;

  if (equal(tok, "(") && is_typename(tok->next)) {
    // Compound literal
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");

    if (scope->next == NULL) {
      Obj *var = new_anon_gvar(ty);
      gvar_initializer(&tok, tok, var);
      node = new_var_node(var, start);
    } else {
      Obj *var = new_lvar("", ty);
      Node *lhs = lvar_initializer(&tok, tok, var);
      node = new_binary(ND_COMMA, lhs, new_var_node(var, tok), start);
      // It's an lvalue of its own type, `const` included, which a comma's
      // value otherwise wouldn't keep: &(const int){0} is const int *.
      add_type(node);
      node->ty = ty;
    }
  } else {
    node = primary(&tok, tok);
  }

  for (;;) {
    if (equal(tok, "(")) {
      node = funcall(&tok, tok->next, node);
      continue;
    }

    if (equal(tok, "[")) {
      // x[y] is short for *(x+y)
      Token *start = tok;
      Node *idx = expr(&tok, tok->next);
      tok = skip(tok, "]");
      add_type(node);
      if (is_vector(node->ty))
        node = vector_elem(node, idx, start);
      else
        node = new_unary(ND_DEREF, new_add(node, idx, start), start);
      continue;
    }

    if (equal(tok, ".")) {
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }

    if (equal(tok, "->")) {
      // x->y is short for (*x).y
      node = new_unary(ND_DEREF, node, tok);
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }

    if (equal(tok, "++")) {
      node = new_inc_dec(node, tok, 1);
      tok = tok->next;
      continue;
    }

    if (equal(tok, "--")) {
      node = new_inc_dec(node, tok, -1);
      tok = tok->next;
      continue;
    }

    *rest = tok;
    return node;
  }
}

// [GNU] v[i] on a vector v: its element i, through a pointer to the
// first. One that isn't in memory, like a call's value, is copied to a
// temporary first.
static Node *vector_elem(Node *vec, Node *idx, Token *tok) {
  Type *ty = vec->ty;
  Node *first = NULL;
  if (vec->kind != ND_VAR && vec->kind != ND_DEREF && vec->kind != ND_MEMBER) {
    Obj *tmp = new_lvar("", ty);
    first = new_binary(ND_ASSIGN, new_var_node(tmp, tok), vec, tok);
    vec = new_var_node(tmp, tok);
  } else if (vec->kind == ND_VAR && vec->var->is_register) {
    error_tok(tok, "subscripting register vector '%s'", vec->var->name);
  }

  Type *elem = qualified(ty->elem, ty->is_const, ty->is_volatile);
  Node *addr = new_cast(new_unary(ND_ADDR, vec, tok), pointer_to(elem));
  Node *node = new_unary(ND_DEREF, new_add(addr, idx, tok), tok);
  if (first)
    return new_binary(ND_COMMA, first, node, tok);
  return node;
}

// funcall = (assign ("," assign)*)? ")"
static Node *funcall(Token **rest, Token *tok, Node *fn) {
  add_type(fn);

  if (fn->ty->kind != TY_FUNC &&
      (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
    error_tok(fn->tok, "not a function");

  Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
  Type *param_ty = ty->params;

  // For error messages: the function's name and parameter count.
  char *name = (fn->kind == ND_VAR) ? fn->var->name : "function";
  int nparams = 0;
  for (Type *t = ty->params; t; t = t->next)
    nparams++;

  Node head = {};
  Node *cur = &head;
  int nargs = 0;

  while (!equal(tok, ")")) {
    if (cur != &head) {
      if (!equal(tok, ","))
        error_expected(tok, "',' or ')'");
      tok = tok->next;
    }

    Node *arg = assign(&tok, tok);
    add_type(arg);
    nargs++;

    if (!param_ty && !ty->is_variadic)
      error_tok(arg->tok, "too many arguments to '%s' (expected %d)",
                name, nparams);

    // A transparent union's argument is any member's, as its first member
    if (param_ty && param_ty->is_transparent && arg->ty->kind != TY_UNION) {
      Member *mem = param_ty->members;
      while (mem && !is_assignable(mem->ty, arg))
        mem = mem->next;
      if (!mem)
        error_tok(arg->tok, "argument %d of '%s' fits no member of '%s'", nargs, name,
                  type_name(param_ty));
      cur = cur->next = new_cast(arg, param_ty->members->ty);
      param_ty = param_ty->next;
      continue;
    }

    if (param_ty) {
      check_assign(param_ty, arg, format("argument %d of '%s'", nargs, name));
      if ((param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION) ||
          is_complex(param_ty))
        arg = new_cast(arg, param_ty);
      param_ty = param_ty->next;
    } else if (arg->ty->kind == TY_FLOAT) {
      // If parameter type is omitted (e.g. in "..."), float
      // arguments are promoted to double.
      arg = new_cast(arg, ty_double);
    }

    cur = cur->next = arg;
  }

  if (param_ty)
    error_tok(tok, "too few arguments to '%s' (expected %d, got %d)",
              name, nparams, nargs);

  *rest = skip(tok, ")");

  if (fn->kind == ND_VAR && fn->var->is_function)
    warn_format(fn->var, head.next);

  // The C library's creal, cimag and conj (and their f and l forms) are
  // builtins, as with gcc, so they need no -lm: a part of the argument,
  // kept in a variable, or the argument with its imaginary part negated.
  static char *part_fns[] = {"creal", "crealf", "creall", "cimag", "cimagf",
                             "cimagl", "conj", "conjf", "conjl"};
  for (int i = 0; i < 9 && fn->kind == ND_VAR && fn->var->is_function &&
                  !fn->var->is_definition && nargs == 1 && is_complex(head.next->ty);
       i++) {
    if (strcmp(name, part_fns[i]))
      continue;
    Token *t = fn->tok;
    Obj *z = new_lvar("", head.next->ty);
    Node *set = new_binary(ND_ASSIGN, new_var_node(z, t), head.next, t);
    Node *part = new_unary(ND_MEMBER, new_var_node(z, t), t);
    part->member = i < 3 ? z->ty->members : z->ty->members->next;
    if (i < 6)
      return new_cast(new_binary(ND_COMMA, set, part, t), ty->return_ty);
    Node *part2 = new_unary(ND_MEMBER, new_var_node(z, t), t);
    part2->member = part->member;
    Node *neg = new_binary(ND_ASSIGN, part, new_unary(ND_NEG, part2, t), t);
    Node *val = new_binary(ND_COMMA, set, neg, t);
    return new_cast(new_binary(ND_COMMA, val, new_var_node(z, t), t), ty->return_ty);
  }

  // Errors about the call's value point at the function name.
  Node *node = new_unary(ND_FUNCALL, fn, fn->tok);
  node->func_ty = ty;
  node->ty = ty->return_ty;
  node->args = head.next;

  // If a function returns a struct, it is caller's responsibility
  // to allocate a space for the return value.
  if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)
    node->ret_buffer = new_lvar("", node->ty);
  return node;
}

// generic-selection = "(" assign "," generic-assoc ("," generic-assoc)* ")"
//
// generic-assoc = type-name ":" assign
//               | "default" ":" assign
static Node *generic_selection(Token **rest, Token *tok) {
  Token *start = tok;
  tok = skip(tok, "(");

  Node *ctrl = assign(&tok, tok);
  add_type(ctrl);

  // The controlling expression's value: an array or a function is a
  // pointer, and its own qualifiers are dropped.
  Type *t1 = unqual(ctrl->ty);
  if (t1->kind == TY_FUNC)
    t1 = pointer_to(t1);
  else if (t1->kind == TY_ARRAY)
    t1 = pointer_to(t1->base);

  Node *ret = NULL;

  while (!consume(rest, tok, ")")) {
    tok = skip(tok, ",");

    if (equal(tok, "default")) {
      tok = skip(tok->next, ":");
      Node *node = assign(&tok, tok);
      if (!ret)
        ret = node;
      continue;
    }

    Type *t2 = typename(&tok, tok);
    tok = skip(tok, ":");
    Node *node = assign(&tok, tok);
    if (is_compatible(t1, t2))
      ret = node;
  }

  if (!ret)
    error_tok(start, "controlling expression type not compatible with"
              " any generic association type");
  return ret;
}

// The builtins primary() handles.
static char *builtin_names[] = {
  "__builtin_types_compatible_p", "__builtin_unreachable",
  "__builtin_compare_and_swap", "__builtin_atomic_exchange",
  "__builtin_va_start", "__builtin_va_end", "__builtin_va_copy",
  "__builtin_va_arg", "__builtin_offsetof", "__builtin_complex",
  "__builtin_shufflevector", "__builtin_convertvector",
};

// A vector of `elem` with `n` elements, if mucc has one that size.
static Type *vector_n(Type *elem, int64_t n, Token *tok) {
  int64_t size = n * elem->size;
  if (size != 4 && size != 8 && size != 16)
    error_tok(tok, "vectors of %ld bytes are not supported (only 4, 8 or 16)", size);
  return vector_of(elem, size);
}

// [GNU] __builtin_shufflevector(a, b, i, ...), from clang, in gcc 12 too:
// a vector of elements of a and b, numbered across both, and -1 for one
// that may be anything. __builtin_convertvector(v, T): each element of v
// converted to T's. Both are an element at a time, through temporaries:
// `(a2 = a, b2 = b, r[0] = a2[i], ..., r)`.
// Element `i` of vector variable `var`
static Node *var_elem(Obj *var, int64_t i, Token *tok) {
  Node *node = new_var_node(var, tok);
  add_type(node);
  return vector_elem(node, new_num(i, tok), tok);
}

static Node *vector_builtin(Token **rest, Token *tok) {
  Token *start = tok;
  bool shuffle = equal(tok, "__builtin_shufflevector");
  tok = skip(tok->next, "(");
  Node *a = assign(&tok, tok);
  add_type(a);
  if (!is_vector(a->ty))
    error_tok(a->tok, "%s needs a vector", get_ident(start));

  Obj *va = new_lvar("", unqual(a->ty));
  Node *node = new_binary(ND_ASSIGN, new_var_node(va, start), a, start);
  Obj *vb = NULL;
  Obj *r;
  int64_t na = a->ty->array_len;
  int64_t idx[16];
  int n = 0;

  if (shuffle) {
    tok = skip(tok, ",");
    Node *b = assign(&tok, tok);
    add_type(b);
    if (!is_vector(b->ty) || !is_compatible(a->ty->elem, b->ty->elem))
      error_tok(b->tok, "__builtin_shufflevector needs two vectors of the same element type");
    vb = new_lvar("", unqual(b->ty));
    node = new_binary(ND_COMMA, node,
                      new_binary(ND_ASSIGN, new_var_node(vb, start), b, start), start);
    while (consume(&tok, tok, ",")) {
      Node *i = assign(&tok, tok);
      add_type(i);
      if (!is_integer(i->ty) || !is_const_expr(i))
        error_tok(i->tok, "a shuffle index must be an integer constant");
      int64_t val = eval(i);
      if (val < -1 || val >= na + b->ty->array_len)
        error_tok(i->tok, "shuffle index %ld out of range", val);
      if (n == 16)
        error_tok(i->tok, "too many shuffle indexes");
      idx[n++] = val;
    }
    r = new_lvar("", vector_n(a->ty->elem, n, start));
  } else {
    tok = skip(tok, ",");
    Type *ty = typename(&tok, tok);
    if (!is_vector(ty) || ty->array_len != na)
      error_tok(start, "__builtin_convertvector needs a vector type with as many elements");
    r = new_lvar("", unqual(ty));
    n = na;
    for (int i = 0; i < n; i++)
      idx[i] = i;
  }
  *rest = skip(tok, ")");

  for (int i = 0; i < n; i++) {
    Node *dst = var_elem(r, i, start);
    Node *src;
    if (idx[i] < 0)
      src = new_num(0, start);
    else if (idx[i] < na)
      src = var_elem(va, idx[i], start);
    else
      src = var_elem(vb, idx[i] - na, start);
    node = new_binary(ND_COMMA, node,
                      new_binary(ND_ASSIGN, dst, new_cast(src, r->ty->elem), start), start);
  }
  return new_binary(ND_COMMA, node, new_var_node(r, start), start);
}

// <stdarg.h>'s va_start, va_end, va_copy and va_arg, as gcc's builtins:
// va_start copies the va_list the prologue made for a variadic function
// (__va_area__, see function()), and va_arg is ND_VA_ARG, which cgen.c
// turns into code that finds the next argument as the calling convention
// passed it.
static Node *va_builtin(Token **rest, Token *tok) {
  Token *start = tok;
  tok = skip(tok->next, "(");
  Node *ap = assign(&tok, tok);
  add_type(ap);

  if (equal(start, "__builtin_va_arg")) {
    tok = skip(tok, ",");
    Type *ty = typename(&tok, tok);
    *rest = skip(tok, ")");
    if (ty->kind == TY_VOID || ty->kind == TY_VLA || ty->kind == TY_FUNC)
      error_tok(start, "va_arg of this type is not supported");
    Node *node = new_unary(ND_VA_ARG, ap, start);
    node->ty = pointer_to(ty);
    // Where a struct passed in two registers is put back together
    node->var = new_lvar("", array_of(ty_long, 2));
    return new_unary(ND_DEREF, node, start);
  }

  Node *arg2 = NULL;
  if (consume(&tok, tok, ","))
    arg2 = assign(&tok, tok);
  *rest = skip(tok, ")");

  if (equal(start, "__builtin_va_end"))
    return new_cast(ap, ty_void);

  if (equal(start, "__builtin_va_copy")) {
    if (!arg2)
      error_tok(start, "va_copy needs two arguments");
    return new_binary(ND_ASSIGN, new_unary(ND_DEREF, ap, start),
                      new_unary(ND_DEREF, arg2, start), start);
  }

  // va_start. In C23 the second argument is optional, and it was never
  // needed here.
  if (!current_fn || !current_fn->va_area)
    error_tok(start, "va_start used in a function with fixed arguments");
  Node *area = new_cast(new_var_node(current_fn->va_area, start), pointer_to(va_elem_ty));
  return new_binary(ND_ASSIGN, new_unary(ND_DEREF, ap, start),
                    new_unary(ND_DEREF, area, start), start);
}

//---------- GNU builtins ----------------------------------------------------

// [GNU] Read-modify-write atomics: `__atomic_fetch_add(p, v, order)` and
// the rest are atomic_rmw() with an op. The fetch_op forms return the old
// value and the op_fetch forms the new one. The __c11 forms, which
// <stdatomic.h> uses, scale v by the pointed-to size when *p is a pointer,
// as C11 says; gcc's own builtins don't.
static struct {
  char *name;
  NodeKind op;
  bool return_old;
  bool is_c11;
} rmw_builtins[] = {
  {"__atomic_fetch_add", ND_ADD, true}, {"__atomic_add_fetch", ND_ADD, false},
  {"__atomic_fetch_sub", ND_SUB, true}, {"__atomic_sub_fetch", ND_SUB, false},
  {"__atomic_fetch_and", ND_BITAND, true}, {"__atomic_and_fetch", ND_BITAND, false},
  {"__atomic_fetch_or", ND_BITOR, true}, {"__atomic_or_fetch", ND_BITOR, false},
  {"__atomic_fetch_xor", ND_BITXOR, true}, {"__atomic_xor_fetch", ND_BITXOR, false},
  {"__sync_fetch_and_add", ND_ADD, true}, {"__sync_add_and_fetch", ND_ADD, false},
  {"__sync_fetch_and_sub", ND_SUB, true}, {"__sync_sub_and_fetch", ND_SUB, false},
  {"__sync_fetch_and_and", ND_BITAND, true}, {"__sync_and_and_fetch", ND_BITAND, false},
  {"__sync_fetch_and_or", ND_BITOR, true}, {"__sync_or_and_fetch", ND_BITOR, false},
  {"__sync_fetch_and_xor", ND_BITXOR, true}, {"__sync_xor_and_fetch", ND_BITXOR, false},
  {"__c11_atomic_fetch_add", ND_ADD, true, true},
  {"__c11_atomic_fetch_sub", ND_SUB, true, true},
  {"__c11_atomic_fetch_and", ND_BITAND, true, true},
  {"__c11_atomic_fetch_or", ND_BITOR, true, true},
  {"__c11_atomic_fetch_xor", ND_BITXOR, true, true},
};

// The other GNU builtins gnu_builtin() handles. The bit builtins also
// have l and ll forms (__builtin_clzl, ...), which take a long.
static char *gnu_builtin_names[] = {
  "__builtin_clz", "__builtin_ctz", "__builtin_popcount", "__builtin_parity",
  "__builtin_ffs", "__builtin_clrsb",
  "__builtin_bswap16", "__builtin_bswap32", "__builtin_bswap64",
  "__builtin_expect", "__builtin_expect_with_probability",
  "__builtin_assume_aligned", "__builtin_constant_p",
  "__builtin_frame_address", "__builtin_return_address",
  "__builtin_trap", "__builtin_prefetch",
  "__sync_val_compare_and_swap", "__sync_bool_compare_and_swap",
  "__sync_lock_test_and_set", "__sync_lock_release", "__sync_synchronize",
  "__atomic_load_n", "__atomic_store_n", "__atomic_exchange_n",
  "__atomic_compare_exchange_n", "__atomic_test_and_set", "__atomic_clear",
  "__atomic_thread_fence", "__atomic_signal_fence",
  "__atomic_always_lock_free", "__atomic_is_lock_free",
};

static char *bit_builtins[] = {
  "__builtin_clz", "__builtin_ctz", "__builtin_popcount", "__builtin_parity",
  "__builtin_ffs", "__builtin_clrsb",
};

// For a bit builtin, `__builtin_clz` (int), `__builtin_clzl` or
// `__builtin_clzll` (long), its name without the suffix and its argument
// size; NULL if `tok` isn't one.
static char *bit_builtin(Token *tok, int *size) {
  for (int i = 0; i < sizeof(bit_builtins) / sizeof(*bit_builtins); i++) {
    char *name = bit_builtins[i];
    int len = strlen(name);
    if (tok->len < len || strncmp(tok->loc, name, len))
      continue;
    char *suffix = tok->loc + len;
    int n = tok->len - len;
    if (n == 0 || (n == 1 && suffix[0] == 'l') ||
        (n == 2 && !strncmp(suffix, "ll", 2))) {
      *size = n ? 8 : 4;
      return name;
    }
  }
  return NULL;
}

static int rmw_builtin(Token *tok) {
  for (int i = 0; i < sizeof(rmw_builtins) / sizeof(*rmw_builtins); i++)
    if (equal(tok, rmw_builtins[i].name))
      return i;
  return -1;
}

// The overflow builtins on __int128 call this helper, defined from C
// (see define_helper()): `op` is 0, 1 or 2 for add, sub or mul, and a
// and b are 128-bit values, signed if `as` or `bs`. It stores the result
// in the `size`-byte integer at `res`, signed if `rs`, and returns whether
// the exact result didn't fit. The exact result is a sign, a magnitude
// and the magnitude's bits above 128 (hi), up to 256 for a product.
static char *overflow128_text =
  "static _Bool __mucc_overflow128(int op, unsigned __int128 a, int as,\n"
  "                                unsigned __int128 b, int bs, void *res, int size, int rs) {\n"
  "  _Bool an = as && (__int128)a < 0, bn = bs && (__int128)b < 0, rn;\n"
  "  unsigned __int128 am = an ? -a : a, bm = bn ? -b : b, rm, hi = 0;\n"
  "  if (op == 2) {\n"
  "    unsigned __int128 a0 = (unsigned long)am, a1 = am >> 64;\n"
  "    unsigned __int128 b0 = (unsigned long)bm, b1 = bm >> 64;\n"
  "    unsigned __int128 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;\n"
  "    unsigned __int128 mid = (p00 >> 64) + (unsigned long)p01 + (unsigned long)p10;\n"
  "    rm = mid << 64 | (unsigned long)p00;\n"
  "    hi = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);\n"
  "    rn = an != bn && (rm || hi);\n"
  "  } else {\n"
  "    if (op == 1)\n"
  "      bn = !bn && bm;\n"
  "    if (an == bn) {\n"
  "      rm = am + bm;\n"
  "      hi = rm < am;\n"
  "      rn = an;\n"
  "    } else if (am >= bm) {\n"
  "      rm = am - bm;\n"
  "      rn = an && rm;\n"
  "    } else {\n"
  "      rm = bm - am;\n"
  "      rn = bn;\n"
  "    }\n"
  "  }\n"
  "  int bits = size * 8;\n"
  "  _Bool fits;\n"
  "  if (hi)\n"
  "    fits = 0;\n"
  "  else if (rs)\n"
  "    fits = rn ? rm <= (unsigned __int128)1 << (bits - 1) : rm < (unsigned __int128)1 << (bits - 1);\n"
  "  else\n"
  "    fits = !rn && (bits == 128 || rm >> bits == 0);\n"
  "  unsigned __int128 v = rn ? -rm : rm;\n"
  "  unsigned char *p = res;\n"
  "  for (int i = 0; i < size; i++, v >>= 8)\n"
  "    p[i] = v;\n"
  "  return !fits;\n"
  "}\n";

static Obj *overflow128_fn;
static Token *overflow128_use;

// A call of the helper above for `*res = a op b`
static Node *overflow128_call(NodeKind op, Node *a, Node *b, Node *res, Token *tok) {
  if (!overflow128_fn) {
    Type *param_tys[] = {ty_int, ty_uint128, ty_int, ty_uint128, ty_int,
                         pointer_to(ty_void), ty_int, ty_int};
    Type *ty = func_type(ty_bool);
    Type head = {};
    Type *cur = &head;
    for (int i = 0; i < 8; i++)
      cur = cur->next = copy_type(param_tys[i]);
    ty->params = head.next;

    Obj *fn = overflow128_fn = arena_alloc(sizeof(Obj));
    fn->name = "__mucc_overflow128";
    fn->ty = ty;
    fn->align = 1;
    fn->is_function = true;
    fn->is_static = true;
    fn->is_used = true;
    fn->next = globals;
    globals = fn;
    overflow128_use = tok;
  }

  Type *base = res->ty->base;
  Node *args[] = {
    new_num(op == ND_ADD ? 0 : op == ND_SUB ? 1 : 2, tok),
    new_cast(a, ty_uint128), new_num(!a->ty->is_unsigned, tok),
    new_cast(b, ty_uint128), new_num(!b->ty->is_unsigned, tok),
    new_cast(res, pointer_to(ty_void)), new_num(base->size, tok),
    new_num(!base->is_unsigned, tok),
  };
  Node *call = new_unary(ND_FUNCALL, new_var_node(overflow128_fn, tok), tok);
  call->func_ty = overflow128_fn->ty;
  call->ty = ty_bool;
  Node head = {};
  Node *cur = &head;
  for (int i = 0; i < 8; i++) {
    add_type(args[i]);
    cur = cur->next = args[i];
  }
  call->args = head.next;
  add_type(call->lhs);
  return call;
}

// For `__builtin_add_overflow` (or sub, mul), its op, with *ty NULL; for
// the typed forms, like `__builtin_saddl_overflow`, also the type they
// take: int, long or long long, signed (s) or unsigned (u). False if
// `tok` isn't one.
static bool overflow_builtin(Token *tok, NodeKind *op, Type **ty) {
  char *pre = "__builtin_", *post = "_overflow";
  int len = tok->len - strlen(pre) - strlen(post);
  if (len < 3 || strncmp(tok->loc, pre, strlen(pre)) ||
      strncmp(tok->loc + tok->len - strlen(post), post, strlen(post)))
    return false;

  char *s = tok->loc + strlen(pre);
  *ty = NULL;
  if (len > 3) {
    if (*s != 's' && *s != 'u')
      return false;
    bool is_unsigned = (*s++ == 'u');
    char *suffix = s + 3;
    int n = len - 4;
    if (n == 0)
      *ty = is_unsigned ? ty_uint : ty_int;
    else if ((n == 1 && suffix[0] == 'l') || (n == 2 && !strncmp(suffix, "ll", 2)))
      *ty = is_unsigned ? ty_ulong : ty_long;
    else
      return false;
  }

  if (!strncmp(s, "add", 3))
    *op = ND_ADD;
  else if (!strncmp(s, "sub", 3))
    *op = ND_SUB;
  else if (!strncmp(s, "mul", 3))
    *op = ND_MUL;
  else
    return false;
  return true;
}

static int fp_builtin_index(Token *tok);
static char *libc_builtin_sig(Token *tok);

static bool is_gnu_builtin(Token *tok) {
  int size;
  NodeKind op;
  Type *ty;
  for (int i = 0; i < sizeof(gnu_builtin_names) / sizeof(*gnu_builtin_names); i++)
    if (equal(tok, gnu_builtin_names[i]))
      return true;
  return bit_builtin(tok, &size) || rmw_builtin(tok) >= 0 ||
         overflow_builtin(tok, &op, &ty) || fp_builtin_index(tok) >= 0;
}

// The bits a bit builtin counts in a constant, so it can be folded.
static int count_bits(NodeKind kind, uint64_t x, int bits) {
  int n = 0;
  if (kind == ND_POPCOUNT) {
    for (; x; x &= x - 1)
      n++;
    return n;
  }
  if (kind == ND_CTZ) {
    while (n < bits && !(x >> n & 1))
      n++;
    return n;
  }
  while (n < bits && !(x >> (bits - 1 - n) & 1))
    n++;
  return n;
}

// clz, ctz or popcount of `arg` as type `ty`: a number if it's constant,
// as with gcc, so it can size an array or be a case label.
static Node *count_bits_node(NodeKind kind, Node *arg, Type *ty, Token *tok) {
  arg = new_cast(arg, ty);
  if (is_const_expr(arg)) {
    uint64_t x = eval(arg);
    if (ty->size == 4)
      x = (uint32_t)x;
    return new_num(count_bits(kind, x, ty->size * 8), tok);
  }
  return new_unary(kind, arg, tok);
}

static Node *bswap_node(Node *arg, Type *ty, Token *tok) {
  arg = new_cast(arg, ty);
  if (is_const_expr(arg)) {
    uint64_t x = eval(arg), y = 0;
    for (int i = 0; i < ty->size; i++)
      y = y << 8 | (x >> (i * 8) & 0xff);
    Node *node = new_num(y, tok);
    node->ty = ty;
    return node;
  }
  return new_unary(ND_BSWAP, arg, tok);
}

// The type `*p` has without _Atomic, for a temporary holding its value.
static Type *pointee_type(Node *p) {
  add_type(p);
  if (!p->ty->base)
    error_tok(p->tok, "pointer expected");
  Type *ty = p->ty->base;
  if (ty->is_atomic) {
    ty = copy_type(ty);
    ty->is_atomic = false;
  }
  return ty;
}

static Node *void_node(Token *tok) {
  return new_cast(new_num(0, tok), ty_void);
}

// [GNU] __builtin_memcpy and the like are the C library function of that
// name, which gcc calls too at -O0. The program needn't have declared it:
// its type comes from here, the return type first, then the parameters,
// with '.' for `...`: v void, i int, l long, L long long, z size_t,
// d double, f float, D long double, p void *, P const void *, s char *,
// S const char *.
static struct {
  char *name;
  char *sig;
} libc_builtins[] = {
  {"memcpy", "ppPz"}, {"memmove", "ppPz"}, {"memset", "ppiz"},
  {"memcmp", "iPPz"}, {"memchr", "pPiz"}, {"strlen", "zS"},
  {"strnlen", "zSz"}, {"strcmp", "iSS"}, {"strncmp", "iSSz"},
  {"strcpy", "ssS"}, {"strncpy", "ssSz"}, {"strcat", "ssS"},
  {"strncat", "ssSz"}, {"strchr", "sSi"}, {"strrchr", "sSi"},
  {"strstr", "sSS"}, {"strspn", "zSS"}, {"strcspn", "zSS"},
  {"strpbrk", "sSS"}, {"strdup", "sS"}, {"strndup", "sSz"},
  {"abs", "ii"}, {"labs", "ll"}, {"llabs", "LL"},
  {"sqrt", "dd"}, {"sqrtf", "ff"}, {"sqrtl", "DD"},
  {"floor", "dd"}, {"floorf", "ff"}, {"ceil", "dd"}, {"ceilf", "ff"},
  {"round", "dd"}, {"roundf", "ff"}, {"trunc", "dd"}, {"truncf", "ff"},
  {"fmod", "ddd"}, {"fmin", "ddd"}, {"fmax", "ddd"}, {"pow", "ddd"},
  {"exp", "dd"}, {"log", "dd"}, {"log2", "dd"}, {"log10", "dd"},
  {"sin", "dd"}, {"cos", "dd"}, {"tan", "dd"}, {"atan2", "ddd"},
  {"ldexp", "ddi"},
  {"malloc", "pz"}, {"calloc", "pzz"}, {"realloc", "ppz"}, {"free", "vp"},
  {"abort", "v"}, {"exit", "vi"}, {"_exit", "vi"},
  {"printf", "iS."}, {"sprintf", "isS."}, {"snprintf", "iszS."},
  {"fprintf", "ipS."}, {"puts", "iS"}, {"putchar", "ii"},
  {"fputs", "iSp"}, {"fputc", "iip"}, {"fwrite", "zPzzp"},
};

static Type *libc_builtin_type(char c) {
  switch (c) {
  case 'v': return ty_void;
  case 'i': return ty_int;
  case 'l': return ty_long;
  case 'L': return ty_llong;
  case 'z': return ty_ulong;
  case 'd': return ty_double;
  case 'f': return ty_float;
  case 'D': return ty_ldouble;
  case 'p': return pointer_to(ty_void);
  case 'P': return pointer_to(qualified(ty_void, true, false));
  case 's': return pointer_to(ty_char);
  case 'S': return pointer_to(qualified(ty_char, true, false));
  }
  unreachable();
}

static bool is_libc_noreturn(char *name);
static Obj *find_func(char *name);

// The type string of `tok`, `__builtin_<name>`, from libc_builtins[], or
// NULL if it isn't one of them.
static char *libc_builtin_sig(Token *tok) {
  int pre = strlen("__builtin_");
  if (tok->kind != TK_IDENT || tok->len <= pre || strncmp(tok->loc, "__builtin_", pre))
    return NULL;
  for (int i = 0; i < sizeof(libc_builtins) / sizeof(*libc_builtins); i++) {
    char *name = libc_builtins[i].name;
    if (strlen(name) == tok->len - pre && !strncmp(name, tok->loc + pre, tok->len - pre))
      return libc_builtins[i].sig;
  }
  return NULL;
}

// The function `tok`, `__builtin_<name>`, calls, or NULL if it isn't one
// of libc_builtins[]: the program's own declaration of it if there is
// one, or else a declaration made here.
static Obj *libc_builtin(Token *tok) {
  static HashMap declared;
  char *sig = libc_builtin_sig(tok);
  if (!sig)
    return NULL;
  char *name = strndup(tok->loc + strlen("__builtin_"), tok->len - strlen("__builtin_"));

  Obj *fn = find_func(name);
  if (!fn)
    fn = hashmap_get(&declared, name);
  if (fn)
    return fn;

  Type *ty = func_type(libc_builtin_type(sig[0]));
  Type head = {};
  Type *cur = &head;
  for (char *p = sig + 1; *p; p++) {
    if (*p == '.')
      ty->is_variadic = true;
    else
      cur = cur->next = copy_type(libc_builtin_type(*p));
  }
  ty->params = head.next;

  fn = arena_alloc(sizeof(Obj));
  fn->name = name;
  fn->ty = ty;
  fn->align = 1;
  fn->is_function = true;
  fn->is_noreturn = is_libc_noreturn(name);
  hashmap_put(&declared, name, fn);
  return fn;
}

// [GNU] Builtins that need no library, each with how many arguments it
// takes. fp_builtin() makes them.
static struct {
  char *name;
  int nargs;
} fp_builtins[] = {
  {"__builtin_inf", 0}, {"__builtin_inff", 0}, {"__builtin_infl", 0},
  {"__builtin_huge_val", 0}, {"__builtin_huge_valf", 0},
  {"__builtin_huge_vall", 0},
  {"__builtin_nan", 1}, {"__builtin_nanf", 1}, {"__builtin_nanl", 1},
  {"__builtin_isnan", 1}, {"__builtin_isinf", 1}, {"__builtin_isfinite", 1},
  {"__builtin_isnormal", 1}, {"__builtin_isinf_sign", 1},
  {"__builtin_signbit", 1}, {"__builtin_signbitf", 1},
  {"__builtin_signbitl", 1}, {"__builtin_fpclassify", 6},
  {"__builtin_isgreater", 2}, {"__builtin_isgreaterequal", 2},
  {"__builtin_isless", 2}, {"__builtin_islessequal", 2},
  {"__builtin_islessgreater", 2}, {"__builtin_isunordered", 2},
  {"__builtin_fabs", 1}, {"__builtin_fabsf", 1}, {"__builtin_fabsl", 1},
  {"__builtin_copysign", 2}, {"__builtin_copysignf", 2},
  {"__builtin_copysignl", 2},
  {"__builtin_choose_expr", 3}, {"__builtin_object_size", 2},
  {"__builtin_dynamic_object_size", 2}, {"__builtin_speculation_safe_value", 1},
  {"__builtin_LINE", 0}, {"__builtin_FILE", 0}, {"__builtin_FUNCTION", 0},
};

static int fp_builtin_index(Token *tok) {
  for (int i = 0; i < sizeof(fp_builtins) / sizeof(*fp_builtins); i++)
    if (equal(tok, fp_builtins[i].name))
      return i;
  return -1;
}

static Node *new_flonum(long double val, Type *ty, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->fval = arena_alloc(sizeof(long double));
  *node->fval = val;
  node->ty = ty;
  return node;
}

// The floating-point type a builtin's name says (an f or l at its end),
// or NULL when it takes any.
static Type *fp_suffix_type(Token *tok, int base_len) {
  if (tok->len == base_len)
    return ty_double;
  return tok->loc[base_len] == 'f' ? ty_float : ty_ldouble;
}

// `T tmp = arg;` in *init, and tmp: a builtin that looks at its argument
// more than once evaluates it once. With `ty` NULL, T is arg's own type,
// which must be floating.
static Obj *fp_temp(Node *arg, Type *ty, Node **init, Token *tok) {
  add_type(arg);
  if (!ty) {
    if (!is_flonum(arg->ty))
      error_tok(arg->tok, "floating-point argument expected, not '%s'",
                type_name(arg->ty));
    ty = unqual(arg->ty);
  }
  Obj *var = new_lvar("", ty);
  *init = new_binary(ND_ASSIGN, new_var_node(var, tok), new_cast(arg, ty), tok);
  return var;
}

// The integer in `var` (float, double or long double) that holds its sign
// bit, as an lvalue, and the bit's mask.
static Node *sign_word(Obj *var, uint64_t *mask, Token *tok) {
  Node *addr = new_unary(ND_ADDR, new_var_node(var, tok), tok);
  if (var->ty->kind == TY_FLOAT) {
    *mask = 0x80000000;
    return new_unary(ND_DEREF, new_cast(addr, pointer_to(ty_uint)), tok);
  }
  if (var->ty->kind == TY_DOUBLE) {
    *mask = (uint64_t)1 << 63;
    return new_unary(ND_DEREF, new_cast(addr, pointer_to(ty_ulong)), tok);
  }
  // x87's 80 bits: the sign is the top bit of the 16-bit word at byte 8.
  *mask = 0x8000;
  Node *word = new_add(new_cast(addr, pointer_to(ty_ushort)), new_num(4, tok), tok);
  return new_unary(ND_DEREF, word, tok);
}

static Node *new_comma(Node *lhs, Node *rhs, Token *tok) {
  return new_binary(ND_COMMA, lhs, rhs, tok);
}

static Node *new_cond(Node *c, Node *then, Node *els, Token *tok) {
  Node *node = new_node(ND_COND, tok);
  node->cond = c;
  node->then = then;
  node->els = els;
  return node;
}

// |var|, for comparing with infinity and the smallest normal number
static Node *fp_abs(Obj *var, Token *tok) {
  Node *is_neg = new_binary(ND_LT, new_var_node(var, tok), new_flonum(0, var->ty, tok), tok);
  return new_cond(is_neg, new_unary(ND_NEG, new_var_node(var, tok), tok),
                  new_var_node(var, tok), tok);
}

// The smallest normal number of a floating-point type
static long double fp_min_normal(Type *ty) {
  if (ty->kind == TY_FLOAT)
    return 0x1p-126L;
  if (ty->kind == TY_DOUBLE)
    return 0x1p-1022L;
  return 0x1p-16382L;
}

// One of fp_builtins[], named by `start`, with its arguments.
static Node *fp_builtin(Token *start, Node **args) {
  Token *tok = start;
  char *name = strndup(start->loc, start->len);

  if (!strncmp(name, "__builtin_inf", 13) || !strncmp(name, "__builtin_huge_val", 18)) {
    int len = name[10] == 'i' ? 13 : 18;
    return new_flonum(strtold("inf", NULL), fp_suffix_type(start, len), tok);
  }

  if (!strncmp(name, "__builtin_nan", 13)) {
    Node *arg = args[0];
    if (arg->kind != ND_VAR || !arg->var->is_string)
      error_tok(arg->tok, "a string literal expected");
    char *payload = format("nan(%s)", arg->var->init_data);
    return new_flonum(strtold(payload, NULL), fp_suffix_type(start, 13), tok);
  }

  if (equal(start, "__builtin_choose_expr")) {
    if (!is_const_expr(args[0]))
      error_tok(args[0]->tok, "a constant expression expected");
    return eval(args[0]) ? args[1] : args[2];
  }

  // The size of the object a pointer points to: known for an array or
  // `&var`, as gcc knows it even at -O0. Otherwise it's unknown, which
  // gcc says as (size_t)-1, or 0 for types 2 and 3.
  if (equal(start, "__builtin_object_size") ||
      equal(start, "__builtin_dynamic_object_size")) {
    Node *p = args[0];
    while (p->kind == ND_CAST)
      p = p->lhs;
    add_type(p);
    if (p->kind == ND_VAR && p->ty->kind == TY_ARRAY)
      return new_ulong(p->ty->size, tok);
    if (p->kind == ND_ADDR && p->lhs->kind == ND_VAR && p->lhs->ty->kind != TY_VLA)
      return new_ulong(p->lhs->ty->size, tok);
    return new_ulong((eval(args[1]) & 2) ? 0 : -1, tok);
  }

  if (equal(start, "__builtin_speculation_safe_value"))
    return args[0];

  // Where the call is, as __LINE__, __FILE__ and __func__ say
  if (equal(start, "__builtin_LINE") || equal(start, "__builtin_FILE")) {
    Token *t = start;
    while (t->origin)
      t = t->origin;
    if (equal(start, "__builtin_LINE"))
      return new_num(t->line_no, tok);
    char *file = t->file->display_name;
    return new_var_node(new_string_literal(file, array_of(ty_char, strlen(file) + 1)), tok);
  }
  if (equal(start, "__builtin_FUNCTION")) {
    char *fn = current_fn ? current_fn->name : "";
    return new_var_node(new_string_literal(fn, array_of(ty_char, strlen(fn) + 1)), tok);
  }

  // The comparisons that are quiet on a NaN: on x86 an ordinary
  // comparison is, and is false when either side is a NaN.
  if (!strncmp(name, "__builtin_is", 12) && strcmp(name, "__builtin_isnan") &&
      strcmp(name, "__builtin_isinf") && strcmp(name, "__builtin_isfinite") &&
      strcmp(name, "__builtin_isnormal") && strcmp(name, "__builtin_isinf_sign")) {
    Node *init_a, *init_b;
    Obj *a = fp_temp(args[0], NULL, &init_a, tok);
    Obj *b = fp_temp(args[1], NULL, &init_b, tok);
    Node *x = new_var_node(a, tok), *y = new_var_node(b, tok);
    Node *res;
    if (equal(start, "__builtin_isgreater"))
      res = new_binary(ND_LT, y, x, tok);
    else if (equal(start, "__builtin_isgreaterequal"))
      res = new_binary(ND_LE, y, x, tok);
    else if (equal(start, "__builtin_isless"))
      res = new_binary(ND_LT, x, y, tok);
    else if (equal(start, "__builtin_islessequal"))
      res = new_binary(ND_LE, x, y, tok);
    else if (equal(start, "__builtin_islessgreater"))
      res = new_binary(ND_LOGOR, new_binary(ND_LT, x, y, tok),
                       new_binary(ND_LT, new_var_node(b, tok), new_var_node(a, tok), tok),
                       tok);
    else
      res = new_binary(ND_LOGOR, new_binary(ND_NE, x, new_var_node(a, tok), tok),
                       new_binary(ND_NE, y, new_var_node(b, tok), tok), tok);
    return new_comma(init_a, new_comma(init_b, res, tok), tok);
  }

  // fabs and copysign clear and copy the sign bit, as gcc does, so they
  // need no libm and get -0.0 and NaNs right.
  if (!strncmp(name, "__builtin_fabs", 14) || !strncmp(name, "__builtin_copysign", 18)) {
    bool is_fabs = name[10] == 'f';
    Type *ty = fp_suffix_type(start, is_fabs ? 14 : 18);
    Node *init;
    Obj *var = fp_temp(args[0], ty, &init, tok);
    uint64_t mask;
    Node *word = sign_word(var, &mask, tok);
    Node *bits = new_binary(ND_BITAND, sign_word(var, &mask, tok),
                            new_ulong(~mask, tok), tok);
    if (!is_fabs) {
      Node *init2;
      Obj *from = fp_temp(args[1], ty, &init2, tok);
      init = new_comma(init, init2, tok);
      bits = new_binary(ND_BITOR, bits,
                        new_binary(ND_BITAND, sign_word(from, &mask, tok),
                                   new_ulong(mask, tok), tok),
                        tok);
    }
    Node *set = new_binary(ND_ASSIGN, word, bits, tok);
    return new_comma(init, new_comma(set, new_var_node(var, tok), tok), tok);
  }

  // The classifications, on one argument of any floating type
  Node *x = equal(start, "__builtin_fpclassify") ? args[5] : args[0];
  Node *init;
  Obj *var = fp_temp(x, NULL, &init, tok);
  Type *ty = var->ty;
  Node *v = new_var_node(var, tok);
  Node *inf = new_flonum(strtold("inf", NULL), ty, tok);

  if (!strncmp(name, "__builtin_signbit", 17)) {
    uint64_t mask;
    Node *word = sign_word(var, &mask, tok);
    Node *bit = new_binary(ND_BITAND, word, new_ulong(mask, tok), tok);
    return new_comma(init, new_binary(ND_NE, bit, new_num(0, tok), tok), tok);
  }

  Node *is_nan = new_binary(ND_NE, v, new_var_node(var, tok), tok);
  Node *is_inf = new_binary(ND_EQ, fp_abs(var, tok), inf, tok);
  Node *res;

  if (equal(start, "__builtin_isnan"))
    res = is_nan;
  else if (equal(start, "__builtin_isinf"))
    res = is_inf;
  else if (equal(start, "__builtin_isfinite"))
    // x - x is 0 for a finite x, and a NaN for an infinity or a NaN.
    res = new_binary(ND_EQ,
                     new_binary(ND_SUB, new_var_node(var, tok), new_var_node(var, tok), tok),
                     new_flonum(0, ty, tok), tok);
  else if (equal(start, "__builtin_isinf_sign"))
    res = new_cond(new_binary(ND_EQ, new_var_node(var, tok),
                             new_flonum(strtold("inf", NULL), ty, tok), tok),
                   new_num(1, tok),
               new_cond(new_binary(ND_EQ, new_var_node(var, tok),
                               new_flonum(-strtold("inf", NULL), ty, tok), tok),
                    new_num(-1, tok), new_num(0, tok), tok),
               tok);
  else if (equal(start, "__builtin_isnormal"))
    res = new_binary(ND_LOGAND,
                     new_binary(ND_LE, new_flonum(fp_min_normal(ty), ty, tok),
                                fp_abs(var, tok), tok),
                     new_binary(ND_LT, fp_abs(var, tok),
                                new_flonum(strtold("inf", NULL), ty, tok), tok),
                     tok);
  else
    // fpclassify(nan, infinite, normal, subnormal, zero, x)
    res = new_cond(is_nan, args[0],
               new_cond(is_inf, args[1],
                    new_cond(new_binary(ND_LE, new_flonum(fp_min_normal(ty), ty, tok),
                                        fp_abs(var, tok), tok),
                         args[2],
                         new_cond(new_binary(ND_EQ, new_var_node(var, tok),
                                         new_flonum(0, ty, tok), tok),
                              args[4], args[3], tok),
                         tok),
                    tok),
               tok);
  return new_comma(init, res, tok);
}

// [GNU] gcc's bit-counting, atomic and other builtins, which real code
// often calls without checking for gcc. `tok` is one of them.
static Node *gnu_builtin(Token **rest, Token *tok) {
  Token *start = tok;
  Node *args[6];
  int nargs = 0;

  tok = skip(tok->next, "(");
  if (!equal(tok, ")")) {
    do {
      if (nargs == 6)
        error_tok(tok, "too many arguments");
      args[nargs++] = assign(&tok, tok);
    } while (consume(&tok, tok, ","));
  }
  *rest = skip(tok, ")");

  int fp = fp_builtin_index(start);
  if (fp >= 0) {
    if (nargs != fp_builtins[fp].nargs)
      error_tok(start, "'%s' takes %d argument%s", fp_builtins[fp].name,
                fp_builtins[fp].nargs, fp_builtins[fp].nargs == 1 ? "" : "s");
    return fp_builtin(start, args);
  }

  int size;
  char *bit = bit_builtin(start, &size);
  int rmw = rmw_builtin(start);
  NodeKind overflow_op;
  Type *overflow_ty;
  bool overflow = overflow_builtin(start, &overflow_op, &overflow_ty);

  // The fewest arguments each takes
  int min = 1;
  if (equal(start, "__sync_synchronize") || equal(start, "__builtin_trap"))
    min = 0;
  else if (rmw >= 0 || equal(start, "__builtin_expect") ||
           equal(start, "__atomic_load_n") ||
           equal(start, "__atomic_exchange_n") ||
           equal(start, "__atomic_store_n") ||
           equal(start, "__atomic_always_lock_free") ||
           equal(start, "__atomic_is_lock_free") ||
           equal(start, "__builtin_assume_aligned") ||
           equal(start, "__sync_lock_test_and_set"))
    min = 2;
  else if (overflow || equal(start, "__sync_val_compare_and_swap") ||
           equal(start, "__sync_bool_compare_and_swap") ||
           equal(start, "__builtin_expect_with_probability") ||
           equal(start, "__atomic_compare_exchange_n"))
    min = 3;
  if (nargs < min)
    error_tok(start, "too few arguments to '%.*s'", start->len, start->loc);

  // *res = a op b, and whether the exact result didn't fit *res. a and b
  // become 64-bit values, keeping their sign, for cgen.c's 128-bit sum.
  if (overflow) {
    Node *a = args[0], *b = args[1], *res = args[2];
    if (overflow_ty) {
      a = new_cast(a, overflow_ty);
      b = new_cast(b, overflow_ty);
    }
    add_type(a);
    add_type(b);
    add_type(res);
    if (!is_integer(a->ty) || !is_integer(b->ty))
      error_tok(start, "integer operands expected");
    if (res->ty->kind != TY_PTR || !is_integer(res->ty->base) ||
        res->ty->base->kind == TY_BOOL)
      error_tok(res->tok, "pointer to an integer type expected");
    if (overflow_ty && res->ty->base->size != overflow_ty->size)
      error_tok(res->tok, "pointer to '%s' expected", type_name(overflow_ty));
    // On __int128, a helper does it (as the rest is checked in 64 bits).
    if (is_int128(a->ty) || is_int128(b->ty) || is_int128(res->ty->base))
      return overflow128_call(overflow_op, a, b, res, start);

    Node *node = new_node(ND_OVERFLOW, start);
    node->lhs = new_cast(a, a->ty->is_unsigned ? ty_ulong : ty_long);
    node->rhs = new_cast(b, b->ty->is_unsigned ? ty_ulong : ty_long);
    node->cas_addr = res;
    node->val = overflow_op;
    return node;
  }

  if (bit) {
    Type *ty = (size == 8) ? ty_ulong : ty_uint;
    Type *sty = (size == 8) ? ty_long : ty_int;

    if (!strcmp(bit, "__builtin_clz"))
      return count_bits_node(ND_CLZ, args[0], ty, start);
    if (!strcmp(bit, "__builtin_ctz"))
      return count_bits_node(ND_CTZ, args[0], ty, start);
    if (!strcmp(bit, "__builtin_popcount"))
      return count_bits_node(ND_POPCOUNT, args[0], ty, start);
    if (!strcmp(bit, "__builtin_parity"))
      return new_binary(ND_BITAND,
                        count_bits_node(ND_POPCOUNT, args[0], ty, start),
                        new_num(1, start), start);

    // ffs(x) is `x ? ctz(x) + 1 : 0`, and clrsb(x), the number of bits
    // after the sign bit that equal it, is clz(((x ^ (x >> (N-1))) << 1) | 1).
    // Both need x once, so it's put in a temporary.
    Obj *var = new_lvar("", sty);
    Node *init = new_binary(ND_ASSIGN, new_var_node(var, start),
                            args[0], start);
    Node *x = new_var_node(var, start);

    if (!strcmp(bit, "__builtin_ffs")) {
      Node *node = new_node(ND_COND, start);
      node->cond = x;
      node->then = new_binary(ND_ADD,
                              count_bits_node(ND_CTZ, new_var_node(var, start),
                                              ty, start),
                              new_num(1, start), start);
      node->els = new_num(0, start);
      return new_binary(ND_COMMA, init, node, start);
    }

    Node *sign = new_binary(ND_SHR, new_var_node(var, start),
                            new_num(size * 8 - 1, start), start);
    Node *y = new_binary(ND_BITXOR, x, sign, start);
    y = new_binary(ND_BITOR,
                   new_binary(ND_SHL, new_cast(y, ty), new_num(1, start), start),
                   new_num(1, start), start);
    return new_binary(ND_COMMA, init, count_bits_node(ND_CLZ, y, ty, start),
                      start);
  }

  if (equal(start, "__builtin_bswap16"))
    return bswap_node(args[0], ty_ushort, start);
  if (equal(start, "__builtin_bswap32"))
    return bswap_node(args[0], ty_uint, start);
  if (equal(start, "__builtin_bswap64"))
    return bswap_node(args[0], ty_ulong, start);

  // Hints: the value of the first argument
  if (equal(start, "__builtin_expect") ||
      equal(start, "__builtin_expect_with_probability"))
    return new_cast(args[0], ty_long);
  if (equal(start, "__builtin_assume_aligned"))
    return new_cast(args[0], pointer_to(ty_void));
  if (equal(start, "__builtin_prefetch"))
    return new_cast(args[0], ty_void);

  // Whether the argument is a constant. It isn't evaluated.
  if (equal(start, "__builtin_constant_p"))
    return new_num(is_const_expr(args[0]), start);

  if (equal(start, "__builtin_frame_address") ||
      equal(start, "__builtin_return_address")) {
    Node *node = new_node(ND_FRAME_ADDR, start);
    node->val = eval(args[0]);
    if (node->val < 0)
      error_tok(args[0]->tok, "the level must not be negative");
    if (equal(start, "__builtin_frame_address"))
      return node;

    // The return address is just above the saved %rbp.
    Node *addr = new_binary(ND_ADD, new_cast(node, ty_ulong),
                            new_ulong(8, start), start);
    return new_unary(ND_DEREF,
                     new_cast(addr, pointer_to(pointer_to(ty_void))), start);
  }

  if (equal(start, "__builtin_trap")) {
    Node *node = new_node(ND_TRAP, start);
    node->ty = ty_void;
    return node;
  }

  if (rmw >= 0) {
    Node *val = args[1];
    if (rmw_builtins[rmw].is_c11) {
      Type *ty = pointee_type(args[0]);
      if (ty->kind == TY_PTR)
        val = new_binary(ND_MUL, new_cast(val, ty_long),
                         new_long(ty->base->size, start), start);
    }
    return atomic_rmw(args[0], val, rmw_builtins[rmw].op,
                      rmw_builtins[rmw].return_old, start);
  }

  // `T tmp = old;` then a compare-and-swap with &tmp, which leaves the
  // value *p had in tmp.
  if (equal(start, "__sync_val_compare_and_swap") ||
      equal(start, "__sync_bool_compare_and_swap")) {
    Obj *var = new_lvar("", pointee_type(args[0]));
    Node *init = new_binary(ND_ASSIGN, new_var_node(var, start), args[1], start);
    Node *cas = new_node(ND_CAS, start);
    cas->cas_addr = args[0];
    cas->cas_old = new_unary(ND_ADDR, new_var_node(var, start), start);
    cas->cas_new = new_cast(args[2], var->ty);
    if (equal(start, "__sync_bool_compare_and_swap"))
      return new_binary(ND_COMMA, init, cas, start);
    return new_binary(ND_COMMA, init,
                      new_binary(ND_COMMA, cas, new_var_node(var, start), start),
                      start);
  }

  if (equal(start, "__atomic_compare_exchange_n")) {
    Node *cas = new_node(ND_CAS, start);
    cas->cas_addr = args[0];
    cas->cas_old = args[1];
    cas->cas_new = new_cast(args[2], pointee_type(args[0]));
    return cas;
  }

  // x86 loads are atomic, and xchg is a sequentially consistent store.
  if (equal(start, "__atomic_load_n"))
    return new_unary(ND_DEREF, args[0], start);
  if (equal(start, "__atomic_exchange_n") ||
      equal(start, "__sync_lock_test_and_set") ||
      equal(start, "__atomic_store_n")) {
    Node *val = new_cast(args[1], pointee_type(args[0]));
    Node *exch = new_binary(ND_EXCH, args[0], val, start);
    if (equal(start, "__atomic_store_n"))
      return new_cast(exch, ty_void);
    return exch;
  }
  if (equal(start, "__sync_lock_release"))
    return new_cast(new_binary(ND_ASSIGN, new_unary(ND_DEREF, args[0], start),
                               new_num(0, start), start),
                    ty_void);

  // On a byte, which becomes 1 or 0
  if (equal(start, "__atomic_test_and_set") || equal(start, "__atomic_clear")) {
    bool set = equal(start, "__atomic_test_and_set");
    Node *exch = new_binary(ND_EXCH, new_cast(args[0], pointer_to(ty_uchar)),
                            new_num(set, start), start);
    return new_cast(exch, set ? ty_bool : ty_void);
  }

  // Only a sequentially consistent fence needs an instruction on x86;
  // the others just keep the compiler from moving memory accesses,
  // which mucc doesn't do. The order is __ATOMIC_SEQ_CST (5) or another.
  if (equal(start, "__sync_synchronize"))
    return new_node(ND_FENCE, start);
  if (equal(start, "__atomic_thread_fence")) {
    if (is_const_expr(args[0]) && eval(args[0]) != 5)
      return void_node(start);
    return new_node(ND_FENCE, start);
  }
  if (equal(start, "__atomic_signal_fence"))
    return void_node(start);

  // Objects of up to 8 bytes are lock-free.
  assert(equal(start, "__atomic_always_lock_free") ||
         equal(start, "__atomic_is_lock_free"));
  return new_num(eval(args[0]) <= 8, start);
}

// __has_builtin(name) in the preprocessor
bool is_known_builtin(char *name) {
  Token tok = {.loc = name, .len = strlen(name)};
  return in_list(name, builtin_names, sizeof(builtin_names) / sizeof(*builtin_names)) ||
         is_gnu_builtin(&tok) || libc_builtin_sig(&tok);
}

//---------- Primary expressions ---------------------------------------------

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | "_Alignof" "(" type-name ")"
//         | "_Alignof" unary
//         | "_Generic" generic-selection
//         | "__builtin_types_compatible_p" "(" type-name, type-name, ")"
//         | "__builtin_va_start" "(" assign ("," assign)? ")"
//         | ("__builtin_va_end" | "__builtin_va_copy") "(" assign ("," assign)? ")"
//         | "__builtin_va_arg" "(" assign "," type-name ")"
//         | "__builtin_offsetof" "(" type-name "," member ("." member | "[" expr "]")* ")"
//         | "true" | "false" | "nullptr"
//         | ident
//         | str
//         | num
static Node *primary(Token **rest, Token *tok) {
  // #embed's bytes, other than as an initializer's elements, are a list
  // of numbers (see embed_init()).
  if (tok->kind == TK_EMBED)
    expand_embed(tok);

  Token *start = tok;

  if (equal(tok, "(") && equal(tok->next, "{")) {
    // This is a GNU statement expresssion.
    Node *node = new_node(ND_STMT_EXPR, tok);
    Node *blk = compound_stmt(&tok, tok->next->next, true);
    node->body = blk->body;
    node->block = blk->block;
    *rest = skip(tok, ")");
    return node;
  }

  if (equal(tok, "(")) {
    Node *node = expr(&tok, tok->next);
    *rest = skip(tok, ")");
    return node;
  }

  if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
    Type *ty = typename(&tok, tok->next->next);
    *rest = skip(tok, ")");

    if (ty->kind == TY_VLA) {
      if (ty->vla_size)
        return new_var_node(ty->vla_size, tok);

      Node *lhs = compute_vla_size(ty, tok);
      Node *rhs = new_var_node(ty->vla_size, tok);
      return new_binary(ND_COMMA, lhs, rhs, tok);
    }

    if (ty->size < 0)
      error_tok(start, "invalid application of 'sizeof' to incomplete type '%s'",
                type_name(ty));
    return new_ulong(ty->size, start);
  }

  if (equal(tok, "sizeof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    if (node->ty->kind == TY_VLA)
      return new_var_node(node->ty->vla_size, tok);
    if (node->ty->size < 0)
      error_tok(tok, "invalid application of 'sizeof' to incomplete type '%s'",
                type_name(node->ty));
    if (node->kind == ND_MEMBER && node->member->is_bitfield)
      error_tok(tok, "'sizeof' applied to a bit-field");
    return new_ulong(node->ty->size, tok);
  }

  if (equal(tok, "_Alignof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
    Type *ty = typename(&tok, tok->next->next);
    *rest = skip(tok, ")");
    return new_ulong(ty->align, tok);
  }

  if (equal(tok, "_Alignof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    return new_ulong(node->ty->align, tok);
  }

  if (equal(tok, "_Generic"))
    return generic_selection(rest, tok->next);

  if (equal(tok, "__builtin_types_compatible_p")) {
    tok = skip(tok->next, "(");
    Type *t1 = typename(&tok, tok);
    tok = skip(tok, ",");
    Type *t2 = typename(&tok, tok);
    *rest = skip(tok, ")");
    // As with gcc, the types' own qualifiers don't count.
    return new_num(is_compatible(unqual(t1), unqual(t2)), start);
  }

  if (equal(tok, "__builtin_va_start") || equal(tok, "__builtin_va_end") ||
      equal(tok, "__builtin_va_copy") || equal(tok, "__builtin_va_arg"))
    return va_builtin(rest, tok);

  if (equal(tok, "__builtin_shufflevector") || equal(tok, "__builtin_convertvector"))
    return vector_builtin(rest, tok);

  // [GNU] __builtin_complex(re, im), which <complex.h>'s CMPLX() is: a
  // complex number of two floating-point numbers of the same type
  if (equal(tok, "__builtin_complex")) {
    tok = skip(tok->next, "(");
    Node *node = new_node(ND_COMPLEX, start);
    node->lhs = assign(&tok, tok);
    tok = skip(tok, ",");
    node->rhs = assign(&tok, tok);
    *rest = skip(tok, ")");
    add_type(node->lhs);
    add_type(node->rhs);
    Type *ty = node->lhs->ty;
    if (!is_flonum(ty) || ty->kind != node->rhs->ty->kind)
      error_tok(start, "__builtin_complex needs two floating-point numbers of the same type");
    node->ty = complex_type(unqual(ty));
    return node;
  }

  // [GNU] __builtin_offsetof(T, a.b[i]) is `(unsigned long)&((T *)0)->a.b[i]`,
  // as <stddef.h>'s offsetof.
  if (equal(tok, "__builtin_offsetof")) {
    tok = skip(tok->next, "(");
    Type *ty = typename(&tok, tok);
    tok = skip(tok, ",");
    Node *node = new_unary(ND_DEREF, new_cast(new_num(0, start), pointer_to(ty)), start);
    node = struct_ref(node, tok);
    tok = tok->next;
    for (;;) {
      if (equal(tok, ".")) {
        node = struct_ref(node, tok->next);
        tok = tok->next->next;
      } else if (equal(tok, "[")) {
        Token *bracket = tok;
        Node *idx = expr(&tok, tok->next);
        tok = skip(tok, "]");
        node = new_unary(ND_DEREF, new_add(node, idx, bracket), bracket);
      } else {
        break;
      }
    }
    *rest = skip(tok, ")");
    return new_cast(new_unary(ND_ADDR, node, start), ty_ulong);
  }

  // A program's own declaration of the name wins.
  if (tok->kind == TK_IDENT && tok->loc[0] == '_' && equal(tok->next, "(") &&
      is_gnu_builtin(tok) && !find_var(tok))
    return gnu_builtin(rest, tok);

  // __builtin_memcpy and the rest of libc_builtins[]: the function itself,
  // which postfix() then calls.
  if (tok->kind == TK_IDENT && tok->loc[0] == '_' && equal(tok->next, "(") &&
      !find_var(tok)) {
    Obj *fn = libc_builtin(tok);
    if (fn) {
      *rest = tok->next;
      return new_var_node(fn, tok);
    }
  }

  // C23's unreachable() in <stddef.h>
  if (equal(tok, "__builtin_unreachable")) {
    tok = skip(tok->next, "(");
    *rest = skip(tok, ")");
    Node *node = new_node(ND_UNREACHABLE, start);
    node->ty = ty_void;
    return node;
  }

  if (equal(tok, "__builtin_compare_and_swap")) {
    Node *node = new_node(ND_CAS, tok);
    tok = skip(tok->next, "(");
    node->cas_addr = assign(&tok, tok);
    tok = skip(tok, ",");
    node->cas_old = assign(&tok, tok);
    tok = skip(tok, ",");
    node->cas_new = assign(&tok, tok);
    *rest = skip(tok, ")");
    return node;
  }

  if (equal(tok, "__builtin_atomic_exchange")) {
    Node *node = new_node(ND_EXCH, tok);
    tok = skip(tok->next, "(");
    node->lhs = assign(&tok, tok);
    tok = skip(tok, ",");
    node->rhs = assign(&tok, tok);
    *rest = skip(tok, ")");
    return node;
  }

  // C23 constants: true and false have type bool, and nullptr is a null
  // pointer (mucc gives it type void *). Before C23 they're names.
  if (tok->kind == TK_KEYWORD && (equal(tok, "true") || equal(tok, "false"))) {
    Node *node = new_num(equal(tok, "true"), tok);
    node->ty = ty_bool;
    *rest = tok->next;
    return node;
  }

  if (tok->kind == TK_KEYWORD && equal(tok, "nullptr")) {
    *rest = tok->next;
    return new_cast(new_num(0, tok), pointer_to(ty_void));
  }

  if (tok->kind == TK_IDENT) {
    // Variable or enum constant
    VarScope *sc = find_var(tok);
    *rest = tok->next;

    // For "static inline" function
    if (sc && sc->var && sc->var->is_function) {
      if (current_fn)
        strarray_push(&current_fn->refs, sc->var->name);
      else
        sc->var->is_root = true;
    }

    if (sc) {
      if (sc->var) {
        sc->var->is_used = true;
        return new_var_node(sc->var, tok);
      }
      if (sc->enum_ty) {
        Node *node = new_num(sc->enum_val, tok);
        node->ty = sc->enum_ty;
        return node;
      }
    }

    char *name = get_ident(tok);
    if (equal(tok->next, "("))
      error_tok(tok, "call to undeclared function '%s' (missing #include?)", name);
    if (tok->next->kind == TK_IDENT && !tok->next->at_bol)
      error_tok(tok, "unknown type name '%s'", name);
    error_tok(tok, "undeclared identifier '%s'", name);
  }

  if (tok->kind == TK_STR) {
    Obj *var = new_string_literal(tok->str, tok->ty);
    *rest = tok->next;
    return new_var_node(var, tok);
  }

  if (tok->kind == TK_NUM) {
    Node *node;
    if (is_flonum(tok->ty) || is_complex(tok->ty)) { // an imaginary constant: fval i
      node = new_node(ND_NUM, tok);
      node->fval = tok->fval;
    } else {
      node = new_num(tok->val, tok);
    }

    node->ty = tok->ty;
    *rest = tok->next;
    return node;
  }

  error_tok(tok, "expected an expression");
}

//---------- Complex numbers -------------------------------------------------
//
// A complex number is a struct of its real and imaginary parts (see
// complex_type()), so it is stored, copied and passed as one. add_type()
// types arithmetic on it, which is otherwise left as it is while a
// function is parsed, so that `z += w` and `z++` are made as for any
// other number. Then lower_complex() rewrites it into
// arithmetic on the parts of temporaries. A global's initializer is
// computed by eval_complex() instead. Multiplying two complex numbers
// and dividing by one call helpers that define_complex_helpers() adds to
// the program, as gcc calls libgcc's.

// The real (i = 0) or imaginary (1) part of complex variable `var`
static Node *part_of(Obj *var, int i, Token *tok) {
  Node *node = new_unary(ND_MEMBER, new_var_node(var, tok), tok);
  node->member = i ? var->ty->members->next : var->ty->members;
  return node;
}

// Adds `expr` to the end of comma expression `*seq`.
static void append(Node **seq, Node *expr, Token *tok) {
  *seq = *seq ? new_comma(*seq, expr, tok) : expr;
}

// A temporary of type `ty`, set to `val` in `*seq`
static Obj *temp_of(Node **seq, Type *ty, Node *val, Token *tok) {
  Obj *var = new_lvar("", ty);
  append(seq, new_binary(ND_ASSIGN, new_var_node(var, tok), val, tok), tok);
  return var;
}

// `*seq`, then a new complex number of type `ty` made of `re` and `im`
static Node *make_complex(Node *seq, Node *re, Node *im, Type *ty, Token *tok) {
  Obj *var = new_lvar("", ty);
  append(&seq, new_binary(ND_ASSIGN, part_of(var, 0, tok), re, tok), tok);
  append(&seq, new_binary(ND_ASSIGN, part_of(var, 1, tok), im, tok), tok);
  append(&seq, new_var_node(var, tok), tok);
  return seq;
}

// Complex number `node` (already lowered) as complex type `ty`
static Node *convert_complex(Node *node, Type *ty) {
  Token *tok = node->tok;
  Type *part = complex_part(ty);
  if (complex_part(node->ty)->kind == part->kind)
    return node;
  Node *seq = NULL;
  Obj *z = temp_of(&seq, node->ty, node, tok);
  return make_complex(seq, new_cast(part_of(z, 0, tok), part),
                      new_cast(part_of(z, 1, tok), part), ty, tok);
}

// An operand of an arithmetic operator, in a temporary: a complex number
// or a real one
typedef struct {
  Obj *var;
  bool is_complex;
} ComplexOperand;

static ComplexOperand complex_operand(Node **seq, Node *node, Type *ty, Token *tok) {
  if (is_complex(node->ty))
    return (ComplexOperand){temp_of(seq, ty, convert_complex(node, ty), tok), true};
  return (ComplexOperand){temp_of(seq, complex_part(ty), node, tok), false};
}

static Node *re_of(ComplexOperand *x, Token *tok) {
  return x->is_complex ? part_of(x->var, 0, tok) : new_var_node(x->var, tok);
}

static Node *im_of(ComplexOperand *x, Type *part, Token *tok) {
  return x->is_complex ? part_of(x->var, 1, tok) : new_flonum(0, part, tok);
}

// The helpers that multiply and divide complex numbers, ported from
// libgcc's __mulsc3, __divsc3 and the others (in libgcc2.c, of GCC 12
// and later), so that infinities and NaNs come out as with gcc, as C's
// Annex G asks. They are C, written for one part type with `$` for the
// type, `@` for the suffix of its builtins (as in __builtin_fabsf) and
// `#` for libgcc's letter for it. They call no library function.
static char *complex_mul_text =
  "static _Complex $ __mucc_mul#c3($ a, $ b, $ c, $ d) {\n"
  "  $ ac = a * c, bd = b * d, ad = a * d, bc = b * c;\n"
  "  $ x = ac - bd, y = ad + bc;\n"
  "  if (__builtin_isnan(x) && __builtin_isnan(y)) {\n"
  "    _Bool recalc = 0;\n"
  "    if (__builtin_isinf(a) || __builtin_isinf(b)) {\n"
  "      a = __builtin_copysign@(__builtin_isinf(a) ? 1 : 0, a);\n"
  "      b = __builtin_copysign@(__builtin_isinf(b) ? 1 : 0, b);\n"
  "      if (__builtin_isnan(c)) c = __builtin_copysign@(0, c);\n"
  "      if (__builtin_isnan(d)) d = __builtin_copysign@(0, d);\n"
  "      recalc = 1;\n"
  "    }\n"
  "    if (__builtin_isinf(c) || __builtin_isinf(d)) {\n"
  "      c = __builtin_copysign@(__builtin_isinf(c) ? 1 : 0, c);\n"
  "      d = __builtin_copysign@(__builtin_isinf(d) ? 1 : 0, d);\n"
  "      if (__builtin_isnan(a)) a = __builtin_copysign@(0, a);\n"
  "      if (__builtin_isnan(b)) b = __builtin_copysign@(0, b);\n"
  "      recalc = 1;\n"
  "    }\n"
  "    if (!recalc && (__builtin_isinf(ac) || __builtin_isinf(bd) ||\n"
  "                    __builtin_isinf(ad) || __builtin_isinf(bc))) {\n"
  "      if (__builtin_isnan(a)) a = __builtin_copysign@(0, a);\n"
  "      if (__builtin_isnan(b)) b = __builtin_copysign@(0, b);\n"
  "      if (__builtin_isnan(c)) c = __builtin_copysign@(0, c);\n"
  "      if (__builtin_isnan(d)) d = __builtin_copysign@(0, d);\n"
  "      recalc = 1;\n"
  "    }\n"
  "    if (recalc) {\n"
  "      x = __builtin_inf@() * (a * c - b * d);\n"
  "      y = __builtin_inf@() * (a * d + b * c);\n"
  "    }\n"
  "  }\n"
  "  return __builtin_complex(x, y);\n"
  "}\n";

// Division of floats, which is done in double precision
static char *complex_divs_text =
  "static _Complex float __mucc_divsc3(float a, float b, float c, float d) {\n"
  "  double aa = a, bb = b, cc = c, dd = d;\n"
  "  double denom = cc * cc + dd * dd;\n"
  "  float x = (aa * cc + bb * dd) / denom;\n"
  "  float y = (bb * cc - aa * dd) / denom;\n";

// Division of doubles and long doubles, by Smith's algorithm (the larger
// of c and d divides the smaller, which keeps the values in range), with
// scaling where they would overflow or underflow. RBIG, RMIN and RMIN2
// are set before this.
static char *complex_div_text =
  "  $ RMINSCAL = 1 / RMIN2, RMAX2 = RBIG * RMIN2, denom, ratio, x, y;\n"
  "  if (__builtin_fabs@(c) < __builtin_fabs@(d)) {\n"
  "    if (__builtin_fabs@(d) >= RBIG) {\n"
  "      a = a / 2; b = b / 2; c = c / 2; d = d / 2;\n"
  "    }\n"
  "    if (__builtin_fabs@(d) < RMIN2 ||\n"
  "        (__builtin_fabs@(a) < RMIN && __builtin_fabs@(b) < RMAX2 && __builtin_fabs@(d) < RMAX2) ||\n"
  "        (__builtin_fabs@(b) < RMIN && __builtin_fabs@(a) < RMAX2 && __builtin_fabs@(d) < RMAX2)) {\n"
  "      a = a * RMINSCAL; b = b * RMINSCAL; c = c * RMINSCAL; d = d * RMINSCAL;\n"
  "    }\n"
  "    ratio = c / d;\n"
  "    denom = c * ratio + d;\n"
  "    if (__builtin_fabs@(ratio) > RMIN) {\n"
  "      x = (a * ratio + b) / denom;\n"
  "      y = (b * ratio - a) / denom;\n"
  "    } else {\n"
  "      x = (c * (a / d) + b) / denom;\n"
  "      y = (c * (b / d) - a) / denom;\n"
  "    }\n"
  "  } else {\n"
  "    if (__builtin_fabs@(c) >= RBIG) {\n"
  "      a = a / 2; b = b / 2; c = c / 2; d = d / 2;\n"
  "    }\n"
  "    if (__builtin_fabs@(c) < RMIN2 ||\n"
  "        (__builtin_fabs@(a) < RMIN && __builtin_fabs@(b) < RMAX2 && __builtin_fabs@(c) < RMAX2) ||\n"
  "        (__builtin_fabs@(b) < RMIN && __builtin_fabs@(a) < RMAX2 && __builtin_fabs@(c) < RMAX2)) {\n"
  "      a = a * RMINSCAL; b = b * RMINSCAL; c = c * RMINSCAL; d = d * RMINSCAL;\n"
  "    }\n"
  "    ratio = d / c;\n"
  "    denom = d * ratio + c;\n"
  "    if (__builtin_fabs@(ratio) > RMIN) {\n"
  "      x = (b * ratio + a) / denom;\n"
  "      y = (b - a * ratio) / denom;\n"
  "    } else {\n"
  "      x = (d * (b / c) + a) / denom;\n"
  "      y = (b - d * (a / c)) / denom;\n"
  "    }\n"
  "  }\n";

// The end of every division: infinities and zeros that came out as NaNs
// are recovered. The only cases are nonzero / zero, infinite / finite
// and finite / infinite.
static char *complex_div_end_text =
  "  if (__builtin_isnan(x) && __builtin_isnan(y)) {\n"
  "    if (c == 0.0 && d == 0.0 && (!__builtin_isnan(a) || !__builtin_isnan(b))) {\n"
  "      x = __builtin_copysign@(__builtin_inf@(), c) * a;\n"
  "      y = __builtin_copysign@(__builtin_inf@(), c) * b;\n"
  "    } else if ((__builtin_isinf(a) || __builtin_isinf(b)) &&\n"
  "               __builtin_isfinite(c) && __builtin_isfinite(d)) {\n"
  "      a = __builtin_copysign@(__builtin_isinf(a) ? 1 : 0, a);\n"
  "      b = __builtin_copysign@(__builtin_isinf(b) ? 1 : 0, b);\n"
  "      x = __builtin_inf@() * (a * c + b * d);\n"
  "      y = __builtin_inf@() * (b * c - a * d);\n"
  "    } else if ((__builtin_isinf(c) || __builtin_isinf(d)) &&\n"
  "               __builtin_isfinite(a) && __builtin_isfinite(b)) {\n"
  "      c = __builtin_copysign@(__builtin_isinf(c) ? 1 : 0, c);\n"
  "      d = __builtin_copysign@(__builtin_isinf(d) ? 1 : 0, d);\n"
  "      x = 0.0 * (a * c + b * d);\n"
  "      y = 0.0 * (b * c - a * d);\n"
  "    }\n"
  "  }\n"
  "  return __builtin_complex(x, y);\n"
  "}\n";

// The helpers' functions, by [is division][part type: float, double,
// long double], once one is called, and the first token to call it
static Obj *complex_helpers[2][3];
static Token *complex_helper_uses[2][3];

static int part_index(Type *part) {
  return part->kind == TY_FLOAT ? 0 : part->kind == TY_DOUBLE ? 1 : 2;
}

// The helper that multiplies or divides complex numbers of `part`. It is
// declared here, in no scope, and defined by define_complex_helpers().
static Obj *complex_helper(bool is_div, Type *part, Token *tok) {
  int i = part_index(part);
  Obj **fn = &complex_helpers[is_div][i];
  if (*fn)
    return *fn;

  Type *ty = func_type(complex_type(part));
  Type head = {};
  Type *cur = &head;
  for (int j = 0; j < 4; j++)
    cur = cur->next = copy_type(part);
  ty->params = head.next;

  *fn = arena_alloc(sizeof(Obj));
  (*fn)->name = format("__mucc_%s%cc3", is_div ? "div" : "mul", "sdx"[i]);
  (*fn)->ty = ty;
  (*fn)->align = ty->align;
  (*fn)->is_function = true;
  (*fn)->is_static = true;
  (*fn)->is_used = true;
  (*fn)->next = globals;
  globals = *fn;
  complex_helper_uses[is_div][i] = tok;
  return *fn;
}

// `text` with `$`, `@` and `#` filled in for part type i
static char *fill_complex_text(char *text, int i) {
  char *types[] = {"float", "double", "long double"};
  char *suffixes[] = {"f", "", "l"};
  char *buf;
  size_t len;
  FILE *out = open_memstream(&buf, &len);
  for (char *p = text; *p; p++) {
    if (*p == '$')
      fputs(types[i], out);
    else if (*p == '@')
      fputs(suffixes[i], out);
    else if (*p == '#')
      fputc("sdx"[i], out);
    else
      fputc(*p, out);
  }
  fclose(out);
  return buf;
}

// Defines helper function `fn` from C `text`, in a scope of its own, so
// that the program's names can't change what it means. Its tokens are put
// at the line of `use`, its first call, for the debug info.
static void define_helper(Obj *fn, char *text, Token *use) {
  Token *tok = tokenize(new_file("<built-in>", use->file->file_no, text));
  for (Token *t = tok; t; t = t->next)
    t->line_no = use->line_no;
  convert_pp_tokens(tok);

  Scope *saved = scope;
  scope = arena_alloc(sizeof(Scope));
  push_scope(fn->name)->var = fn;
  while (tok->kind != TK_EOF)
    tok = top_level_item(tok);
  scope = saved;
}

// Defines the complex helpers that were called (see define_helper()).
static void define_complex_helpers(void) {
  // RBIG, RMIN and RMIN2: half the largest number, the smallest normal
  // one and the epsilon of double and long double
  char *limits[] = {
    NULL,
    "  double RBIG = 0x1.fffffffffffffp1023 / 2, RMIN = 0x1p-1022, RMIN2 = 0x1p-52;\n",
    "  long double RBIG = 0x1.fffffffffffffffep16383L / 2, RMIN = 0x1p-16382L,"
    " RMIN2 = 0x1p-63L;\n",
  };

  for (int is_div = 0; is_div < 2; is_div++) {
    for (int i = 0; i < 3; i++) {
      Obj *fn = complex_helpers[is_div][i];
      if (!fn)
        continue;
      char *text;
      if (!is_div)
        text = complex_mul_text;
      else if (i == 0)
        text = format("%s%s", complex_divs_text, complex_div_end_text);
      else
        text = format("static _Complex $ __mucc_div#c3($ a, $ b, $ c, $ d) {\n%s%s%s",
                      limits[i], complex_div_text, complex_div_end_text);

      define_helper(fn, fill_complex_text(text, i), complex_helper_uses[is_div][i]);
    }
  }
}

static Node *lower_arith(Node *node) {
  Token *tok = node->tok;
  Type *ty = node->ty, *part = complex_part(ty);
  Node *seq = NULL;
  ComplexOperand a = complex_operand(&seq, node->lhs, ty, tok);
  ComplexOperand b = complex_operand(&seq, node->rhs, ty, tok);
  NodeKind k = node->kind;
  Node *re, *im;

  if (k == ND_ADD || k == ND_SUB) {
    re = new_binary(k, re_of(&a, tok), re_of(&b, tok), tok);
    if (a.is_complex && b.is_complex)
      im = new_binary(k, im_of(&a, part, tok), im_of(&b, part, tok), tok);
    else if (a.is_complex)
      im = im_of(&a, part, tok);
    else if (k == ND_ADD)
      im = im_of(&b, part, tok);
    else
      im = new_unary(ND_NEG, im_of(&b, part, tok), tok);
    return make_complex(seq, re, im, ty, tok);
  }

  // A complex number times or divided by a real one: each part
  if (!b.is_complex || (k == ND_MUL && !a.is_complex)) {
    ComplexOperand *z = a.is_complex ? &a : &b, *x = a.is_complex ? &b : &a;
    re = new_binary(k, re_of(&a, tok), re_of(&b, tok), tok);
    if (z == &a)
      im = new_binary(k, im_of(&a, part, tok), re_of(x, tok), tok);
    else
      im = new_binary(k, re_of(x, tok), im_of(&b, part, tok), tok);
    return make_complex(seq, re, im, ty, tok);
  }

  // A complex number times a complex number, or anything divided by
  // one: a call to a helper, as gcc calls libgcc's, with 0 as a real
  // dividend's imaginary part.
  Obj *fn = complex_helper(k == ND_DIV, part, tok);
  Node *call = new_unary(ND_FUNCALL, new_var_node(fn, tok), tok);
  call->func_ty = fn->ty;
  call->ty = ty;
  call->ret_buffer = new_lvar("", ty);
  call->args = re_of(&a, tok);
  call->args->next = im_of(&a, part, tok);
  call->args->next->next = re_of(&b, tok);
  call->args->next->next->next = im_of(&b, part, tok);
  add_type(call->lhs);
  for (Node *arg = call->args; arg; arg = arg->next)
    add_type(arg);
  append(&seq, call, tok);
  return seq;
}

// `node` without complex arithmetic, or NULL if it has none
static Node *lowered(Node *node) {
  Token *tok = node->tok;
  Type *ty = node->ty;
  Node *seq = NULL;

  switch (node->kind) {
  case ND_NUM: // an imaginary constant
    if (!is_complex(ty))
      return NULL;
    return make_complex(NULL, new_flonum(0, complex_part(ty), tok),
                        new_flonum(*node->fval, complex_part(ty), tok), ty, tok);
  case ND_COMPLEX:
    return make_complex(NULL, new_cast(node->lhs, complex_part(ty)),
                        new_cast(node->rhs, complex_part(ty)), ty, tok);
  case ND_CAST: {
    Type *from = node->lhs->ty;
    if (is_complex(ty) && is_complex(from))
      return convert_complex(node->lhs, ty);
    if (is_complex(ty))
      return make_complex(NULL, new_cast(node->lhs, complex_part(ty)),
                          new_cast(new_num(0, tok), complex_part(ty)), ty, tok);
    // To a real number, its real part. (To bool, cgen.c tests both
    // parts.)
    if (!is_complex(from) || ty->kind == TY_BOOL || ty->kind == TY_VOID)
      return NULL;
    Obj *z = temp_of(&seq, from, node->lhs, tok);
    append(&seq, new_cast(part_of(z, 0, tok), ty), tok);
    return seq;
  }
  case ND_NEG:
  case ND_BITNOT: { // ~z is z's conjugate (GNU)
    if (!is_complex(ty))
      return NULL;
    Obj *z = temp_of(&seq, ty, node->lhs, tok);
    Node *re = part_of(z, 0, tok);
    if (node->kind == ND_NEG)
      re = new_unary(ND_NEG, re, tok);
    return make_complex(seq, re, new_unary(ND_NEG, part_of(z, 1, tok), tok), ty, tok);
  }
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
    return is_complex(ty) ? lower_arith(node) : NULL;
  case ND_EQ:
  case ND_NE: {
    // Both are the same complex type here (see add_type()).
    if (!is_complex(node->lhs->ty))
      return NULL;
    Obj *z = temp_of(&seq, node->lhs->ty, node->lhs, tok);
    Obj *w = temp_of(&seq, node->lhs->ty, node->rhs, tok);
    NodeKind join = node->kind == ND_EQ ? ND_LOGAND : ND_LOGOR;
    append(&seq, new_binary(join,
                            new_binary(node->kind, part_of(z, 0, tok), part_of(w, 0, tok), tok),
                            new_binary(node->kind, part_of(z, 1, tok), part_of(w, 1, tok), tok),
                            tok), tok);
    return seq;
  }
  }
  return NULL;
}

// `node` with the complex arithmetic in it and what's under it rewritten,
// children first: node, changed in place, or what replaces it.
static Node *lower_complex(Node *node) {
  if (!node)
    return NULL;

  node->lhs = lower_complex(node->lhs);
  node->rhs = lower_complex(node->rhs);
  if (has_stmt_fields(node->kind)) {
    node->cond = lower_complex(node->cond);
    node->then = lower_complex(node->then);
    node->els = lower_complex(node->els);
    node->init = lower_complex(node->init);
    node->inc = lower_complex(node->inc);
    for (Node **p = &node->body; *p; p = &(*p)->next)
      *p = lower_complex(*p);
  }
  if (node->kind == ND_CAS || node->kind == ND_OVERFLOW) {
    node->cas_addr = lower_complex(node->cas_addr);
    node->cas_old = lower_complex(node->cas_old);
    node->cas_new = lower_complex(node->cas_new);
  }
  if (node->kind == ND_FUNCALL)
    for (Node **p = &node->args; *p; p = &(*p)->next)
      *p = lower_complex(*p);

  if (!node->ty)
    return node;
  Node *new = lowered(node);
  if (!new)
    return node;
  add_type(new);
  new->next = node->next;
  return new;
}

// The value of constant `node`, a complex or real number, in `*re` and
// `*im`, for a global's initializer
static void eval_complex(Node *node, long double *re, long double *im) {
  add_type(node);
  if (!is_complex(node->ty)) {
    if (is_flonum(node->ty))
      *re = eval_double(node);
    else if (node->ty->is_unsigned)
      *re = (uint64_t)eval(node);
    else
      *re = eval(node);
    *im = 0;
    return;
  }

  long double a, b, c, d;
  switch (node->kind) {
  case ND_NUM:
    *re = 0;
    *im = *node->fval;
    return;
  case ND_COMPLEX:
    *re = eval_double(node->lhs);
    *im = eval_double(node->rhs);
    return;
  case ND_CAST:
    eval_complex(node->lhs, re, im);
    return;
  case ND_NEG:
  case ND_BITNOT:
    eval_complex(node->lhs, &a, &b);
    *re = node->kind == ND_NEG ? -a : a;
    *im = -b;
    return;
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
    eval_complex(node->lhs, &a, &b);
    eval_complex(node->rhs, &c, &d);
    if (node->kind == ND_ADD) {
      *re = a + c;
      *im = b + d;
    } else if (node->kind == ND_SUB) {
      *re = a - c;
      *im = b - d;
    } else if (node->kind == ND_MUL) {
      *re = a * c - b * d;
      *im = a * d + b * c;
    } else {
      long double denom = c * c + d * d;
      *re = (a * c + b * d) / denom;
      *im = (b * c - a * d) / denom;
    }
    return;
  }
  error_tok(node->tok, "not a compile-time constant");
}

//---------- Warnings --------------------------------------------------------

// Warnings, each with a name that -W<name> and -Wno-<name> turn on and
// off (see warning_state() in token.c). Two are checked once a function
// is parsed: unused local variables, and non-void functions that can end
// without a return. The others are checked where the parser meets the
// code: here are the functions it calls. None is given in system
// headers or with -w.

// C library functions that never return, when a system header declares
// them. glibc marks them with __attribute__((noreturn)), which it hides
// from compilers other than gcc, so they're listed.
static bool is_libc_noreturn(char *name) {
  static char *names[] = {
    "abort", "exit", "_exit", "_Exit", "quick_exit", "longjmp", "_longjmp",
    "siglongjmp", "pthread_exit", "err", "errx", "verr", "verrx",
    "__assert_fail", "__assert_perror_fail", "__stack_chk_fail",
  };
  for (int i = 0; i < sizeof(names) / sizeof(*names); i++)
    if (!strcmp(name, names[i]))
      return true;
  return false;
}

// Can evaluating `node` change anything, or is only its value wanted?
static bool has_side_effects(Node *node) {
  if (!node)
    return false;
  switch (node->kind) {
  case ND_ASSIGN: // also ++, -- and op=
  case ND_FUNCALL:
  case ND_ASM:
  case ND_STMT_EXPR:
  case ND_CAS:
  case ND_EXCH:
  case ND_MEMZERO:
  case ND_VA_ARG:
  case ND_FENCE:
  case ND_UNREACHABLE:
  case ND_TRAP:
  case ND_OVERFLOW:
  case ND_VLA_FREE:
    return true;
  case ND_VAR:
  case ND_DEREF:
  case ND_MEMBER:
    // Reading a volatile object is something a program can see.
    if (node->ty && node->ty->is_volatile)
      return true;
    break;
  }
  if (has_side_effects(node->lhs) || has_side_effects(node->rhs))
    return true;
  return has_stmt_fields(node->kind) &&
         (has_side_effects(node->cond) || has_side_effects(node->then) ||
          has_side_effects(node->els));
}

// -Wunused-value: `x == 1;`, a statement that computes a value and drops
// it without doing anything. A cast to void says that's meant. `stmt` is
// a statement in a block, but not the last one of a statement expression,
// whose value is the block's.
static void warn_unused_value(Node *stmt) {
  while (stmt->kind == ND_LABEL || stmt->kind == ND_CASE)
    stmt = stmt->lhs;
  if (stmt->kind != ND_EXPR_STMT)
    return;
  Node *expr = stmt->lhs;
  Token *start = stmt->tok;
  add_type(expr);
  switch (expr->kind) {
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
  case ND_BITAND: case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR:
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
  case ND_NOT: case ND_BITNOT: case ND_NEG:
  case ND_VAR: case ND_NUM: case ND_MEMBER:
    if (!has_side_effects(expr))
      warn_opt("unused-value", start, "statement with no effect");
  }
}

// -Wparentheses: `if (x = 0)`, most likely meant as `if (x == 0)`. Extra
// parentheses, `if ((x = f()))`, say it's meant. `start` is the
// condition's first token.
static void warn_assign_in_condition(Node *cond, Token *start) {
  if (cond->kind == ND_ASSIGN && equal(cond->tok, "=") && !equal(start, "("))
    warn_opt("parentheses", cond->tok,
             "suggest parentheses around assignment used as truth value");
}

// -Waddress: a condition that is a function or an array is always true,
// since only its address is tested.
static void warn_address_condition(Node *cond) {
  if (cond->kind != ND_VAR)
    return;
  Obj *var = cond->var;
  if (var->ty->kind == TY_FUNC)
    warn_opt("address", cond->tok,
             "the address of '%s' will always evaluate as 'true'", var->name);
  else if (var->ty->kind == TY_ARRAY && !var->is_string)
    warn_opt("address", cond->tok,
             "the address of '%s' will always evaluate as 'true'", var->name);
}

// -Waddress: `s == "abc"` compares addresses, not the strings.
static void warn_string_compare(Node *lhs, Node *rhs, Token *tok) {
  if ((lhs->kind == ND_VAR && lhs->var->is_string) ||
      (rhs->kind == ND_VAR && rhs->var->is_string))
    warn_opt("address", tok, "comparison with string literal results in "
             "unspecified behavior");
}

// -Wreturn-local-addr: `return &x;` or `return buf;` for a local x or
// array buf, which no longer exists once the function returns.
static void warn_return_local(Node *exp) {
  Node *node = exp;
  while (node->kind == ND_CAST)
    node = node->lhs;
  // Through a[i], the object is a only if a is an array, not a pointer.
  bool needs_array = true;
  if (node->kind == ND_ADDR) {
    node = node->lhs;
    needs_array = false;
    // &x.member, &a[i]
    while (node->kind == ND_MEMBER ||
           (node->kind == ND_DEREF && node->lhs->kind == ND_ADD)) {
      if (node->kind == ND_DEREF)
        needs_array = true;
      node = (node->kind == ND_MEMBER) ? node->lhs : node->lhs->lhs;
      while (node->kind == ND_CAST)
        node = node->lhs;
    }
  } else if (node->kind == ND_ADD) {
    // &a[2], which the parser has made a + 2
    node = node->lhs;
    while (node->kind == ND_CAST)
      node = node->lhs;
  }
  if (node->kind == ND_VAR && node->var->is_local && node->var->tok &&
      (!needs_array || node->var->ty->kind == TY_ARRAY))
    warn_opt("return-local-addr", exp->tok,
             "function returns address of local variable '%s'", node->var->name);
}

// -Wdiv-by-zero: a division by an integer constant 0. (By 0.0 is how a
// program asks for an infinity or a NaN.)
static void warn_div_by_zero(Node *lhs, Node *rhs, Token *tok) {
  add_type(rhs);
  if (is_integer(rhs->ty) && is_const_expr(rhs) && eval(rhs) == 0)
    warn_opt("div-by-zero", tok, "division by zero");
}

// -Wshift-count-overflow and -Wshift-count-negative: a constant shift
// count that is negative, or at least the width of the shifted type.
static void warn_shift_count(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);
  if (!is_integer(lhs->ty) || !is_integer(rhs->ty) || !is_const_expr(rhs))
    return;
  int64_t n = eval(rhs);
  int width = MAX(lhs->ty->size, 4) * 8; // after integer promotion
  char *dir = equal(tok, "<<") ? "left" : "right";
  if (n < 0)
    warn_opt("shift-count-negative", tok, "%s shift count is negative", dir);
  else if (n >= width)
    warn_opt("shift-count-overflow", tok, "%s shift count >= width of type", dir);
}

// -Wformat: a printf or scanf format string that doesn't match the
// arguments after it. Only a literal format is checked.
static struct {
  char *name;
  int fmt; // which argument is the format, from 0
  bool is_scanf;
} format_funcs[] = {
  {"printf", 0}, {"fprintf", 1}, {"dprintf", 1}, {"sprintf", 1},
  {"snprintf", 2}, {"asprintf", 1},
  {"scanf", 0, true}, {"fscanf", 1, true}, {"sscanf", 1, true},
  // glibc's names for them, which its <stdio.h> uses for C99 and C23
  {"__isoc99_scanf", 0, true}, {"__isoc99_fscanf", 1, true},
  {"__isoc99_sscanf", 1, true}, {"__isoc23_scanf", 0, true},
  {"__isoc23_fscanf", 1, true}, {"__isoc23_sscanf", 1, true},
};

// The type a conversion takes, as a type, for checking and messages.
// For printf's integers, `ty` is only the size class: int for no length
// (char and short become int), and the sign isn't checked, as with gcc.
static Type *format_arg_type(char conv, char *len, bool is_scanf) {
  Type *ty;
  if (conv == 'c')
    return is_scanf ? pointer_to(!strcmp(len, "l") ? ty_int : ty_char) : ty_int;
  if (strchr("diouxXn", conv)) {
    if (!strcmp(len, "l") || !strcmp(len, "j") || !strcmp(len, "z") || !strcmp(len, "t"))
      ty = ty_long;
    else if (!strcmp(len, "ll") || !strcmp(len, "q"))
      ty = ty_llong;
    else if (is_scanf && !strcmp(len, "h"))
      ty = ty_short;
    else if (is_scanf && !strcmp(len, "hh"))
      ty = ty_char;
    else
      ty = ty_int;
    return (conv == 'n' || is_scanf) ? pointer_to(ty) : ty;
  }
  if (strchr("fFeEgGaA", conv)) {
    ty = !strcmp(len, "L") ? ty_ldouble
         : (is_scanf && strcmp(len, "l")) ? ty_float : ty_double;
    return is_scanf ? pointer_to(ty) : ty;
  }
  if (conv == 's' || conv == '[')
    return pointer_to(!strcmp(len, "l") ? ty_int : ty_char);
  if (conv == 'p')
    return pointer_to(is_scanf ? pointer_to(ty_void) : ty_void);
  return NULL;
}

// Does argument type `arg` fit what the conversion wants, `want`?
static bool format_arg_ok(Type *want, Type *arg, char conv, char *len) {
  // j, z and t are the 64-bit typedefs, which may be long or long long
  bool any64 = len[0] && strchr("jzt", len[0]);
  if (want->kind != TY_PTR) {
    if (is_flonum(want))
      return want->kind == TY_LDOUBLE ? arg->kind == TY_LDOUBLE
                                      : arg->kind == TY_DOUBLE || arg->kind == TY_FLOAT;
    if (!is_integer(arg))
      return false;
    if (want->size == 8)
      return arg->size == 8 && (any64 || arg->is_distinct == want->is_distinct);
    return arg->size <= 4;
  }

  // %p takes any pointer; the others, a pointer to the right type.
  if (!arg->base)
    return arg->kind == TY_FUNC && conv == 'p';
  if (conv == 'p')
    return true;
  Type *w = want->base, *a = arg->base;
  if (w->kind == TY_PTR)
    return a->kind == TY_PTR;
  if (is_flonum(w))
    return a->kind == w->kind;
  if (w->kind == TY_CHAR)
    return a->kind == TY_CHAR;
  return is_integer(a) && a->size == w->size &&
         (w->size != 8 || any64 || a->is_distinct == w->is_distinct);
}

static void warn_format(Obj *fn, Node *args) {
  int idx = -1;
  for (int i = 0; i < sizeof(format_funcs) / sizeof(*format_funcs); i++)
    if (!strcmp(fn->name, format_funcs[i].name))
      idx = i;
  if (idx < 0 || !warning_on("format"))
    return;
  bool is_scanf = format_funcs[idx].is_scanf;

  Node *arg = args;
  int argno = 1;
  for (int i = 0; i < format_funcs[idx].fmt && arg; i++, argno++)
    arg = arg->next;
  if (!arg)
    return;
  Node *fmt_node = arg;
  while (fmt_node->kind == ND_CAST)
    fmt_node = fmt_node->lhs;
  if (fmt_node->kind != ND_VAR || !fmt_node->var->is_string ||
      fmt_node->var->ty->base->size != 1)
    return;
  char *fmt = fmt_node->var->init_data;
  Token *fmt_tok = fmt_node->tok;
  arg = arg->next;
  argno++;

  for (char *p = fmt; *p; p++) {
    if (*p != '%')
      continue;
    char *spec = p++;
    if (*p == '%')
      continue;

    // %1$d: arguments by position, which this doesn't follow
    char *q = p;
    while (isdigit(*q))
      q++;
    if (*q == '$')
      return;

    bool suppress = is_scanf && *p == '*';
    if (suppress)
      p++;
    while (!is_scanf && *p && strchr("-+ #0'", *p))
      p++;

    // Width and precision; a `*` takes an int argument.
    for (int part = 0; part < 2; part++) {
      if (part == 1) {
        if (is_scanf || *p != '.')
          break;
        p++;
      }
      if (*p == '*' && !is_scanf) {
        p++;
        if (!arg) {
          warn_opt("format", fmt_tok, "field width or precision '*' expects a matching 'int' argument");
          return;
        }
        add_type(arg);
        if (!is_integer(arg->ty))
          warn_opt("format", arg->tok, "field width or precision '*' expects argument of "
                   "type 'int', but argument %d has type '%s'", argno, type_name(arg->ty));
        arg = arg->next;
        argno++;
      }
      while (isdigit(*p))
        p++;
    }

    // scanf's `m` (as in %ms) allocates the string, so its argument is a
    // pointer to the pointer. (printf's %m takes no argument.)
    bool alloc = is_scanf && *p == 'm';
    if (alloc)
      p++;

    char len[3] = {0};
    if ((p[0] == 'h' && p[1] == 'h') || (p[0] == 'l' && p[1] == 'l')) {
      len[0] = len[1] = *p;
      p += 2;
    } else if (*p && strchr("hlLqjzt", *p)) {
      len[0] = *p++;
    }

    char conv = *p;
    if (!conv)
      return;
    if (conv == '[')
      // %[...]: skip the set; a ']' first is part of it.
      for (p += (p[1] == ']') + 1; *p && *p != ']'; p++)
        ;
    if (suppress || conv == 'm')
      continue;

    Type *want = format_arg_type(conv, len, is_scanf);
    if (!want)
      continue; // a conversion this doesn't know
    if (alloc)
      want = pointer_to(want);
    int spec_len = (int)(p - spec + (*p != '\0'));
    if (!arg) {
      warn_opt("format", fmt_tok, "format '%.*s' expects a matching '%s' argument",
               spec_len, spec, type_name(want));
      return;
    }
    add_type(arg);
    if (!format_arg_ok(want, arg->ty, conv, len))
      warn_opt("format", arg->tok, "format '%.*s' expects argument of type '%s', but "
               "argument %d has type '%s'", spec_len, spec, type_name(want), argno,
               type_name(arg->ty));
    arg = arg->next;
    argno++;
    if (!*p)
      break;
  }

  if (arg)
    warn_opt("format-extra-args", arg->tok, "too many arguments for format");
}

// -Wswitch: a switch on an enum, with no default, that has no case for
// one of its constants.
static void warn_switch(Node *sw) {
  Type *ty = sw->cond->ty;
  if (ty->kind != TY_ENUM || sw->default_case)
    return;
  bool uns = ty->is_unsigned && ty->size >= 4;
  for (EnumConst *e = ty->enum_consts; e; e = e->next) {
    int64_t v = to_case_type(ty, e->val);
    Node *c = sw->case_next;
    while (c && !(uns ? (uint64_t)c->begin <= (uint64_t)v && (uint64_t)v <= (uint64_t)c->end
                      : c->begin <= v && v <= c->end))
      c = c->case_next;
    if (!c)
      warn_opt("switch", sw->tok, "enumeration value '%.*s' not handled in switch",
               e->name->len, e->name->loc);
  }
}

// -Wunused-function: a static function that is defined and never used,
// in the order they were defined (the list is newest first).
static void warn_unused_functions(Obj *globals) {
  int n = 0;
  for (Obj *fn = globals; fn; fn = fn->next)
    n++;
  Obj **fns = calloc(n, sizeof(Obj *));
  int i = n;
  for (Obj *fn = globals; fn; fn = fn->next)
    fns[--i] = fn;

  for (i = 0; i < n; i++) {
    Obj *fn = fns[i];
    if (!fn->is_function || !fn->is_definition || !fn->is_static || fn->is_inline ||
        fn->is_used || fn->is_kept || fn->is_ctor || fn->is_dtor || !fn->tok)
      continue;
    bool aliased = false;
    for (Obj *var = globals; var; var = var->next)
      aliased |= var->alias_target && !strcmp(var->alias_target, fn->name);
    if (!aliased)
      warn_opt("unused-function", fn->tok, "'%s' defined but not used", fn->name);
  }
  free(fns);
}

// Warns about each local variable that is declared but never named
// again, in declaration order (the list is newest first).
static void warn_unused_locals(Obj *var) {
  if (!var)
    return;
  warn_unused_locals(var->next);

  // Parameters and compiler temporaries have no `tok`.
  if (var->tok && !var->is_used && !in_system_header(var->tok))
    warn_opt("unused-variable", var->tok, "unused variable '%s'", var->name);
}

// Does statement `node` contain a jump to `label`, such as the `break`
// or `continue` of a loop?
static bool has_jump_to(Node *node, char *label) {
  if (!node)
    return false;

  switch (node->kind) {
  case ND_GOTO:
    return node->unique_label == label;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      if (has_jump_to(n, label))
        return true;
    return false;
  case ND_LABEL:
  case ND_CASE:
    return has_jump_to(node->lhs, label);
  case ND_IF:
  case ND_FOR:
  case ND_DO:
  case ND_SWITCH:
    return has_jump_to(node->then, label) || has_jump_to(node->els, label);
  default:
    return false;
  }
}

static bool is_always_true(Node *cond) {
  return !cond || (is_const_expr(cond) && eval(cond) != 0);
}

// Can control run off the end of statement `node`, on to whatever comes
// after it?
static bool falls_through(Node *node) {
  if (!node)
    return true;

  switch (node->kind) {
  case ND_RETURN:
  case ND_GOTO: // also break and continue
  case ND_GOTO_EXPR:
    return false;
  case ND_EXPR_STMT: {
    Node *e = node->lhs;
    if (e->kind == ND_UNREACHABLE || e->kind == ND_TRAP)
      return false;
    return !(e->kind == ND_FUNCALL && e->lhs->kind == ND_VAR &&
             e->lhs->var->is_noreturn);
  }
  case ND_BLOCK: {
    // Code after a return is unreachable, unless it has a label that
    // can be jumped to.
    bool reachable = true;
    for (Node *n = node->body; n; n = n->next) {
      if (n->kind == ND_LABEL || n->kind == ND_CASE)
        reachable = true;
      if (reachable)
        reachable = falls_through(n);
    }
    return reachable;
  }
  case ND_LABEL:
  case ND_CASE:
    return falls_through(node->lhs);
  case ND_IF:
    // A constant condition takes one branch (see stmt()).
    if (is_integer(node->cond->ty) && is_const_expr(node->cond))
      return falls_through(eval(node->cond) ? node->then : node->els);
    return !node->els || falls_through(node->then) || falls_through(node->els);
  case ND_FOR:
    // `for (;;)` and `while (1)` only end with a break.
    if (is_always_true(node->cond))
      return has_jump_to(node->then, node->brk_label);
    return true;
  case ND_DO:
    if (has_jump_to(node->then, node->brk_label))
      return true;
    if (is_always_true(node->cond))
      return false;
    // The condition is reached only if the body finishes or continues.
    return falls_through(node->then) ||
           has_jump_to(node->then, node->cont_label);
  case ND_SWITCH:
    // With no default, it's possible that no case runs.
    return !node->default_case || falls_through(node->then) ||
           has_jump_to(node->then, node->brk_label);
  default:
    return true;
  }
}

// `rbrace` is the `}` that ends the function's body.
static void warn_function(Obj *fn, Token *rbrace) {
  if (error_count)
    return;

  warn_unused_locals(fn->locals);

  // main() returns 0 if it runs off the end (C99).
  if (fn->ty->return_ty->kind != TY_VOID && strcmp(fn->name, "main") &&
      !in_system_header(rbrace) && falls_through(fn->body))
    warn_opt("return-type", rbrace, "control reaches end of non-void function '%s'",
             fn->name);
}

//---------- Top level: typedefs and function definitions --------------------

static Token *parse_typedef(Token *tok, Type *basety, VarAttr *attr) {
  bool first = true;

  while (!consume(&tok, tok, ";")) {
    if (!first)
      tok = skip_decl_comma(tok);
    first = false;

    Attrs all = attr->gnu;
    Type *ty = declarator(&tok, tok, basety, &all);
    if (!ty->name)
      error_tok(ty->name_pos, "typedef name omitted");
    if (all.is_packed)
      error_tok(all.layout_tok, "attribute 'packed' is not supported on a typedef");
    no_symbol_attrs(&all, "a typedef");

    // On a typedef, aligned(N) sets the new type's alignment, and can
    // lower it too, as with gcc. The type is still compatible with the
    // original.
    if (all.align) {
      ty = copy_type(ty);
      ty->align = all.align;
    }

    // [GNU] A transparent union, as a parameter, takes an argument of any
    // of its members' types, passed as its first member is. glibc's
    // socket functions take any struct sockaddr pointer this way.
    if (all.transparent_tok) {
      if (ty->kind != TY_UNION || !ty->members)
        error_tok(all.transparent_tok, "attribute 'transparent_union' needs a union");
      ty = copy_type(ty);
      ty->is_transparent = true;
    }

    // A typedef may be repeated in its scope, but only for the same type.
    char *name = get_ident(ty->name);
    VarScope *prev = hashmap_get(&scope->vars, name);
    if (prev && !prev->type_def)
      error_tok(ty->name, "'%s' redeclared as a different kind of symbol", name);
    if (prev && !is_compatible(prev->type_def, ty))
      error_tok(ty->name, "conflicting types for '%s'", name);
    push_scope(name)->type_def = ty;
  }
  return tok;
}

// C23 allows unnamed parameters in a definition, as in `int f(int) {...}`;
// they still get a stack slot, just no name.
// The declarations of a K&R definition's parameters, before its body:
// `f(a, b) char *a; { ... }`, where b, not declared, is an int. Each is
// passed promoted, a char as an int and a float as a double, so that is
// its parameter's type; `decl` gets each one's declared type, which the
// body sees (see function()).
static Type *kr_params(Token **rest, Token *tok, Type *fn_ty, Type ***decl) {
  int n = 1;
  for (Token *t = fn_ty->kr_names; !equal(t->next, ")"); t = t->next->next)
    n++;
  Token **names = arena_alloc(n * sizeof(Token *));
  Type **types = arena_alloc(n * sizeof(Type *));
  Token *t = fn_ty->kr_names;
  for (int i = 0; i < n; i++, t = t->next->next)
    names[i] = t;

  while (!equal(tok, "{")) {
    VarAttr attr = {};
    Type *basety = declspec(&tok, tok, &attr);
    do {
      Type *ty = declarator(&tok, tok, basety, NULL);
      int i = 0;
      while (i < n && !(ty->name && equal(names[i], get_ident(ty->name))))
        i++;
      if (i == n)
        error_tok(ty->name ? ty->name : tok, "declaration of a parameter that isn't in the list");
      if (types[i])
        error_tok(ty->name, "redeclaration of parameter '%s'", get_ident(ty->name));
      types[i] = ty;
    } while (consume(&tok, tok, ","));
    tok = skip(tok, ";");
  }
  *rest = tok;

  Type head = {};
  Type *cur = &head;
  for (int i = 0; i < n; i++) {
    Type *ty = types[i] ? types[i] : ty_int;
    if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
      ty = pointer_to(ty->base);
    else if (ty->kind == TY_FUNC)
      ty = pointer_to(ty);
    types[i] = ty;

    Type *passed = ty;
    if (ty->kind == TY_FLOAT)
      passed = ty_double;
    else if (is_integer(ty) && ty->size < 4)
      passed = ty_int;
    cur = cur->next = copy_type(passed);
    cur->name = names[i];

    Obj *var = arena_alloc(sizeof(Obj));
    var->name = get_ident(names[i]);
    var->ty = cur;
    var->align = cur->align;
    var->is_local = true;
    cur->param_var = var;
  }
  *decl = types;
  return head.next;
}

static void create_param_lvars(Type *param) {
  if (!param)
    return;
  create_param_lvars(param->next);

  // A named parameter already has its variable, from func_params().
  Obj *var = param->param_var;
  if (!var) {
    new_lvar("", param);
    return;
  }
  push_scope(var->name)->var = var;
  var->next = locals;
  locals = var;
}

// This function matches gotos or labels-as-values with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static void resolve_goto_labels(void) {
  for (Node *x = gotos; x; x = x->goto_next) {
    for (Node *y = labels; y; y = y->goto_next) {
      if (!strcmp(x->label, y->label)) {
        x->unique_label = y->unique_label;
        // Leaving the scope of cleanup variables and VLAs runs their
        // cleanups. Entering one is an error. `&&label` is no jump, and
        // a computed goto runs no cleanups, as with gcc.
        if (x->kind == ND_LABEL_VAL)
          break;
        if (!is_scope_of(y->cleanups, x->cleanups))
          jump_into_scope(y->cleanups, x->tok);
        if (x->cleanups != y->cleanups)
          x->lhs = cleanup_calls(x->cleanups, y->cleanups, x->tok);
        break;
      }
    }

    // After an error, the label may be in a statement that was skipped,
    // so only report this while there have been no other errors.
    if (x->unique_label == NULL && error_count == 0)
      error_tok(x->tok->next, "use of undeclared label");
  }

  gotos = labels = NULL;
}

static Obj *find_func(char *name) {
  Scope *sc = scope;
  while (sc->next)
    sc = sc->next;

  VarScope *sc2 = hashmap_get(&sc->vars, name);
  if (sc2 && sc2->var && sc2->var->is_function)
    return sc2->var;
  return NULL;
}

static void mark_live(Obj *var) {
  if (!var->is_function || var->is_live)
    return;
  var->is_live = true;

  for (int i = 0; i < var->refs.len; i++) {
    Obj *fn = find_func(var->refs.data[i]);
    if (fn)
      mark_live(fn);
  }
}

static Token *function(Token *tok, Type *basety, VarAttr *attr) {
  Attrs da = attr->gnu;
  Type *ty = declarator(&tok, tok, basety, &da);
  if (!ty->name)
    error_tok(ty->name_pos, "function name omitted");

  // A K&R definition's parameters, declared before its body
  Type *params = ty->params;
  Type **kr_decl = NULL;
  if (ty->kr_names && !equal(tok, ";") && !equal(tok, ","))
    params = kr_params(&tok, tok, ty, &kr_decl);
  char *name_str = get_ident(ty->name);
  bool is_noreturn = attr->is_noreturn || da.is_noreturn;

  // aligned(N) on a function only aligns its code, which changes nothing
  // a program can see, so it's ignored.
  if (da.is_packed)
    error_tok(da.layout_tok, "attribute 'packed' is not supported on a function");
  if (da.weak_tok && attr->is_static)
    error_tok(da.weak_tok, "a weak function must not be static");
  no_cleanup(&da, "a function");
  if (da.common_tok)
    error_tok(da.common_tok, "attribute '%s' is not supported on a function",
              da.common > 0 ? "common" : "nocommon");

  Obj *fn = find_func(name_str);
  if (fn) {
    // Redeclaration
    if (!fn->is_function)
      error_tok(tok, "redeclared as a different kind of symbol");
    if (!is_compatible(fn->ty, ty))
      error_tok(ty->name, "conflicting types for '%s'", name_str);
    if (fn->is_definition && equal(tok, "{"))
      error_tok(tok, "redefinition of %s", name_str);

    // A prototype after `int f()` is f's type from then on.
    if (fn->ty->is_oldstyle && !ty->is_oldstyle)
      fn->ty = ty;
    if (!fn->is_static && attr->is_static)
      error_tok(tok, "static declaration follows a non-static declaration");
    fn->is_definition = fn->is_definition || equal(tok, "{");
    fn->is_noreturn = fn->is_noreturn || is_noreturn;
  } else {
    fn = new_gvar(name_str, ty);
    fn->is_function = true;
    fn->is_definition = equal(tok, "{");
    // mucc treats a C99 `inline` function as static. With gnu_inline, it
    // is an ordinary external definition, as with gcc.
    fn->is_static = attr->is_static ||
                    (attr->is_inline && !attr->is_extern && !da.gnu_inline_tok);
    fn->is_inline = attr->is_inline;
    // A program's own function named like one (gawk has an err()) may
    // return, so only the C library's declaration counts.
    fn->is_noreturn = is_noreturn ||
                      (is_libc_noreturn(name_str) && in_system_header(ty->name));
  }

  // `int f(void) asm("g");`: f's symbol is g, as glibc's __REDIRECT has
  // it (`glob` is `glob64` with _FILE_OFFSET_BITS=64).
  if (da.asm_label_tok)
    fn->asm_name = da.asm_label;

  // Where it's defined (or first declared), for warnings
  if (!fn->tok || equal(tok, "{"))
    fn->tok = ty->name;
  fn->is_weak |= da.weak_tok != NULL;
  fn->is_kept |= da.is_used;
  if (da.ctor_tok) {
    fn->is_ctor = true;
    fn->ctor_prio = da.ctor_prio;
  }
  if (da.dtor_tok) {
    fn->is_dtor = true;
    fn->dtor_prio = da.dtor_prio;
  }

  if (da.alias_tok) {
    if (equal(tok, "{"))
      error_tok(da.alias_tok, "a function with an alias can't have a body");
    fn->alias_target = da.alias_target;
    fn->tok = da.alias_tok;
  }
  if (da.section)
    fn->section = da.section;
  if (da.visibility)
    fn->visibility = da.visibility;

  // Only a static inline function nothing calls is left out.
  fn->is_root = !(fn->is_static && fn->is_inline) || fn->is_kept ||
                fn->is_ctor || fn->is_dtor;

  if (consume(&tok, tok, ";"))
    return tok;

  // Inside a function, a definition (a GNU nested function) isn't
  // supported, and more declarators after a prototype are local
  // variables, left to block_item() (unless it's all extern).
  if (scope->next && equal(tok, "{"))
    error_tok(tok, "nested functions are not supported");
  if (scope->next && equal(tok, ",") && !attr->is_extern)
    return tok;

  // `int f(void), g(void), x;`: more declarators follow a prototype.
  if (equal(tok, ",")) {
    tok = tok->next;
    if (is_function(tok, basety))
      return function(tok, basety, attr);
    return global_variable(tok, basety, attr);
  }

  // gcc's `extern inline` (gnu_inline): the body is only for inlining,
  // which mucc doesn't do, so calls go to the definition elsewhere. It's
  // still parsed and checked.
  bool body_only_for_inlining =
    da.gnu_inline_tok && attr->is_inline && attr->is_extern;

  current_fn = fn;
  locals = NULL;
  current_block = NULL;
  enter_scope();
  create_param_lvars(params);

  // A buffer for a struct/union return value is passed
  // as the hidden first parameter.
  Type *rty = ty->return_ty;
  if ((rty->kind == TY_STRUCT || rty->kind == TY_UNION) && is_ret_in_memory(rty))
    new_lvar("", pointer_to(rty));

  fn->params = locals;

  if (ty->is_variadic && !kr_decl)
    fn->va_area = new_lvar("__va_area__", array_of(ty_char, 200));
  fn->alloca_bottom = new_lvar("__alloca_size__", pointer_to(ty_char));

  // A parameter like `int m[r][c]` is a pointer to a variable-length row,
  // whose size is computed on entry. This must happen before the body is
  // parsed, since pointer arithmetic on `m` uses that size.
  Node vla_head = {};
  Node *vla_cur = &vla_head;
  for (Type *param = params; param; param = param->next)
    if (param->base && is_variably_modified(param->base)) {
      vla_cur = vla_cur->next =
        new_unary(ND_EXPR_STMT, compute_vla_size(param, tok), tok);
      add_type(vla_cur);
    }

  // A K&R parameter declared narrower than it's passed, like a char or a
  // float, is a local of its declared type, set from it on entry.
  int i = 0;
  for (Type *param = params; kr_decl && param; param = param->next, i++) {
    Type *decl = kr_decl[i];
    if (decl->kind == param->kind && decl->size == param->size)
      continue;
    Obj *var = new_lvar(param->param_var->name, decl);
    Node *set = new_binary(ND_ASSIGN, new_var_node(var, tok),
                           new_cast(new_var_node(param->param_var, tok), decl), tok);
    vla_cur = vla_cur->next = new_unary(ND_EXPR_STMT, set, tok);
    add_type(vla_cur);
  }

  tok = skip(tok, "{");

  // [https://www.sigbus.info/n1570#6.4.2.2p1] "__func__" is
  // automatically defined as a local variable containing the
  // current function name.
  Obj *fn_name = new_string_literal(fn->name, array_of(ty_char, strlen(fn->name) + 1));
  push_scope("__func__")->var = fn_name;

  // [GNU] __FUNCTION__ and __PRETTY_FUNCTION__ are other names for
  // __func__ (in C, the "pretty" name is the plain one).
  push_scope("__FUNCTION__")->var = fn_name;
  push_scope("__PRETTY_FUNCTION__")->var = fn_name;

  Token *body = tok;
  is_fn_body = true;
  fn->body = compound_stmt(&tok, tok, false);
  if (vla_head.next) {
    vla_cur->next = fn->body->body;
    fn->body->body = vla_head.next;
  }
  fn->body = lower_complex(fn->body);
  fn->locals = locals;
  leave_scope();
  resolve_goto_labels();

  // Find the body's closing `}`, the token before `tok`.
  Token *rbrace = body;
  while (rbrace->next != tok)
    rbrace = rbrace->next;
  warn_function(fn, rbrace);
  if (body_only_for_inlining)
    fn->is_definition = false;
  return tok;
}

//---------- Top level: global variables -------------------------------------

// Globals declared `extern` in a block, by name: later declarations of
// the name must agree with them too (see global_variable()).
static HashMap block_externs;

static Token *global_variable(Token *tok, Type *basety, VarAttr *attr) {
  bool first = true;

  while (!consume(&tok, tok, ";")) {
    if (!first) {
      tok = skip_decl_comma(tok);
      // `int x, f(void);`: the rest starts with a function declaration.
      if (is_function(tok, basety))
        return function(tok, basety, attr);
    }
    first = false;

    Attrs all = attr->gnu;
    Type *ty = declarator(&tok, tok, basety, &all);
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");
    if (all.is_packed)
      error_tok(all.layout_tok, "attribute 'packed' is not supported on a variable");
    no_fn_attrs(&all, "a variable");
    no_cleanup(&all, "a global variable");

    Token *name = ty->name;
    if (basety == &auto_type) {
      check_auto_declarator(ty, tok);
      if (ty == &auto_type && equal(tok, "="))
        ty = peek_auto_type(tok->next);
    }

    // A second initializer for the same variable, `int x = 1; int x = 1;`
    VarScope *prev = find_var(name);
    if (equal(tok, "=") && prev && prev->var && !prev->var->is_function &&
        prev->var->is_definition && !prev->var->is_tentative)
      error_tok(name, "redefinition of %s", prev->var->name);

    // Its declarations must agree: `extern int v; extern long v;` is an
    // error (autoconf's tests rely on it), `extern int a[]; int a[3];` not.
    // So must one in a block, which is out of scope later.
    if (prev && prev->var && !prev->var->is_local) {
      if (prev->var->is_function)
        error_tok(name, "'%s' redeclared as a different kind of symbol", prev->var->name);
      if (!is_compatible(prev->var->ty, ty))
        error_tok(name, "conflicting types for '%s'", prev->var->name);
    }
    Obj *in_block = hashmap_get2(&block_externs, name->loc, name->len);
    if (in_block && !is_compatible(in_block->ty, ty))
      error_tok(name, "conflicting types for '%s'", in_block->name);

    // A variable of type void can only be declared.
    if (ty->kind == TY_VOID && !attr->is_extern)
      error_tok(name, "storage size of '%.*s' isn't known", name->len, name->loc);

    Obj *var = new_gvar(get_ident(name), ty);
    var->tok = name;
    var->is_definition = !attr->is_extern;
    var->is_static = attr->is_static;
    if (scope->next)
      hashmap_put2(&block_externs, name->loc, name->len, var);
    var->is_tls = attr->is_tls;
    if (attr->align)
      var->align = attr->align;
    var->align = MAX(var->align, all.align);
    if (all.weak_tok && attr->is_static)
      error_tok(all.weak_tok, "a weak variable must not be static");
    var->is_weak = all.weak_tok != NULL;
    var->section = all.section;
    var->common = all.common;

    // `extern int x asm("y");`: x's symbol is y, also in later
    // declarations of x without it. Likewise its visibility, as CPython's
    // headers declare PyAPI_DATA(PyTypeObject) PyList_Type.
    bool redecl = prev && prev->var && !prev->var->is_function && !prev->var->is_local;
    if (all.asm_label_tok)
      var->asm_name = all.asm_label;
    else if (redecl)
      var->asm_name = prev->var->asm_name;
    var->visibility = all.visibility;
    if (!all.visibility && redecl)
      var->visibility = prev->var->visibility;

    // An alias defines no storage of its own.
    if (all.alias_tok) {
      if (equal(tok, "="))
        error_tok(all.alias_tok, "a variable with an alias can't have an initializer");
      var->alias_target = all.alias_target;
      var->tok = all.alias_tok;
      var->is_definition = false;
      continue;
    }

    // A file-scope constexpr is internal to this file, like a static, so
    // a header can define one without clashing at link time.
    if (attr->is_constexpr) {
      if (!equal(tok, "="))
        error_tok(name, "constexpr '%s' needs an initializer", var->name);
      var->is_static = true;
      peek_constexpr_value(var, tok->next);
    }

    if (equal(tok, "="))
      gvar_initializer(&tok, tok->next, var);
    else if (!attr->is_extern)
      var->is_tentative = true;
  }
  return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
//
// The declarator alone decides, `f(void)`, unless it's a bare name and
// `basety` is a function type from a typedef: `F f;` declares a function
// too (as in Tcl's `Tcl_FSRenameFileProc TclpObjRenameFile;`).
static bool is_function(Token *tok, Type *basety) {
  if (equal(tok, ";"))
    return false;

  // A lookahead only: the attributes are checked when the real parse runs.
  // The dummy is an int, which mode(QI) and vector_size(16) can apply to.
  Type dummy = *ty_int;
  Attrs ignored = {};
  Type *ty = declarator(&tok, tok, &dummy, &ignored);
  return ty->kind == TY_FUNC || (ty == &dummy && basety->kind == TY_FUNC);
}

// Remove redundant tentative definitions: a tentative one is dropped if
// the same name has a definition with an initializer, or a tentative one
// kept already (`int x; int x;` keeps the first, so exactly one of them
// is emitted). Names are looked up in hash maps, as a file may have
// 100,000 globals.
static void scan_globals(void) {
  HashMap real = {};  // names with a definition that isn't tentative
  HashMap first = {}; // each name's first definition in `globals`
  for (Obj *var = globals; var; var = var->next) {
    if (!var->is_definition)
      continue;
    if (!var->is_tentative)
      hashmap_put(&real, var->name, var);
    if (!hashmap_get(&first, var->name))
      hashmap_put(&first, var->name, var);
  }

  Obj head;
  Obj *cur = &head;
  for (Obj *var = globals; var; var = var->next) {
    if (var->is_tentative &&
        (hashmap_get(&real, var->name) ||
         (var->is_definition && hashmap_get(&first, var->name) != var)))
      continue;
    cur = cur->next = var;
  }

  cur->next = NULL;
  globals = head.next;
}

//---------- C23 [[attributes]] and built-in declarations --------------------

static void declare_builtin_functions(void) {
  Type *ty = func_type(pointer_to(ty_void));
  ty->params = copy_type(ty_int);
  builtin_alloca = new_gvar("alloca", ty);
  builtin_alloca->is_definition = false;

  // __builtin_va_list, which <stdarg.h> calls va_list: one 24-byte
  // element, as the psABI lays it out. Only the code for va_arg reads the
  // fields; they're declared for -g, as gcc names them.
  Token *tok = tokenize(new_file("<built-in>", 0,
                                 "struct __va_list_tag { unsigned gp_offset, fp_offset;"
                                 " void *overflow_arg_area, *reg_save_area; }"));
  convert_pp_tokens(tok);
  va_elem_ty = declspec(&tok, tok, NULL);
  push_scope("__builtin_va_list")->type_def = array_of(va_elem_ty, 1);

  // gcc's names for the 128-bit integers
  push_scope("__int128_t")->type_def = ty_int128;
  push_scope("__uint128_t")->type_def = ty_uint128;
}

// A token the parser sees in place of `at`. Its text lives in a
// "<built-in>" file; errors about it report `at`.
static Token *new_builtin_token(Token *at, TokenKind kind, char *text) {
  Token *t = arena_alloc(sizeof(Token));
  *t = *at;
  t->kind = kind;
  t->file = new_file("<built-in>", at->file->file_no, text);
  t->loc = t->file->contents;
  t->len = strlen(text);
  t->origin = at;
  return t;
}

// Appends `__attribute__((` tokens from `first` to `last` `))` to `cur`.
static Token *append_gnu_attribute(Token *cur, Token *first, Token *last) {
  cur = cur->next = new_builtin_token(first, TK_KEYWORD, "__attribute__");
  cur = cur->next = new_builtin_token(first, TK_PUNCT, "(");
  cur = cur->next = new_builtin_token(first, TK_PUNCT, "(");
  cur->next = first;
  cur = last;
  cur = cur->next = new_builtin_token(last, TK_PUNCT, ")");
  cur = cur->next = new_builtin_token(last, TK_PUNCT, ")");
  return cur;
}

// C23 attributes, [[...]], are rewritten before parsing. In C, `[[` can't
// start anything else.
//  - [[gnu::name(args)]] becomes __attribute__((name(args))), so it gets
//    the same checks: a GNU attribute is never silently dropped.
//  - [[noreturn]] becomes the keyword _Noreturn, for the missing-return
//    warning, and [[maybe_unused]] becomes __attribute__((unused)).
//  - The other standard attributes are hints and are dropped.
//  - Anything else is dropped with a warning, as C23 asks.
static Token *remove_attributes(Token *tok) {
  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    if (!equal(tok, "[") || !equal(tok->next, "[")) {
      cur = cur->next = tok;
      tok = tok->next;
      continue;
    }

    Token *start = tok;
    tok = tok->next->next;

    while (!(equal(tok, "]") && equal(tok->next, "]"))) {
      if (tok->kind == TK_EOF)
        error_tok(start, "unterminated attribute");
      if (consume(&tok, tok, ","))
        continue;

      // prefix::name, where `::` is two ':' tokens
      Token *prefix = NULL;
      Token *name = tok;
      if (equal(tok->next, ":") && equal(tok->next->next, ":")) {
        prefix = tok;
        name = tok->next->next->next;
      }

      // The attribute's last token, and the token after it
      Token *last = name;
      if (equal(name->next, "(")) {
        int depth = 0;
        for (last = name->next; last->kind != TK_EOF; last = last->next) {
          if (equal(last, "("))
            depth++;
          else if (equal(last, ")") && --depth == 0)
            break;
        }
        if (last->kind == TK_EOF)
          error_tok(start, "unterminated attribute");
      }
      tok = last->next;

      if (prefix && (equal(prefix, "gnu") || equal(prefix, "__gnu__"))) {
        cur = append_gnu_attribute(cur, name, last);
        continue;
      }

      char *str = attribute_name(name);
      if (!prefix && (!strcmp(str, "noreturn") || !strcmp(str, "_Noreturn"))) {
        cur = cur->next = new_builtin_token(name, TK_KEYWORD, "_Noreturn");
      } else if (!prefix && !strcmp(str, "maybe_unused")) {
        Token *unused = new_builtin_token(name, TK_IDENT, "unused");
        cur = append_gnu_attribute(cur, unused, unused);
      } else if (!prefix && (!strcmp(str, "nodiscard") ||
                             !strcmp(str, "deprecated") ||
                             !strcmp(str, "fallthrough") ||
                             !strcmp(str, "unsequenced") ||
                             !strcmp(str, "reproducible"))) {
        // A hint mucc doesn't use.
      } else if (!in_system_header(name)) {
        warn_opt("attributes", name, "unknown attribute '%s%s%s' ignored",
                 prefix ? strndup(prefix->loc, prefix->len) : "",
                 prefix ? "::" : "", str);
      }
    }
    tok = tok->next->next;
  }

  cur->next = tok;
  return head.next;
}

//---------- Entry point -----------------------------------------------------

// top-level-item = static-assert | typedef | function-definition
//                | global-variable
static Token *top_level_item(Token *tok) {
  if (equal(tok, "_Static_assert"))
    return static_assertion(tok);

  // [GNU] asm("...") at file scope: assembly that goes into the output as
  // it is, as musl's startup code (_start) is written
  if (equal(tok, "asm") && equal(tok->next, "(")) {
    tok = tok->next->next;
    if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
      error_tok(tok, "expected string literal");
    strarray_push(&toplevel_asm, tok->str);
    tok = skip(tok->next, ")");
    return skip(tok, ";");
  }

  VarAttr attr = {};
  Type *basety = declspec(&tok, tok, &attr);

  if (attr.is_typedef)
    return parse_typedef(tok, basety, &attr);
  if (is_function(tok, basety))
    return function(tok, basety, &attr);
  return global_variable(tok, basety, &attr);
}

// Like top_level_item(), but on an error skips the item and goes on, as
// block_item_or_skip() does inside functions.
static Token *top_level_item_or_skip(Token *tok) {
  ParserState saved = save_state();
  jmp_buf here;

  if (setjmp(here)) {
    error_recovery = NULL;
    restore_state(saved);
    return skip_bad_item(tok);
  }

  error_recovery = &here;
  Token *rest = top_level_item(tok);
  error_recovery = NULL;
  return rest;
}

// program = top-level-item*
//
// The caller must check error_count: after errors, the result is
// incomplete and must not be compiled.
Obj *parse(Token *tok) {
  declare_builtin_functions();
  globals = NULL;
  tok = remove_attributes(tok);

  while (tok->kind != TK_EOF)
    tok = top_level_item_or_skip(tok);
  define_complex_helpers();
  if (overflow128_fn)
    define_helper(overflow128_fn, overflow128_text, overflow128_use);

  // An alias's target must be defined here, as with gcc, and is kept.
  for (Obj *var = globals; var; var = var->next) {
    if (!var->alias_target)
      continue;
    Obj *t = globals;
    while (t && !(t != var && (t->is_definition || t->alias_target) &&
                  !strcmp(t->name, var->alias_target)))
      t = t->next;
    if (!t)
      error_tok(var->tok, "alias target '%s' is not defined in this file",
                var->alias_target);
    else
      t->is_root = true;
  }

  for (Obj *var = globals; var; var = var->next)
    if (var->is_root)
      mark_live(var);

  if (!error_count)
    warn_unused_functions(globals);

  // Remove redundant tentative definitions.
  scan_globals();

  // From here on, names are symbols: an asm label replaces the C name.
  for (Obj *var = globals; var; var = var->next)
    if (var->asm_name)
      var->name = var->asm_name;
  return globals;
}
