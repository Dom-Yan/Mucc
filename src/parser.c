//============================================================================
// parser.c - STAGE 3 of 4: PARSE
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

#include "mucc.h"

//---------- Parser state ----------------------------------------------------

// Scope for local variables, global variables, typedefs
// or enum constants
typedef struct {
  Obj *var;
  Type *type_def;
  Type *enum_ty;
  int enum_val;
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
  bool is_unused; // __attribute__((unused)): no unused-variable warning
  int align;
  Attrs gnu;      // all GNU attributes in the declaration specifiers
} VarAttr;

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
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
  // `children` has initializers for its children.
  Initializer **children;

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
struct Cleanup {
  Cleanup *next;
  Obj *var;
  Obj *fn;
};

static Cleanup *cleanups;
static Cleanup *brk_cleanups;
static Cleanup *cont_cleanups;
static Cleanup *case_cleanups;

static Obj *builtin_alloca;

// declspec() returns this for `auto` with no other type, as in C23's
// `auto x = 1;`. declaration() and global_variable() then take the type
// from the initializer. Anywhere else it acts as the old implicit int.
static Type auto_type = {TY_INT, 4, 4};

static bool is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename(Token **rest, Token *tok);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *typeof_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty, Attrs *attrs);
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem);
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
static int64_t eval2(Node *node, char ***label);
static int64_t eval_rval(Node *node, char ***label);
static bool is_const_expr(Node *node);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static double eval_double(Node *node);
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
static bool is_function(Token *tok);
static bool falls_through(Node *node);
static Token *function(Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(Token *tok, Type *basety, VarAttr *attr);

//---------- Scopes and name lookup ------------------------------------------

static int align_down(int n, int align) {
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

static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = arena_alloc(sizeof(Node));
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

  Node *node = arena_alloc(sizeof(Node));
  node->kind = ND_CAST;
  node->tok = expr->tok;
  node->lhs = expr;
  node->ty = copy_type(ty);
  return node;
}

//---------- Variables, globals and string literals --------------------------

static VarScope *push_scope(char *name) {
  VarScope *sc = arena_alloc(sizeof(VarScope));
  hashmap_put(&scope->vars, name, sc);
  return sc;
}

static Initializer *new_initializer(Type *ty, bool is_flexible) {
  Initializer *init = arena_alloc(sizeof(Initializer));
  init->ty = ty;

  if (ty->kind == TY_ARRAY) {
    if (is_flexible && ty->size < 0) {
      init->is_flexible = true;
      return init;
    }

    init->children = calloc(ty->array_len, sizeof(Initializer *));
    for (int i = 0; i < ty->array_len; i++)
      init->children[i] = new_initializer(ty->base, false);
    return init;
  }

  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    // Count the number of struct members.
    int len = 0;
    for (Member *mem = ty->members; mem; mem = mem->next)
      len++;

    init->children = calloc(len, sizeof(Initializer *));

    for (Member *mem = ty->members; mem; mem = mem->next) {
      if (is_flexible && ty->is_flexible && !mem->next) {
        Initializer *child = arena_alloc(sizeof(Initializer));
        child->ty = mem->ty;
        child->is_flexible = true;
        init->children[mem->idx] = child;
      } else {
        init->children[mem->idx] = new_initializer(mem->ty, false);
      }
    }
    return init;
  }

  return init;
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
  return var;
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
  "retain", "weakref", "vector_size", "mode", "ifunc", "naked", "target",
  "target_clones", "transparent_union", "common", "nocommon", "copy",
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

// `packed`, `aligned`, `weak`, `constructor` and `destructor` change a
// layout or a symbol, so they are only allowed where the caller applies
// them (`allow_decl`); anywhere else, they're errors.
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
    int align = 16;
    if (args) {
      align = const_expr(&args, args);
      skip(args, ")");
    }
    if (align <= 0 || (align & (align - 1)))
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
  Token *toks[] = {a->weak_tok, a->alias_tok, a->section_tok, a->vis_tok};
  not_on(toks, 4, what);
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
  };

  Type *ty = ty_int;
  int counter = 0;
  bool is_atomic = false;
  bool is_auto = false;

  while (is_typename(tok)) {
    // Without `attr` (a cast, a parameter), nothing applies `aligned`.
    if (is_attribute(tok)) {
      Attrs a = {};
      tok = attributes(tok, &a, attr != NULL);
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
      tok = tok->next;
      continue;
    }

    // These keywords are recognized but ignored.
    if (consume(&tok, tok, "const") || consume(&tok, tok, "volatile") ||
        consume(&tok, tok, "register") || consume(&tok, tok, "restrict") ||
        consume(&tok, tok, "__restrict") || consume(&tok, tok, "__restrict__"))
      continue;

    // With a type, `auto` is the old storage class and means nothing.
    // Alone, it's C23 type inference (see auto_type).
    if (equal(tok, "auto")) {
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

      if (is_typename(tok))
        attr->align = typename(&tok, tok)->align;
      else
        attr->align = const_expr(&tok, tok);
      tok = skip(tok, ")");
      continue;
    }

    // Handle user-defined types.
    Type *ty2 = find_typedef(tok);
    if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") ||
        equal(tok, "typeof") || ty2) {
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
    case SIGNED + CHAR:
      ty = ty_char;
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
    case LONG + LONG:
    case LONG + LONG + INT:
    case SIGNED + LONG:
    case SIGNED + LONG + INT:
    case SIGNED + LONG + LONG:
    case SIGNED + LONG + LONG + INT:
      ty = ty_long;
      break;
    case UNSIGNED + LONG:
    case UNSIGNED + LONG + INT:
    case UNSIGNED + LONG + LONG:
    case UNSIGNED + LONG + LONG + INT:
      ty = ty_ulong;
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
    default:
      error_tok(tok, "invalid type");
    }

    tok = tok->next;
  }

  if (is_atomic) {
    ty = copy_type(ty);
    ty->is_atomic = true;
  }

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

    Type *ty2 = declspec(&tok, tok, NULL);
    ty2 = declarator(&tok, tok, ty2, NULL);

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

  if (cur == &head)
    is_variadic = true;

  ty = func_type(ty);
  ty->params = head.next;
  ty->is_variadic = is_variadic;
  *rest = tok->next;
  return ty;
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
    return array_of(ty, -1);
  }

  Node *expr = conditional(&tok, tok);
  tok = skip(tok, "]");
  ty = type_suffix(rest, tok, ty);

  if (ty->kind == TY_VLA || !is_const_expr(expr))
    return vla_of(ty, expr);
  return array_of(ty, eval(expr));
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
      if (equal(tok, "const") || equal(tok, "volatile") || equal(tok, "restrict") ||
          equal(tok, "__restrict") || equal(tok, "__restrict__"))
        tok = tok->next;
      else if (is_attribute(tok))
        tok = skip_attributes(tok);
      else
        break;
    }
  }
  *rest = tok;
  return ty;
}

// declarator = attributes pointers attributes
//              ("(" declarator ")" | ident attributes)? type-suffix attributes
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

  if (equal(tok, "(")) {
    Token *start = tok;
    Type dummy = {};
    Attrs ignored = {};
    declarator(&tok, start->next, &dummy, &ignored);
    tok = skip(tok, ")");
    ty = type_suffix(&tok, tok, ty);
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
    *rest = attributes(tok, attrs, true);
    ty->name = name;
    ty->name_pos = name_pos;
  }

  if (attrs == &scratch)
    no_decl_attrs(&scratch);
  return ty;
}

// abstract-declarator = attributes pointers attributes
//                       ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  tok = skip_attributes(tok);
  ty = pointers(&tok, tok, ty);
  tok = skip_attributes(tok);

  if (equal(tok, "(")) {
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

// enum-specifier = attributes ident? "{" enum-list? "}" attributes
//                | attributes ident ("{" enum-list? "}" attributes)?
//
// enum-list      = enumerator ("," enumerator)* ","?
// enumerator     = ident attributes ("=" num)?
static Type *enum_specifier(Token **rest, Token *tok) {
  Type *ty = enum_type();
  Attrs a = {};
  tok = attributes(tok, &a, true);

  // Read a struct tag.
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }

  if (tag && !equal(tok, "{")) {
    Type *ty = find_tag(tag);
    if (!ty)
      error_tok(tag, "unknown enum type");
    if (ty->kind != TY_ENUM)
      error_tok(tag, "not an enum tag");
    *rest = tok;
    return ty;
  }

  tok = skip(tok, "{");

  // Read an enum-list.
  int i = 0;
  int val = 0;
  int min = 0, max = 0;
  while (!consume_end(rest, tok)) {
    if (i++ > 0)
      tok = skip(tok, ",");

    char *name = get_ident(tok);
    tok = skip_attributes(tok->next);

    if (equal(tok, "="))
      val = const_expr(&tok, tok->next);

    min = (i == 1) ? val : MIN(min, val);
    max = (i == 1) ? val : MAX(max, val);
    VarScope *sc = push_scope(name);
    sc->enum_ty = ty;
    sc->enum_val = val++;
  }

  *rest = attributes(*rest, &a, true);
  no_symbol_attrs(&a, "an enum");
  if (a.align)
    error_tok(a.layout_tok, "attribute 'aligned' is not supported on an enum");

  // A packed enum is the smallest integer type that holds its values, as
  // with gcc.
  if (a.is_packed) {
    ty->is_unsigned = min >= 0;
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
// functions turned into pointers and _Atomic dropped.
static Type *auto_type_of(Node *init) {
  add_type(init);
  Type *ty = init->ty;

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
    var->constexpr_fval = eval_double(init);
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

// `fn(&var)`, for the cleanup of `var`.
static Node *cleanup_call(Cleanup *c, Token *tok) {
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

// `return` in the scope of cleanup variables: the value is computed
// first, as it may use them, then the cleanups run, as with gcc.
static Node *return_with_cleanups(Node *ret) {
  if (!cleanups)
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
    base_sz = new_num(ty->base->size, tok);

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
    if (i++ > 0)
      tok = skip_decl_comma(tok);

    Attrs da = {};
    Type *ty = declarator(&tok, tok, basety, &da);
    if (ty->kind == TY_VOID)
      error_tok(tok, "variable declared void");
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");

    // __attribute__((unused)) turns off the unused-variable warning.
    bool is_unused = da.is_unused || (attr && attr->is_unused);

    // aligned(N) raises the variable's alignment. The stack is only
    // 16-byte aligned, so more than that works only for a static.
    Attrs all = attr ? attr->gnu : (Attrs){};
    merge_attrs(&all, &da);
    no_global_attrs(&all, "a local variable");
    no_fn_attrs(&all, "a local variable");
    if (all.is_packed)
      error_tok(all.layout_tok, "attribute 'packed' is not supported on a variable");
    if (all.align > 16 && !(attr && attr->is_static))
      error_tok(all.layout_tok,
                "alignment above 16 on a local variable is not supported yet");
    Token *name = ty->name;
    bool is_constexpr = attr && attr->is_constexpr;
    if (is_constexpr && !equal(tok, "="))
      error_tok(name, "constexpr '%s' needs an initializer", get_ident(name));
    if (basety == &auto_type)
      check_auto_declarator(ty, tok);

    if (attr && attr->is_static) {
      // static local variable
      no_cleanup(&all, "a static variable");
      if (ty == &auto_type && equal(tok, "="))
        ty = peek_auto_type(tok->next);
      Obj *var = new_anon_gvar(ty);
      var->align = MAX(var->align, all.align);
      push_scope(get_ident(name))->var = var;
      if (is_constexpr)
        peek_constexpr_value(var, tok->next);
      if (equal(tok, "="))
        gvar_initializer(&tok, tok->next, var);
      continue;
    }

    // C23 `auto x = init;`: x takes init's type.
    if (ty == &auto_type && equal(tok, "=")) {
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

      // Variable length arrays (VLAs) are translated to alloca() calls.
      // For example, `int x[n+2]` is translated to `tmp = n + 2,
      // x = alloca(tmp)`.
      Obj *var = new_lvar(get_ident(ty->name), ty);
      var->tok = ty->name;
      var->is_used = is_unused;
      Token *tok = ty->name;
      Node *expr = new_binary(ND_ASSIGN, new_vla_ptr(var, tok),
                              new_alloca(new_var_node(ty->vla_size, tok)),
                              tok);

      cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
      continue;
    }

    Obj *var = new_lvar(get_ident(ty->name), ty);
    var->tok = ty->name;
    var->is_used = is_unused;
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
  if (init->is_flexible)
    *init = *new_initializer(array_of(init->ty->base, tok->ty->array_len), false);

  int len = MIN(init->ty->array_len, tok->ty->array_len);

  switch (init->ty->base->size) {
  case 1: {
    char *str = tok->str;
    for (int i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  case 2: {
    uint16_t *str = (uint16_t *)tok->str;
    for (int i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  case 4: {
    uint32_t *str = (uint32_t *)tok->str;
    for (int i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  default:
    unreachable();
  }

  *rest = tok->next;
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
static void array_designator(Token **rest, Token *tok, Type *ty, int *begin, int *end) {
  *begin = const_expr(&tok, tok->next);
  if (*begin >= ty->array_len)
    error_tok(tok, "array designator index exceeds array bounds");

  if (equal(tok, "...")) {
    *end = const_expr(&tok, tok->next);
    if (*end >= ty->array_len)
      error_tok(tok, "array designator index exceeds array bounds");
    if (*end < *begin)
      error_tok(tok, "array designator range [%d, %d] is empty", *begin, *end);
  } else {
    *end = *begin;
  }

  *rest = skip(tok, "]");
}

// struct-designator = "." ident
static Member *struct_designator(Token **rest, Token *tok, Type *ty) {
  Token *start = tok;
  tok = skip(tok, ".");
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected a field designator");

  for (Member *mem = ty->members; mem; mem = mem->next) {
    // Anonymous struct member
    if (mem->ty->kind == TY_STRUCT && !mem->name) {
      if (get_struct_member(mem->ty, tok)) {
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

    int begin, end;
    array_designator(&tok, tok, init->ty, &begin, &end);

    Token *tok2;
    for (int i = begin; i <= end; i++)
      designation(&tok2, tok, init->children[i]);
    array_initializer2(rest, tok2, init, begin + 1);
    return;
  }

  if (equal(tok, ".") && init->ty->kind == TY_STRUCT) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    designation(&tok, tok, init->children[mem->idx]);
    init->expr = NULL;
    struct_initializer2(rest, tok, init, mem->next);
    return;
  }

  if (equal(tok, ".") && init->ty->kind == TY_UNION) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    init->mem = mem;
    designation(rest, tok, init->children[mem->idx]);
    return;
  }

  if (equal(tok, "."))
    error_tok(tok, "field name not in struct or union initializer");

  if (equal(tok, "="))
    tok = tok->next;
  initializer2(rest, tok, init);
}

// An array length can be omitted if an array has an initializer
// (e.g. `int x[] = {1,2,3}`). If it's omitted, count the number
// of initializer elements.
static int count_array_init_elements(Token *tok, Type *ty) {
  bool first = true;
  Initializer *dummy = new_initializer(ty->base, true);

  int i = 0, max = 0;

  while (!consume_end(&tok, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[")) {
      i = const_expr(&tok, tok->next);
      if (equal(tok, "..."))
        i = const_expr(&tok, tok->next);
      tok = skip(tok, "]");
      designation(&tok, tok, dummy);
    } else {
      initializer2(&tok, tok, dummy);
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

  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len), false);
  }

  for (int i = 0; !consume_end(rest, tok); i++) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[")) {
      int begin, end;
      array_designator(&tok, tok, init->ty, &begin, &end);

      Token *tok2;
      for (int j = begin; j <= end; j++)
        designation(&tok2, tok, init->children[j]);
      tok = tok2;
      i = end;
      continue;
    }

    if (i < init->ty->array_len)
      initializer2(&tok, tok, init->children[i]);
    else
      tok = skip_excess_element(tok);
  }
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i) {
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

    initializer2(&tok, tok, init->children[i]);
  }
  *rest = tok;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
  tok = skip(tok, "{");

  Member *mem = init->ty->members;
  bool first = true;

  while (!consume_end(rest, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, ".")) {
      mem = struct_designator(&tok, tok, init->ty);
      designation(&tok, tok, init->children[mem->idx]);
      mem = mem->next;
      continue;
    }

    if (mem) {
      initializer2(&tok, tok, init->children[mem->idx]);
      mem = mem->next;
    } else {
      tok = skip_excess_element(tok);
    }
  }
}

// struct-initializer2 = initializer ("," initializer)*
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem) {
  bool first = true;

  for (; mem && !is_end(tok); mem = mem->next) {
    Token *start = tok;

    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }

    initializer2(&tok, tok, init->children[mem->idx]);
  }
  *rest = tok;
}

static void union_initializer(Token **rest, Token *tok, Initializer *init) {
  // Unlike structs, union initializers take only one initializer,
  // and that initializes the first union member by default.
  // You can initialize other member using a designated initializer.
  if (equal(tok, "{") && equal(tok->next, ".")) {
    Member *mem = struct_designator(&tok, tok->next, init->ty);
    init->mem = mem;
    designation(&tok, tok, init->children[mem->idx]);
    *rest = skip(tok, "}");
    return;
  }

  init->mem = init->ty->members;

  if (equal(tok, "{")) {
    initializer2(&tok, tok->next, init->children[0]);
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
  } else {
    initializer2(rest, tok, init->children[0]);
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

    struct_initializer2(rest, tok, init, init->ty->members);
    return;
  }

  if (init->ty->kind == TY_UNION) {
    union_initializer(rest, tok, init);
    return;
  }

  if (equal(tok, "{")) {
    // An initializer for a scalar variable can be surrounded by
    // braces. E.g. `int x = {3};`. Handle that case.
    initializer2(&tok, tok->next, init);
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
    mem->ty = init->children[mem->idx]->ty;
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

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
  if (ty->kind == TY_ARRAY) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    for (int i = 0; i < ty->array_len; i++) {
      InitDesg desg2 = {desg, i};
      Node *rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_STRUCT && !init->expr) {
    Node *node = new_node(ND_NULL_EXPR, tok);

    for (Member *mem = ty->members; mem; mem = mem->next) {
      InitDesg desg2 = {desg, 0, mem};
      Node *rhs = create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_UNION) {
    Member *mem = init->mem ? init->mem : ty->members;
    InitDesg desg2 = {desg, 0, mem};
    return create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
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

  // A scalar with a value, like `int x = 5`, is just assigned it.
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

static uint64_t read_buf(char *buf, int sz) {
  if (sz == 1)
    return *buf;
  if (sz == 2)
    return *(uint16_t *)buf;
  if (sz == 4)
    return *(uint32_t *)buf;
  if (sz == 8)
    return *(uint64_t *)buf;
  unreachable();
}

static void write_buf(char *buf, uint64_t val, int sz) {
  if (sz == 1)
    *buf = val;
  else if (sz == 2)
    *(uint16_t *)buf = val;
  else if (sz == 4)
    *(uint32_t *)buf = val;
  else if (sz == 8)
    *(uint64_t *)buf = val;
  else
    unreachable();
}

static Relocation *
write_gvar_data(Relocation *cur, Initializer *init, Type *ty, char *buf, int offset) {
  if (ty->kind == TY_ARRAY) {
    int sz = ty->base->size;
    for (int i = 0; i < ty->array_len; i++)
      cur = write_gvar_data(cur, init->children[i], ty->base, buf, offset + sz * i);
    return cur;
  }

  if (ty->kind == TY_STRUCT) {
    for (Member *mem = ty->members; mem; mem = mem->next) {
      if (mem->is_bitfield) {
        Node *expr = init->children[mem->idx]->expr;
        if (!expr)
          break;

        char *loc = buf + offset + mem->offset;
        uint64_t oldval = read_buf(loc, mem->ty->size);
        uint64_t newval = eval(expr);
        uint64_t mask = (1L << mem->bit_width) - 1;
        uint64_t combined = oldval | ((newval & mask) << mem->bit_offset);
        write_buf(loc, combined, mem->ty->size);
      } else {
        cur = write_gvar_data(cur, init->children[mem->idx], mem->ty, buf,
                              offset + mem->offset);
      }
    }
    return cur;
  }

  if (ty->kind == TY_UNION) {
    if (!init->mem)
      return cur;
    return write_gvar_data(cur, init->children[init->mem->idx],
                           init->mem->ty, buf, offset);
  }

  if (!init->expr)
    return cur;

  if (ty->kind == TY_FLOAT) {
    *(float *)(buf + offset) = eval_double(init->expr);
    return cur;
  }

  if (ty->kind == TY_DOUBLE) {
    *(double *)(buf + offset) = eval_double(init->expr);
    return cur;
  }

  // A bool is 0 or 1, not the low byte of the value (see eval2's ND_CAST).
  if (ty->kind == TY_BOOL) {
    char **label = NULL;
    buf[offset] = eval2(new_cast(init->expr, ty_bool), &label);
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
  Initializer *init = initializer(rest, tok, var->ty, &var->ty);

  Relocation head = {};
  char *buf = calloc(1, var->ty->size);
  write_gvar_data(&head, init, var->ty, buf, 0);
  var->init_data = buf;
  var->rel = head.next;
}

//---------- Statements ------------------------------------------------------

// Returns true if a given token represents a type.
static bool is_typename(Token *tok) {
  static HashMap map;

  if (map.capacity == 0) {
    static char *kw[] = {
      "void", "_Bool", "char", "short", "int", "long", "struct", "union",
      "typedef", "enum", "static", "extern", "_Alignas", "signed", "unsigned",
      "const", "volatile", "auto", "register", "restrict", "__restrict",
      "__restrict__", "_Noreturn", "float", "double", "typeof", "inline",
      "_Thread_local", "__thread", "_Atomic", "constexpr", "__attribute__",
      "__attribute",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
      hashmap_put(&map, kw[i], (void *)1);
  }

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

// asm-stmt = "asm" ("volatile" | "inline")* "(" string-literal ")"
static Node *asm_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_ASM, tok);
  tok = tok->next;

  while (equal(tok, "volatile") || equal(tok, "inline"))
    tok = tok->next;

  tok = skip(tok, "(");
  if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
    error_tok(tok, "expected string literal");
  node->asm_str = tok->str;
  *rest = skip(tok->next, ")");
  return node;
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
    }

    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
      exp = new_cast(exp, current_fn->ty->return_ty);

    node->lhs = exp;
    return return_with_cleanups(node);
  }

  if (equal(tok, "if")) {
    Node *node = new_node(ND_IF, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(&tok, tok);
    if (equal(tok, "else"))
      node->els = stmt(&tok, tok->next);
    *rest = tok;
    return node;
  }

  if (equal(tok, "switch")) {
    Node *node = new_node(ND_SWITCH, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");

    Node *sw = current_switch;
    current_switch = node;

    char *brk = brk_label;
    brk_label = node->brk_label = new_unique_name();
    Cleanup *brk_c = brk_cleanups, *case_c = case_cleanups;
    brk_cleanups = case_cleanups = cleanups;

    node->then = stmt(rest, tok);

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
      error_tok(tok, "jump into the scope of a variable with a cleanup");

    Node *node = new_node(ND_CASE, tok);
    int begin = const_expr(&tok, tok->next);
    int end;

    if (equal(tok, "...")) {
      // [GNU] Case ranges, e.g. "case 1 ... 5:"
      end = const_expr(&tok, tok->next);
      if (end < begin)
        error_tok(tok, "empty case range specified");
    } else {
      end = begin;
    }

    tok = skip(tok, ":");
    node->label = new_unique_name();
    node->lhs = label_body(rest, tok);
    node->begin = begin;
    node->end = end;
    node->case_next = current_switch->case_next;
    current_switch->case_next = node;
    return node;
  }

  if (equal(tok, "default")) {
    if (!current_switch)
      error_tok(tok, "stray default");
    if (cleanups != case_cleanups)
      error_tok(tok, "jump into the scope of a variable with a cleanup");

    Node *node = new_node(ND_CASE, tok);
    tok = skip(tok->next, ":");
    node->label = new_unique_name();
    node->lhs = label_body(rest, tok);
    current_switch->default_case = node;
    return node;
  }

  if (equal(tok, "for")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");

    enter_scope();

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

    if (!equal(tok, ";"))
      node->cond = expr(&tok, tok);
    tok = skip(tok, ";");

    if (!equal(tok, ")"))
      node->inc = expr(&tok, tok);
    tok = skip(tok, ")");

    node->then = stmt(rest, tok);

    leave_scope();
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
    node->cond = expr(&tok, tok);
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
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    *rest = skip(tok, ";");
    return node;
  }

  if (equal(tok, "asm"))
    return asm_stmt(rest, tok);

  if (equal(tok, "goto")) {
    if (equal(tok->next, "*")) {
      // [GNU] `goto *ptr` jumps to the address specified by `ptr`.
      if (cleanups)
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

  if (is_function(tok)) {
    *rest = function(tok, basety, &attr);
    return NULL;
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

  while (!equal(tok, "}") && tok->kind != TK_EOF) {
    Node *item = block_item_or_skip(&tok, tok);
    if (item)
      cur = cur->next = item;
  }

  leave_scope();

  node->body = head.next;
  if (cleanups != outer) {
    if (is_stmt_expr)
      error_tok(tok, "a variable with a cleanup at the end of a statement "
                "expression is not supported");
    if (falls_through(node))
      cur->next = cleanup_calls(cleanups, outer, tok);
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

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr+n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
static int64_t eval2(Node *node, char ***label) {
  add_type(node);

  if (is_flonum(node->ty))
    return eval_double(node);

  switch (node->kind) {
  case ND_ADD:
    return eval2(node->lhs, label) + eval(node->rhs);
  case ND_SUB:
    return eval2(node->lhs, label) - eval(node->rhs);
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
    if (node->ty->is_unsigned && node->ty->size == 8)
      return (uint64_t)eval(node->lhs) >> eval(node->rhs);
    return eval(node->lhs) >> eval(node->rhs);
  case ND_EQ:
    return eval(node->lhs) == eval(node->rhs);
  case ND_NE:
    return eval(node->lhs) != eval(node->rhs);
  case ND_LT:
    if (node->lhs->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) < eval(node->rhs);
    return eval(node->lhs) < eval(node->rhs);
  case ND_LE:
    if (node->lhs->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) <= eval(node->rhs);
    return eval(node->lhs) <= eval(node->rhs);
  case ND_COND:
    return eval(node->cond) ? eval2(node->then, label) : eval2(node->els, label);
  case ND_COMMA:
    return eval2(node->rhs, label);
  case ND_NOT:
    return !eval(node->lhs);
  case ND_BITNOT:
    return ~eval(node->lhs);
  case ND_LOGAND:
    return eval(node->lhs) && eval(node->rhs);
  case ND_LOGOR:
    return eval(node->lhs) || eval(node->rhs);
  case ND_CAST: {
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
    if (is_integer(node->ty)) {
      switch (node->ty->size) {
      case 1: return node->ty->is_unsigned ? (uint8_t)val : (int8_t)val;
      case 2: return node->ty->is_unsigned ? (uint16_t)val : (int16_t)val;
      case 4: return node->ty->is_unsigned ? (uint32_t)val : (int32_t)val;
      }
    }
    return val;
  }
  case ND_ADDR:
    return eval_rval(node->lhs, label);
  case ND_LABEL_VAL:
    *label = &node->unique_label;
    return 0;
  case ND_MEMBER:
    if (!label)
      error_tok(node->tok, "not a compile-time constant");
    if (node->ty->kind != TY_ARRAY)
      error_tok(node->tok, "invalid initializer");
    return eval_rval(node->lhs, label) + node->member->offset;
  case ND_VAR:
    if (node->var->is_constexpr)
      return node->var->constexpr_val;
    if (!label)
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
    if (node->var->is_local)
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

static bool is_const_expr(Node *node) {
  add_type(node);

  switch (node->kind) {
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
    return is_const_expr(node->rhs);
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

static double eval_double(Node *node) {
  add_type(node);

  if (is_integer(node->ty)) {
    if (node->ty->is_unsigned)
      return (unsigned long)eval(node);
    return eval(node);
  }

  switch (node->kind) {
  case ND_ADD:
    return eval_double(node->lhs) + eval_double(node->rhs);
  case ND_SUB:
    return eval_double(node->lhs) - eval_double(node->rhs);
  case ND_MUL:
    return eval_double(node->lhs) * eval_double(node->rhs);
  case ND_DIV:
    return eval_double(node->lhs) / eval_double(node->rhs);
  case ND_NEG:
    return -eval_double(node->lhs);
  case ND_COND:
    return eval_double(node->cond) ? eval_double(node->then) : eval_double(node->els);
  case ND_COMMA:
    return eval_double(node->rhs);
  case ND_CAST:
    if (is_flonum(node->lhs->ty))
      return eval_double(node->lhs);
    return eval(node->lhs);
  case ND_NUM:
    return node->fval;
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
// A constexpr can't change after its initialization. (mucc doesn't track
// `const` in general, but a constexpr's value is also baked into
// constant expressions, so a change would be silently half-applied.)
static void check_modifiable(Node *lhs) {
  if (lhs->kind == ND_VAR && lhs->var->is_constexpr)
    error_tok(lhs->tok, "cannot modify constexpr '%s'", lhs->var->name);
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

  // If A is an atomic type, Convert `A op= B` to
  //
  // ({
  //   T1 *addr = &A; T2 val = (B); T1 old = *addr; T1 new;
  //   do {
  //    new = old op val;
  //   } while (!atomic_compare_exchange_strong(addr, &old, new));
  //   new;
  // })
  if (binary->lhs->ty->is_atomic) {
    Node head = {};
    Node *cur = &head;

    Obj *addr = new_lvar("", pointer_to(binary->lhs->ty));
    Obj *val = new_lvar("", binary->rhs->ty);
    Obj *old = new_lvar("", binary->lhs->ty);
    Obj *new = new_lvar("", binary->lhs->ty);

    cur = cur->next =
      new_unary(ND_EXPR_STMT,
                new_binary(ND_ASSIGN, new_var_node(addr, tok),
                           new_unary(ND_ADDR, binary->lhs, tok), tok),
                tok);

    cur = cur->next =
      new_unary(ND_EXPR_STMT,
                new_binary(ND_ASSIGN, new_var_node(val, tok), binary->rhs, tok),
                tok);

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
                            new_binary(binary->kind, new_var_node(old, tok),
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
    cur = cur->next = new_unary(ND_EXPR_STMT, new_var_node(new, tok), tok);

    Node *node = new_node(ND_STMT_EXPR, tok);
    node->body = head.next;
    return node;
  }

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

// conditional = logor ("?" expr? ":" conditional)?
static Node *conditional(Token **rest, Token *tok) {
  Node *cond = logor(&tok, tok);

  if (!equal(tok, "?")) {
    *rest = tok;
    return cond;
  }

  if (equal(tok->next, ":")) {
    // [GNU] Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
    add_type(cond);
    Obj *var = new_lvar("", cond->ty);
    Node *lhs = new_binary(ND_ASSIGN, new_var_node(var, tok), cond, tok);
    Node *rhs = new_node(ND_COND, tok);
    rhs->cond = new_var_node(var, tok);
    rhs->then = new_var_node(var, tok);
    rhs->els = conditional(rest, tok->next->next);
    return new_binary(ND_COMMA, lhs, rhs, tok);
  }

  Node *node = new_node(ND_COND, tok);
  node->cond = cond;
  node->then = expr(&tok, tok->next);
  tok = skip(tok, ":");
  node->els = conditional(rest, tok);
  return node;
}

// logor = logand ("||" logand)*
static Node *logor(Token **rest, Token *tok) {
  Node *node = logand(&tok, tok);
  while (equal(tok, "||")) {
    Token *start = tok;
    node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(Token **rest, Token *tok) {
  Node *node = bitor(&tok, tok);
  while (equal(tok, "&&")) {
    Token *start = tok;
    node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
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

    if (equal(tok, "==")) {
      node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "!=")) {
      node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
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

    if (equal(tok, "<<")) {
      node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, ">>")) {
      node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
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
static Node *scale(Node *n, int size, Token *tok) {
  if (size == 1)
    return new_cast(n, ty_long);
  return new_binary(ND_MUL, n, new_long(size, tok), tok);
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);

  if (lhs->ty->base && rhs->ty->base)
    error_tok(tok, "invalid operands");

  // Canonicalize `num + ptr` to `ptr + num`.
  if (!lhs->ty->base && rhs->ty->base) {
    Node *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
  }

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
  if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);

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
    return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
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

    if (equal(tok, "/")) {
      node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "%")) {
      node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
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

    // type cast
    Node *node = new_cast(cast(rest, tok), ty);
    node->tok = start;
    return node;
  }

  return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | "&&" ident
//       | postfix
static Node *unary(Token **rest, Token *tok) {
  if (equal(tok, "+"))
    return cast(rest, tok->next);

  if (equal(tok, "-"))
    return new_unary(ND_NEG, cast(rest, tok->next), tok);

  if (equal(tok, "&")) {
    Node *lhs = cast(rest, tok->next);
    add_type(lhs);
    if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
      error_tok(tok, "cannot take address of bitfield");
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
      first = false;

      Member *mem = arena_alloc(sizeof(Member));
      Attrs all = attr.gnu;
      mem->ty = declarator(&tok, tok, basety, &all);
      mem->name = mem->ty->name;
      mem->idx = idx++;

      if (consume(&tok, tok, ":")) {
        mem->is_bitfield = true;
        mem->bit_width = const_expr(&tok, tok);
        tok = attributes(tok, &all, true);
      }

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

  tok = skip(tok, "{");

  // The tag is in scope from the `{` on, so a member can refer to its own
  // struct, even in a function pointer's parameters, as in
  // `struct S { int (*f)(struct S *); };`. It is incomplete until the `}`.
  // A struct already declared in this scope (`struct S;`) is the same type,
  // completed here.
  Type *prev = NULL;
  if (tag) {
    prev = hashmap_get2(&scope->tags, tag->loc, tag->len);
    if (!prev)
      push_tag_scope(tag, ty);
  }

  // Construct a struct object.
  ty->size = -1;
  struct_members(&tok, tok, ty);
  ty->size = 0;
  *rest = attribute_list(tok, ty);

  if (prev) {
    *prev = *ty;
    return prev;
  }
  return ty;
}

// In a packed struct, only an explicit aligned(N) or _Alignas on a member
// counts; every other member is 1-byte aligned.
static int member_align(Type *ty, Member *mem) {
  if (ty->is_packed)
    return MAX(1, mem->attr_align);
  return mem->align;
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_STRUCT;

  if (ty->size < 0)
    return ty;

  // Assign offsets within the struct to members.
  int bits = 0;

  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (mem->is_bitfield && mem->bit_width == 0) {
      // Zero-width anonymous bitfield has a special meaning.
      // It affects only alignment.
      bits = align_to(bits, mem->ty->size * 8);
    } else if (mem->is_bitfield) {
      int sz = mem->ty->size;
      if (bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8))
        bits = align_to(bits, sz * 8);

      mem->offset = align_down(bits / 8, sz);
      mem->bit_offset = bits % (sz * 8);
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
  // alignment and the size though.
  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (ty->align < mem->align)
      ty->align = mem->align;
    if (ty->size < mem->ty->size)
      ty->size = mem->ty->size;
  }
  ty->size = align_to(ty->size, ty->align);
  return ty;
}

// Find a struct member by name.
static Member *get_struct_member(Type *ty, Token *tok) {
  for (Member *mem = ty->members; mem; mem = mem->next) {
    // Anonymous struct member
    if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) &&
        !mem->name) {
      if (get_struct_member(mem->ty, tok))
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

//---------- Postfix, calls, _Generic and primary expressions ----------------

// Convert A++ to `(typeof A)((A += 1) - 1)`
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
  add_type(node);
  return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
                          new_num(-addend, tok), tok),
                  node->ty);
}

// postfix = "(" type-name ")" "{" initializer-list "}"
//         = ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static Node *postfix(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    // Compound literal
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");

    if (scope->next == NULL) {
      Obj *var = new_anon_gvar(ty);
      gvar_initializer(rest, tok, var);
      return new_var_node(var, start);
    }

    Obj *var = new_lvar("", ty);
    Node *lhs = lvar_initializer(rest, tok, var);
    Node *rhs = new_var_node(var, tok);
    return new_binary(ND_COMMA, lhs, rhs, start);
  }

  Node *node = primary(&tok, tok);

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

    if (param_ty) {
      check_assign(param_ty, arg, format("argument %d of '%s'", nargs, name));
      if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION)
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

  Type *t1 = ctrl->ty;
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

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | "_Alignof" "(" type-name ")"
//         | "_Alignof" unary
//         | "_Generic" generic-selection
//         | "__builtin_types_compatible_p" "(" type-name, type-name, ")"
//         | "__builtin_reg_class" "(" type-name ")"
//         | "true" | "false" | "nullptr"
//         | ident
//         | str
//         | num
static Node *primary(Token **rest, Token *tok) {
  Token *start = tok;

  if (equal(tok, "(") && equal(tok->next, "{")) {
    // This is a GNU statement expresssion.
    Node *node = new_node(ND_STMT_EXPR, tok);
    node->body = compound_stmt(&tok, tok->next->next, true)->body;
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

    return new_ulong(ty->size, start);
  }

  if (equal(tok, "sizeof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    if (node->ty->kind == TY_VLA)
      return new_var_node(node->ty->vla_size, tok);
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
    return new_num(is_compatible(t1, t2), start);
  }

  if (equal(tok, "__builtin_reg_class")) {
    tok = skip(tok->next, "(");
    Type *ty = typename(&tok, tok);
    *rest = skip(tok, ")");

    if (is_integer(ty) || ty->kind == TY_PTR)
      return new_num(0, start);
    if (is_flonum(ty))
      return new_num(1, start);
    return new_num(2, start);
  }

  // C23's unreachable() in <stddef.h>. Reaching it traps (ud2).
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
  // pointer (mucc gives it type void *).
  if (equal(tok, "true") || equal(tok, "false")) {
    Node *node = new_num(equal(tok, "true"), tok);
    node->ty = ty_bool;
    *rest = tok->next;
    return node;
  }

  if (equal(tok, "nullptr")) {
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
      if (sc->enum_ty)
        return new_num(sc->enum_val, tok);
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
    if (is_flonum(tok->ty)) {
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

//---------- Warnings --------------------------------------------------------

// Two warnings, checked once a function is parsed: unused local
// variables, and non-void functions that can end without a return.
// Neither is given in system headers, after errors, or with -w.

// C library functions that never return. glibc marks them with
// __attribute__((noreturn)), which mucc doesn't read, so they're listed.
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

// Warns about each local variable that is declared but never named
// again, in declaration order (the list is newest first).
static void warn_unused_locals(Obj *var) {
  if (!var)
    return;
  warn_unused_locals(var->next);

  // Parameters and compiler temporaries have no `tok`.
  if (var->tok && !var->is_used && !in_system_header(var->tok))
    warn_tok(var->tok, "unused variable '%s'", var->name);
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
    if (e->kind == ND_UNREACHABLE)
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
    warn_tok(rbrace, "control reaches end of non-void function '%s'", fn->name);
}

//---------- Top level: functions and global variables -----------------------

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
    push_scope(get_ident(ty->name))->type_def = ty;
  }
  return tok;
}

// C23 allows unnamed parameters in a definition, as in `int f(int) {...}`;
// they still get a stack slot, just no name.
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
        // Leaving the scope of cleanup variables runs their cleanups.
        // Entering one would skip the variable's initialization.
        if (!is_scope_of(y->cleanups, x->cleanups))
          error_tok(x->tok, "jump into the scope of a variable with a cleanup");
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
  char *name_str = get_ident(ty->name);
  bool is_noreturn = attr->is_noreturn || da.is_noreturn;

  // aligned(N) on a function only aligns its code, which changes nothing
  // a program can see, so it's ignored.
  if (da.is_packed)
    error_tok(da.layout_tok, "attribute 'packed' is not supported on a function");
  if (da.weak_tok && attr->is_static)
    error_tok(da.weak_tok, "a weak function must not be static");
  no_cleanup(&da, "a function");

  Obj *fn = find_func(name_str);
  if (fn) {
    // Redeclaration
    if (!fn->is_function)
      error_tok(tok, "redeclared as a different kind of symbol");
    if (fn->is_definition && equal(tok, "{"))
      error_tok(tok, "redefinition of %s", name_str);
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
    fn->is_noreturn = is_noreturn || is_libc_noreturn(name_str);
  }

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

  // gcc's `extern inline` (gnu_inline): the body is only for inlining,
  // which mucc doesn't do, so calls go to the definition elsewhere. It's
  // still parsed and checked.
  bool body_only_for_inlining =
    da.gnu_inline_tok && attr->is_inline && attr->is_extern;

  current_fn = fn;
  locals = NULL;
  enter_scope();
  create_param_lvars(ty->params);

  // A buffer for a struct/union return value is passed
  // as the hidden first parameter.
  Type *rty = ty->return_ty;
  if ((rty->kind == TY_STRUCT || rty->kind == TY_UNION) && rty->size > 16)
    new_lvar("", pointer_to(rty));

  fn->params = locals;

  if (ty->is_variadic)
    fn->va_area = new_lvar("__va_area__", array_of(ty_char, 136));
  fn->alloca_bottom = new_lvar("__alloca_size__", pointer_to(ty_char));

  // A parameter like `int m[r][c]` is a pointer to a variable-length row,
  // whose size is computed on entry. This must happen before the body is
  // parsed, since pointer arithmetic on `m` uses that size.
  Node vla_head = {};
  Node *vla_cur = &vla_head;
  for (Type *param = ty->params; param; param = param->next)
    if (param->base && is_variably_modified(param->base)) {
      vla_cur = vla_cur->next =
        new_unary(ND_EXPR_STMT, compute_vla_size(param, tok), tok);
      add_type(vla_cur);
    }

  tok = skip(tok, "{");

  // [https://www.sigbus.info/n1570#6.4.2.2p1] "__func__" is
  // automatically defined as a local variable containing the
  // current function name.
  push_scope("__func__")->var =
    new_string_literal(fn->name, array_of(ty_char, strlen(fn->name) + 1));

  // [GNU] __FUNCTION__ is yet another name of __func__.
  push_scope("__FUNCTION__")->var =
    new_string_literal(fn->name, array_of(ty_char, strlen(fn->name) + 1));

  Token *body = tok;
  fn->body = compound_stmt(&tok, tok, false);
  if (vla_head.next) {
    vla_cur->next = fn->body->body;
    fn->body->body = vla_head.next;
  }
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

static Token *global_variable(Token *tok, Type *basety, VarAttr *attr) {
  bool first = true;

  while (!consume(&tok, tok, ";")) {
    if (!first)
      tok = skip_decl_comma(tok);
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

    Obj *var = new_gvar(get_ident(name), ty);
    var->is_definition = !attr->is_extern;
    var->is_static = attr->is_static;
    var->is_tls = attr->is_tls;
    if (attr->align)
      var->align = attr->align;
    var->align = MAX(var->align, all.align);
    if (all.weak_tok && attr->is_static)
      error_tok(all.weak_tok, "a weak variable must not be static");
    var->is_weak = all.weak_tok != NULL;
    var->section = all.section;
    var->visibility = all.visibility;

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
    else if (!attr->is_extern && !attr->is_tls)
      var->is_tentative = true;
  }
  return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
static bool is_function(Token *tok) {
  if (equal(tok, ";"))
    return false;

  // A lookahead only: the attributes are checked when the real parse runs.
  Type dummy = {};
  Attrs ignored = {};
  Type *ty = declarator(&tok, tok, &dummy, &ignored);
  return ty->kind == TY_FUNC;
}

// Remove redundant tentative definitions.
static void scan_globals(void) {
  Obj head;
  Obj *cur = &head;

  for (Obj *var = globals; var; var = var->next) {
    if (!var->is_tentative) {
      cur = cur->next = var;
      continue;
    }

    // Find another definition of the same identifier.
    Obj *var2 = globals;
    for (; var2; var2 = var2->next)
      if (var != var2 && var2->is_definition && !strcmp(var->name, var2->name))
        break;

    // If there's another definition, the tentative definition
    // is redundant
    if (!var2)
      cur = cur->next = var;
  }

  cur->next = NULL;
  globals = head.next;
}

static void declare_builtin_functions(void) {
  Type *ty = func_type(pointer_to(ty_void));
  ty->params = copy_type(ty_int);
  builtin_alloca = new_gvar("alloca", ty);
  builtin_alloca->is_definition = false;
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
        warn_tok(name, "unknown attribute '%s%s%s' ignored",
                 prefix ? strndup(prefix->loc, prefix->len) : "",
                 prefix ? "::" : "", str);
      }
    }
    tok = tok->next->next;
  }

  cur->next = tok;
  return head.next;
}

// top-level-item = static-assert | typedef | function-definition
//                | global-variable
static Token *top_level_item(Token *tok) {
  if (equal(tok, "_Static_assert"))
    return static_assertion(tok);

  VarAttr attr = {};
  Type *basety = declspec(&tok, tok, &attr);

  if (attr.is_typedef)
    return parse_typedef(tok, basety, &attr);
  if (is_function(tok))
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

  // Remove redundant tentative definitions.
  scan_globals();
  return globals;
}
