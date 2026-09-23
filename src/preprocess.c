//============================================================================
// preprocess.c - STAGE 2 of 4: PREPROCESS
//
// Runs #include, #define, #if and friends over the token list and
// expands macros. The output is a plain token list for the parser.
//============================================================================

#include "mucc.h"

//---------- Data structures and state ---------------------------------------

typedef struct MacroParam MacroParam;
struct MacroParam {
  MacroParam *next;
  char *name;
};

typedef struct MacroArg MacroArg;
struct MacroArg {
  MacroArg *next;
  char *name;
  bool is_va_args;
  Token *tok;
};

typedef Token *macro_handler_fn(Token *);

typedef struct Macro Macro;
struct Macro {
  char *name;
  bool is_objlike; // Object-like or function-like
  MacroParam *params;
  char *va_args_name;
  Token *body;
  macro_handler_fn *handler;
};

// `#if` can be nested, so we use a stack to manage nested `#if`s.
typedef struct CondIncl CondIncl;
struct CondIncl {
  CondIncl *next;
  enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
  Token *tok;
  bool included;
};

typedef struct Hideset Hideset;
struct Hideset {
  Hideset *next;
  char *name;
};

static HashMap macros;
static CondIncl *cond_incl;
static HashMap pragma_once;
static int include_next_idx;

static Token *preprocess2(Token *tok);
static Macro *find_macro(Token *tok);
static char *join_tokens(Token *tok, Token *end);
static bool find_include(char *filename);
static int has_embed(Token **rest, Token *tok);

//---------- Token helpers and hidesets --------------------------------------

static bool is_hash(Token *tok) {
  return tok->at_bol && equal(tok, "#");
}

// Is `tok` `name`, spelled either `name` or `__name__`? (#embed
// parameters and attribute names may be written both ways.)
static bool is_name(Token *tok, char *name) {
  return equal(tok, name) || equal(tok, format("__%s__", name));
}

// Some preprocessor directives such as #endif allow extraneous tokens
// before the newline. Warn about them and skip to the next line.
static Token *skip_line(Token *tok) {
  if (tok->at_bol)
    return tok;
  warn_tok(tok, "extra tokens at end of directive");
  while (!tok->at_bol)
    tok = tok->next;
  return tok;
}

static Token *copy_token(Token *tok) {
  Token *t = arena_alloc(sizeof(Token));
  *t = *tok;
  t->next = NULL;
  return t;
}

static Token *new_eof(Token *tok) {
  Token *t = copy_token(tok);
  t->kind = TK_EOF;
  t->len = 0;
  return t;
}

static Hideset *new_hideset(char *name) {
  Hideset *hs = arena_alloc(sizeof(Hideset));
  hs->name = name;
  return hs;
}

static Hideset *hideset_union(Hideset *hs1, Hideset *hs2) {
  Hideset head = {};
  Hideset *cur = &head;

  for (; hs1; hs1 = hs1->next)
    cur = cur->next = new_hideset(hs1->name);
  cur->next = hs2;
  return head.next;
}

static bool hideset_contains(Hideset *hs, char *s, int len) {
  for (; hs; hs = hs->next)
    if (strlen(hs->name) == len && !strncmp(hs->name, s, len))
      return true;
  return false;
}

static Hideset *hideset_intersection(Hideset *hs1, Hideset *hs2) {
  Hideset head = {};
  Hideset *cur = &head;

  for (; hs1; hs1 = hs1->next)
    if (hideset_contains(hs2, hs1->name, strlen(hs1->name)))
      cur = cur->next = new_hideset(hs1->name);
  return head.next;
}

static Token *add_hideset(Token *tok, Hideset *hs) {
  Token head = {};
  Token *cur = &head;

  for (; tok; tok = tok->next) {
    Token *t = copy_token(tok);
    t->hideset = hideset_union(t->hideset, hs);
    cur = cur->next = t;
  }
  return head.next;
}

// Append tok2 to the end of tok1.
static Token *append(Token *tok1, Token *tok2) {
  if (tok1->kind == TK_EOF)
    return tok2;

  Token head = {};
  Token *cur = &head;

  for (; tok1->kind != TK_EOF; tok1 = tok1->next)
    cur = cur->next = copy_token(tok1);
  cur->next = tok2;
  return head.next;
}

//---------- Skipping #if blocks ---------------------------------------------

static Token *skip_cond_incl2(Token *tok) {
  while (tok->kind != TK_EOF) {
    if (is_hash(tok) &&
        (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
         equal(tok->next, "ifndef"))) {
      tok = skip_cond_incl2(tok->next->next);
      continue;
    }
    if (is_hash(tok) && equal(tok->next, "endif"))
      return tok->next->next;
    tok = tok->next;
  }
  return tok;
}

// Skip until next `#else`, `#elif` or `#endif`.
// Nested `#if` and `#endif` are skipped.
static Token *skip_cond_incl(Token *tok) {
  while (tok->kind != TK_EOF) {
    if (is_hash(tok) &&
        (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
         equal(tok->next, "ifndef"))) {
      tok = skip_cond_incl2(tok->next->next);
      continue;
    }

    if (is_hash(tok) &&
        (equal(tok->next, "elif") || equal(tok->next, "elifdef") ||
         equal(tok->next, "elifndef") || equal(tok->next, "else") ||
         equal(tok->next, "endif")))
      break;
    tok = tok->next;
  }
  return tok;
}

//---------- Making new tokens -----------------------------------------------

// Double-quote a given string and returns it.
static char *quote_string(char *str) {
  int bufsize = 3;
  for (int i = 0; str[i]; i++) {
    if (str[i] == '\\' || str[i] == '"')
      bufsize++;
    bufsize++;
  }

  char *buf = calloc(1, bufsize);
  char *p = buf;
  *p++ = '"';
  for (int i = 0; str[i]; i++) {
    if (str[i] == '\\' || str[i] == '"')
      *p++ = '\\';
    *p++ = str[i];
  }
  *p++ = '"';
  *p++ = '\0';
  return buf;
}

static Token *new_str_token(char *str, Token *tmpl) {
  char *buf = quote_string(str);
  return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(Token **rest, Token *tok) {
  Token head = {};
  Token *cur = &head;

  for (; !tok->at_bol; tok = tok->next)
    cur = cur->next = copy_token(tok);

  cur->next = new_eof(tok);
  *rest = tok;
  return head.next;
}

static Token *new_num_token(int val, Token *tmpl) {
  char *buf = format("%d\n", val);
  return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

//---------- #if expressions -------------------------------------------------

static Token *read_const_expr(Token **rest, Token *tok) {
  tok = copy_line(rest, tok);

  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    // "defined(foo)" or "defined foo" becomes "1" if macro "foo"
    // is defined. Otherwise "0".
    if (equal(tok, "defined")) {
      Token *start = tok;
      bool has_paren = consume(&tok, tok->next, "(");

      if (tok->kind != TK_IDENT)
        error_tok(start, "macro name must be an identifier");
      Macro *m = find_macro(tok);
      tok = tok->next;

      if (has_paren)
        tok = skip(tok, ")");

      cur = cur->next = new_num_token(m ? 1 : 0, start);
      continue;
    }

    // C23: "__has_include(<foo.h>)" or "__has_include("foo.h")" becomes
    // "1" if #include would find the header. Otherwise "0".
    if (equal(tok, "__has_include")) {
      Token *start = tok;
      tok = skip(tok->next, "(");

      bool found;
      if (tok->kind == TK_STR) {
        char *name = strndup(tok->loc + 1, tok->len - 2);
        char *dir = dirname(strdup(start->file->name));
        found = file_exists(format("%s/%s", dir, name)) || find_include(name);
        tok = tok->next;
      } else {
        Token *lt = tok;
        tok = skip(tok, "<");
        while (!equal(tok, ">")) {
          if (tok->kind == TK_EOF)
            error_tok(lt, "expected '>'");
          tok = tok->next;
        }
        found = find_include(join_tokens(lt->next, tok));
        tok = tok->next;
      }

      tok = skip(tok, ")");
      cur = cur->next = new_num_token(found, start);
      continue;
    }

    // C23: "__has_embed(...)" (see "#embed").
    if (equal(tok, "__has_embed")) {
      Token *start = tok;
      cur = cur->next = new_num_token(has_embed(&tok, tok), start);
      continue;
    }

    // C23: "__has_c_attribute(x)" is the version of standard attribute x
    // that mucc accepts. It accepts them all (and ignores most).
    if (equal(tok, "__has_c_attribute")) {
      Token *start = tok;
      tok = skip(tok->next, "(");
      Token *name = tok;
      int val = 0;
      if (equal(tok->next, ")")) {
        if (is_name(name, "deprecated") || is_name(name, "fallthrough") ||
            is_name(name, "maybe_unused"))
          val = 201904;
        else if (is_name(name, "nodiscard"))
          val = 202003;
        else if (is_name(name, "noreturn") || equal(name, "_Noreturn"))
          val = 202202;
        else if (is_name(name, "unsequenced") || is_name(name, "reproducible"))
          val = 202207;
      }
      while (!equal(tok, ")")) {
        if (tok->kind == TK_EOF)
          error_tok(start, "expected ')'");
        tok = tok->next;
      }
      tok = tok->next;
      cur = cur->next = new_num_token(val, start);
      continue;
    }

    cur = cur->next = tok;
    tok = tok->next;
  }

  cur->next = tok;
  return head.next;
}

// Evaluates `expr`, an EOF-terminated copy of the tokens of an #if
// expression (or of an #embed limit). `start` is for error messages.
static long eval_pp_expr(Token *start, Token *expr) {
  expr = preprocess2(expr);

  if (expr->kind == TK_EOF)
    error_tok(start, "no expression");

  // [https://www.sigbus.info/n1570#6.10.1p4] The standard requires
  // we replace remaining non-macro identifiers with "0" before
  // evaluating a constant expression. For example, `#if foo` is
  // equivalent to `#if 0` if foo is not defined.
  // (C23 makes `true` an exception: it's 1.)
  for (Token *t = expr; t->kind != TK_EOF; t = t->next) {
    if (t->kind == TK_IDENT) {
      Token *next = t->next;
      *t = *new_num_token(equal(t, "true") ? 1 : 0, t);
      t->next = next;
    }
  }

  // Convert pp-numbers to regular numbers
  convert_pp_tokens(expr);

  Token *rest2;
  long val = const_expr(&rest2, expr);
  if (rest2->kind != TK_EOF)
    error_tok(rest2, "extra token");
  return val;
}

// Read and evaluate the constant expression of an #if or #elif.
static long eval_const_expr(Token **rest, Token *tok) {
  return eval_pp_expr(tok, read_const_expr(rest, tok->next));
}

static CondIncl *push_cond_incl(Token *tok, bool included) {
  CondIncl *ci = arena_alloc(sizeof(CondIncl));
  ci->next = cond_incl;
  ci->ctx = IN_THEN;
  ci->tok = tok;
  ci->included = included;
  cond_incl = ci;
  return ci;
}

//---------- Macro definitions -----------------------------------------------

static Macro *find_macro(Token *tok) {
  if (tok->kind != TK_IDENT)
    return NULL;
  return hashmap_get2(&macros, tok->loc, tok->len);
}

static Macro *add_macro(char *name, bool is_objlike, Token *body) {
  Macro *m = arena_alloc(sizeof(Macro));
  m->name = name;
  m->is_objlike = is_objlike;
  m->body = body;
  hashmap_put(&macros, name, m);
  return m;
}

static MacroParam *read_macro_params(Token **rest, Token *tok, char **va_args_name) {
  MacroParam head = {};
  MacroParam *cur = &head;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok = skip(tok, ",");

    if (equal(tok, "...")) {
      *va_args_name = "__VA_ARGS__";
      *rest = skip(tok->next, ")");
      return head.next;
    }

    if (tok->kind != TK_IDENT)
      error_tok(tok, "expected an identifier");

    if (equal(tok->next, "...")) {
      *va_args_name = strndup(tok->loc, tok->len);
      *rest = skip(tok->next->next, ")");
      return head.next;
    }

    MacroParam *m = arena_alloc(sizeof(MacroParam));
    m->name = strndup(tok->loc, tok->len);
    cur = cur->next = m;
    tok = tok->next;
  }

  *rest = tok->next;
  return head.next;
}

static void read_macro_definition(Token **rest, Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "macro name must be an identifier");
  char *name = strndup(tok->loc, tok->len);
  tok = tok->next;

  if (!tok->has_space && equal(tok, "(")) {
    // Function-like macro
    char *va_args_name = NULL;
    MacroParam *params = read_macro_params(&tok, tok->next, &va_args_name);

    Macro *m = add_macro(name, false, copy_line(rest, tok));
    m->params = params;
    m->va_args_name = va_args_name;
  } else {
    // Object-like macro
    add_macro(name, true, copy_line(rest, tok));
  }
}

//---------- Macro arguments -------------------------------------------------

static MacroArg *read_macro_arg_one(Token **rest, Token *tok, bool read_rest) {
  Token head = {};
  Token *cur = &head;
  int level = 0;

  for (;;) {
    if (level == 0 && equal(tok, ")"))
      break;
    if (level == 0 && !read_rest && equal(tok, ","))
      break;

    if (tok->kind == TK_EOF)
      error_tok(tok, "premature end of input");

    if (equal(tok, "("))
      level++;
    else if (equal(tok, ")"))
      level--;

    cur = cur->next = copy_token(tok);
    tok = tok->next;
  }

  cur->next = new_eof(tok);

  MacroArg *arg = arena_alloc(sizeof(MacroArg));
  arg->tok = head.next;
  *rest = tok;
  return arg;
}

static MacroArg *
read_macro_args(Token **rest, Token *tok, MacroParam *params, char *va_args_name) {
  Token *start = tok;
  tok = tok->next->next;

  MacroArg head = {};
  MacroArg *cur = &head;

  MacroParam *pp = params;
  for (; pp; pp = pp->next) {
    if (cur != &head)
      tok = skip(tok, ",");
    cur = cur->next = read_macro_arg_one(&tok, tok, false);
    cur->name = pp->name;
  }

  if (va_args_name) {
    MacroArg *arg;
    if (equal(tok, ")")) {
      arg = arena_alloc(sizeof(MacroArg));
      arg->tok = new_eof(tok);
    } else {
      if (pp != params)
        tok = skip(tok, ",");
      arg = read_macro_arg_one(&tok, tok, true);
    }
    arg->name = va_args_name;;
    arg->is_va_args = true;
    cur = cur->next = arg;
  } else if (pp) {
    error_tok(start, "too many arguments");
  }

  skip(tok, ")");
  *rest = tok;
  return head.next;
}

static MacroArg *find_arg(MacroArg *args, Token *tok) {
  for (MacroArg *ap = args; ap; ap = ap->next)
    if (tok->len == strlen(ap->name) && !strncmp(tok->loc, ap->name, tok->len))
      return ap;
  return NULL;
}

//---------- Macro expansion (#, ## and substitution) ------------------------

// Concatenates all tokens in `tok` and returns a new string.
static char *join_tokens(Token *tok, Token *end) {
  // Compute the length of the resulting token.
  int len = 1;
  for (Token *t = tok; t != end && t->kind != TK_EOF; t = t->next) {
    if (t != tok && t->has_space)
      len++;
    len += t->len;
  }

  char *buf = calloc(1, len);

  // Copy token texts.
  int pos = 0;
  for (Token *t = tok; t != end && t->kind != TK_EOF; t = t->next) {
    if (t != tok && t->has_space)
      buf[pos++] = ' ';
    strncpy(buf + pos, t->loc, t->len);
    pos += t->len;
  }
  buf[pos] = '\0';
  return buf;
}

// Concatenates all tokens in `arg` and returns a new string token.
// This function is used for the stringizing operator (#).
static Token *stringize(Token *hash, Token *arg) {
  // Create a new string token. We need to set some value to its
  // source location for error reporting function, so we use a macro
  // name token as a template.
  char *s = join_tokens(arg, NULL);
  return new_str_token(s, hash);
}

// Concatenate two tokens to create a new token.
static Token *paste(Token *lhs, Token *rhs) {
  // Paste the two tokens.
  char *buf = format("%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);

  // Tokenize the resulting string.
  Token *tok = tokenize(new_file(lhs->file->name, lhs->file->file_no, buf));
  if (tok->next->kind != TK_EOF)
    error_tok(lhs, "pasting forms '%s', an invalid token", buf);
  return tok;
}

static bool has_varargs(MacroArg *args) {
  for (MacroArg *ap = args; ap; ap = ap->next)
    if (!strcmp(ap->name, "__VA_ARGS__"))
      return ap->tok->kind != TK_EOF;
  return false;
}

// Replace func-like macro parameters with given arguments.
static Token *subst(Token *tok, MacroArg *args) {
  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    // "#" followed by a parameter is replaced with stringized actuals.
    if (equal(tok, "#")) {
      MacroArg *arg = find_arg(args, tok->next);
      if (!arg)
        error_tok(tok->next, "'#' is not followed by a macro parameter");
      cur = cur->next = stringize(tok, arg->tok);
      tok = tok->next->next;
      continue;
    }

    // [GNU] If __VA_ARG__ is empty, `,##__VA_ARGS__` is expanded
    // to the empty token list. Otherwise, its expaned to `,` and
    // __VA_ARGS__.
    if (equal(tok, ",") && equal(tok->next, "##")) {
      MacroArg *arg = find_arg(args, tok->next->next);
      if (arg && arg->is_va_args) {
        if (arg->tok->kind == TK_EOF) {
          tok = tok->next->next->next;
        } else {
          cur = cur->next = copy_token(tok);
          tok = tok->next->next;
        }
        continue;
      }
    }

    if (equal(tok, "##")) {
      if (cur == &head)
        error_tok(tok, "'##' cannot appear at start of macro expansion");

      if (tok->next->kind == TK_EOF)
        error_tok(tok, "'##' cannot appear at end of macro expansion");

      MacroArg *arg = find_arg(args, tok->next);
      if (arg) {
        if (arg->tok->kind != TK_EOF) {
          *cur = *paste(cur, arg->tok);
          for (Token *t = arg->tok->next; t->kind != TK_EOF; t = t->next)
            cur = cur->next = copy_token(t);
        }
        tok = tok->next->next;
        continue;
      }

      *cur = *paste(cur, tok->next);
      tok = tok->next->next;
      continue;
    }

    MacroArg *arg = find_arg(args, tok);

    if (arg && equal(tok->next, "##")) {
      Token *rhs = tok->next->next;

      if (arg->tok->kind == TK_EOF) {
        MacroArg *arg2 = find_arg(args, rhs);
        if (arg2) {
          for (Token *t = arg2->tok; t->kind != TK_EOF; t = t->next)
            cur = cur->next = copy_token(t);
        } else {
          cur = cur->next = copy_token(rhs);
        }
        tok = rhs->next;
        continue;
      }

      for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next)
        cur = cur->next = copy_token(t);
      tok = tok->next;
      continue;
    }

    // If __VA_ARG__ is empty, __VA_OPT__(x) is expanded to the
    // empty token list. Otherwise, __VA_OPT__(x) is expanded to x.
    if (equal(tok, "__VA_OPT__") && equal(tok->next, "(")) {
      MacroArg *arg = read_macro_arg_one(&tok, tok->next->next, true);
      if (has_varargs(args))
        for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next)
          cur = cur->next = t;
      tok = skip(tok, ")");
      continue;
    }

    // Handle a macro token. Macro arguments are completely macro-expanded
    // before they are substituted into a macro body.
    if (arg) {
      Token *t = preprocess2(arg->tok);
      t->at_bol = tok->at_bol;
      t->has_space = tok->has_space;
      for (; t->kind != TK_EOF; t = t->next)
        cur = cur->next = copy_token(t);
      tok = tok->next;
      continue;
    }

    // Handle a non-macro token.
    cur = cur->next = copy_token(tok);
    tok = tok->next;
    continue;
  }

  cur->next = tok;
  return head.next;
}

// If tok is a macro, expand it and return true.
// Otherwise, do nothing and return false.
static bool expand_macro(Token **rest, Token *tok) {
  if (hideset_contains(tok->hideset, tok->loc, tok->len))
    return false;

  Macro *m = find_macro(tok);
  if (!m)
    return false;

  // Built-in dynamic macro application such as __LINE__
  if (m->handler) {
    *rest = m->handler(tok);
    (*rest)->next = tok->next;
    return true;
  }

  // Object-like macro application
  if (m->is_objlike) {
    Hideset *hs = hideset_union(tok->hideset, new_hideset(m->name));
    Token *body = add_hideset(m->body, hs);
    for (Token *t = body; t->kind != TK_EOF; t = t->next)
      t->origin = tok;
    *rest = append(body, tok->next);
    (*rest)->at_bol = tok->at_bol;
    (*rest)->has_space = tok->has_space;
    return true;
  }

  // If a funclike macro token is not followed by an argument list,
  // treat it as a normal identifier.
  if (!equal(tok->next, "("))
    return false;

  // Function-like macro application
  Token *macro_token = tok;
  MacroArg *args = read_macro_args(&tok, tok, m->params, m->va_args_name);
  Token *rparen = tok;

  // Tokens that consist a func-like macro invocation may have different
  // hidesets, and if that's the case, it's not clear what the hideset
  // for the new tokens should be. We take the interesection of the
  // macro token and the closing parenthesis and use it as a new hideset
  // as explained in the Dave Prossor's algorithm.
  Hideset *hs = hideset_intersection(macro_token->hideset, rparen->hideset);
  hs = hideset_union(hs, new_hideset(m->name));

  Token *body = subst(m->body, args);
  body = add_hideset(body, hs);
  for (Token *t = body; t->kind != TK_EOF; t = t->next)
    t->origin = macro_token;
  *rest = append(body, tok->next);
  (*rest)->at_bol = macro_token->at_bol;
  (*rest)->has_space = macro_token->has_space;
  return true;
}

//---------- #include --------------------------------------------------------

char *search_include_paths(char *filename) {
  if (filename[0] == '/')
    return filename;

  static HashMap cache;
  char *cached = hashmap_get(&cache, filename);
  if (cached)
    return cached;

  // Search a file from the include paths.
  for (int i = 0; i < include_paths.len; i++) {
    char *path = format("%s/%s", include_paths.data[i], filename);
    if (!file_exists(path))
      continue;
    hashmap_put(&cache, filename, path);
    include_next_idx = i + 1;
    return path;
  }
  return NULL;
}

// Would #include <filename> find a file? Unlike search_include_paths(),
// this doesn't cache or change where #include_next continues from.
// Used by __has_include.
static bool find_include(char *filename) {
  if (filename[0] == '/')
    return file_exists(filename);
  for (int i = 0; i < include_paths.len; i++)
    if (file_exists(format("%s/%s", include_paths.data[i], filename)))
      return true;
  return false;
}

static char *search_include_next(char *filename) {
  for (; include_next_idx < include_paths.len; include_next_idx++) {
    char *path = format("%s/%s", include_paths.data[include_next_idx], filename);
    if (file_exists(path))
      return path;
  }
  return NULL;
}

// Read an #include argument.
static char *read_include_filename(Token **rest, Token *tok, bool *is_dquote) {
  // Pattern 1: #include "foo.h"
  if (tok->kind == TK_STR) {
    // A double-quoted filename for #include is a special kind of
    // token, and we don't want to interpret any escape sequences in it.
    // For example, "\f" in "C:\foo" is not a formfeed character but
    // just two non-control characters, backslash and f.
    // So we don't want to use token->str.
    *is_dquote = true;
    *rest = skip_line(tok->next);
    return strndup(tok->loc + 1, tok->len - 2);
  }

  // Pattern 2: #include <foo.h>
  if (equal(tok, "<")) {
    // Reconstruct a filename from a sequence of tokens between
    // "<" and ">".
    Token *start = tok;

    // Find closing ">".
    for (; !equal(tok, ">"); tok = tok->next)
      if (tok->at_bol || tok->kind == TK_EOF)
        error_tok(tok, "expected '>'");

    *is_dquote = false;
    *rest = skip_line(tok->next);
    return join_tokens(start->next, tok);
  }

  // Pattern 3: #include FOO
  // In this case FOO must be macro-expanded to either
  // a single string token or a sequence of "<" ... ">".
  if (tok->kind == TK_IDENT) {
    Token *tok2 = preprocess2(copy_line(rest, tok));
    return read_include_filename(&tok2, tok2, is_dquote);
  }

  error_tok(tok, "expected a filename");
}

// Detect the following "include guard" pattern.
//
//   #ifndef FOO_H
//   #define FOO_H
//   ...
//   #endif
static char *detect_include_guard(Token *tok) {
  // Detect the first two lines.
  if (!is_hash(tok) || !equal(tok->next, "ifndef"))
    return NULL;
  tok = tok->next->next;

  if (tok->kind != TK_IDENT)
    return NULL;

  char *macro = strndup(tok->loc, tok->len);
  tok = tok->next;

  if (!is_hash(tok) || !equal(tok->next, "define") || !equal(tok->next->next, macro))
    return NULL;

  // The #endif matching the #ifndef must be the last thing in the file.
  // Skip nested #if blocks whole, so their #endifs aren't mistaken for it.
  while (tok->kind != TK_EOF) {
    if (!is_hash(tok)) {
      tok = tok->next;
      continue;
    }

    Token *dir = tok->next;
    if (equal(dir, "if") || equal(dir, "ifdef") || equal(dir, "ifndef")) {
      tok = skip_cond_incl2(dir->next);
      continue;
    }

    if (equal(dir, "endif"))
      return dir->next->kind == TK_EOF ? macro : NULL;

    // An #else of the guard means the file has content when it's defined.
    if (equal(dir, "else") || equal(dir, "elif") ||
        equal(dir, "elifdef") || equal(dir, "elifndef"))
      return NULL;

    tok = dir;
  }
  return NULL;
}

static Token *include_file(Token *tok, char *path, Token *filename_tok) {
  // Check for "#pragma once"
  if (hashmap_get(&pragma_once, path))
    return tok;

  // If we read the same file before, and if the file was guarded
  // by the usual #ifndef ... #endif pattern, we may be able to
  // skip the file without opening it.
  static HashMap include_guards;
  char *guard_name = hashmap_get(&include_guards, path);
  if (guard_name && hashmap_get(&macros, guard_name))
    return tok;

  Token *tok2 = tokenize_file(path);
  if (!tok2)
    error_tok(filename_tok, "%s: cannot open file: %s", path, strerror(errno));

  guard_name = detect_include_guard(tok2);
  if (guard_name)
    hashmap_put(&include_guards, path, guard_name);

  return append(tok2, tok);
}

//---------- #embed (C23) ----------------------------------------------------

// #embed "file" (or <file>) turns into the file's bytes as a list of
// numbers, for an initializer:
//
//   static const unsigned char icon[] = {
//   #embed "icon.png"
//   };
//
// After the name it takes these parameters (each may also be spelled
// __name__):
//   limit(N)          use at most N bytes
//   prefix(tokens)    put these before the bytes, if there are any
//   suffix(tokens)    put these after the bytes, if there are any
//   if_empty(tokens)  put these instead, if there are no bytes

typedef struct {
  char *name;      // as written
  Token *name_tok; // for error messages
  char *path;      // where it was found, or NULL
  long limit;      // -1 for none
  Token *prefix;   // each an EOF-terminated token list, or NULL
  Token *suffix;
  Token *if_empty;
  Token *unknown;  // the first parameter mucc doesn't know, if any
} EmbedArgs;

// Copies the tokens inside a parameter's parentheses (which may nest),
// ending the copy with EOF. `tok` is the "(".
static Token *read_embed_param(Token **rest, Token *tok) {
  Token *start = tok;
  tok = skip(tok, "(");

  Token head = {};
  Token *cur = &head;
  int depth = 0;
  while (depth > 0 || !equal(tok, ")")) {
    if (tok->kind == TK_EOF)
      error_tok(start, "expected ')'");
    if (equal(tok, "("))
      depth++;
    else if (equal(tok, ")"))
      depth--;
    cur = cur->next = copy_token(tok);
    tok = tok->next;
  }

  cur->next = new_eof(tok);
  *rest = tok->next;
  return head.next;
}

// Finds the file the way #include would.
static char *find_embed(char *name, bool is_dquote, Token *hash) {
  if (name[0] == '/')
    return file_exists(name) ? name : NULL;

  if (is_dquote) {
    char *path = format("%s/%s", dirname(strdup(hash->file->name)), name);
    if (file_exists(path))
      return path;
  }

  for (int i = 0; i < include_paths.len; i++) {
    char *path = format("%s/%s", include_paths.data[i], name);
    if (file_exists(path))
      return path;
  }
  return NULL;
}

// Reads the file name and parameters from `tok`, an EOF-terminated list.
static EmbedArgs read_embed_args(Token *tok, Token *hash) {
  EmbedArgs args = {.limit = -1};
  bool is_dquote;
  args.name_tok = tok;

  if (tok->kind == TK_STR) {
    args.name = strndup(tok->loc + 1, tok->len - 2);
    is_dquote = true;
    tok = tok->next;
  } else if (equal(tok, "<")) {
    Token *lt = tok;
    while (!equal(tok, ">")) {
      if (tok->kind == TK_EOF)
        error_tok(lt, "expected '>'");
      tok = tok->next;
    }
    args.name = join_tokens(lt->next, tok);
    is_dquote = false;
    tok = tok->next;
  } else {
    error_tok(tok, "expected a file name");
  }
  args.path = find_embed(args.name, is_dquote, hash);

  while (tok->kind != TK_EOF) {
    Token *param = tok;
    if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
      error_tok(tok, "expected an #embed parameter");

    if (is_name(tok, "limit")) {
      Token *expr = read_embed_param(&tok, tok->next);
      args.limit = eval_pp_expr(param, expr);
      if (args.limit < 0)
        error_tok(param, "#embed limit can't be negative");
    } else if (is_name(tok, "prefix")) {
      args.prefix = read_embed_param(&tok, tok->next);
    } else if (is_name(tok, "suffix")) {
      args.suffix = read_embed_param(&tok, tok->next);
    } else if (is_name(tok, "if_empty")) {
      args.if_empty = read_embed_param(&tok, tok->next);
    } else {
      // Unknown, maybe vendor::name. Skip it and any (...).
      if (!args.unknown)
        args.unknown = param;
      tok = tok->next;
      while (equal(tok, ":") || tok->kind == TK_IDENT)
        tok = tok->next;
      if (equal(tok, "("))
        read_embed_param(&tok, tok);
    }
  }
  return args;
}

// Reads up to `limit` bytes (or all, if -1) of the file.
static unsigned char *read_embed_file(EmbedArgs *args, size_t *len) {
  FILE *fp = fopen(args->path, "rb");
  if (!fp)
    error_tok(args->name_tok, "%s: cannot open file: %s", args->path, strerror(errno));

  size_t cap = 4096;
  size_t n = 0;
  unsigned char *buf = malloc(cap);

  while (args->limit < 0 || n < args->limit) {
    if (n == cap)
      buf = realloc(buf, cap *= 2);
    size_t want = cap - n;
    if (args->limit >= 0 && want > args->limit - n)
      want = args->limit - n;
    size_t got = fread(buf + n, 1, want, fp);
    if (got == 0)
      break;
    n += got;
  }

  fclose(fp);
  *len = n;
  return buf;
}

// Puts token list `list` (EOF-terminated, may be NULL) in front of `rest`.
static Token *splice(Token *list, Token *rest) {
  if (!list || list->kind == TK_EOF)
    return rest;
  Token *t = list;
  while (t->next->kind != TK_EOF)
    t = t->next;
  t->next = rest;
  return list;
}

// Handles `#embed ...` (`tok` is just after "embed") and returns the
// tokens it becomes, followed by the rest of the input.
static Token *embed(Token *hash, Token *tok) {
  Token *rest;
  Token *line = copy_line(&rest, tok);

  // #embed MACRO: the name comes from a macro.
  if (line->kind == TK_IDENT)
    line = preprocess2(line);

  EmbedArgs args = read_embed_args(line, hash);
  if (args.unknown)
    error_tok(args.unknown, "unknown #embed parameter '%.*s'",
              args.unknown->len, args.unknown->loc);
  if (!args.path)
    error_tok(args.name_tok, "%s: cannot open file: No such file or directory",
              args.name);

  size_t len;
  unsigned char *bytes = read_embed_file(&args, &len);

  // List each embedded file once in -M output.
  static HashMap embedded;
  if (!hashmap_get(&embedded, args.path)) {
    hashmap_put(&embedded, args.path, (void *)1);
    add_input_file(args.path, "");
  }

  if (len == 0)
    return splice(args.if_empty, rest);

  // Write the bytes as "1,2,3" and tokenize that. Each byte takes at
  // most 4 characters ("255,").
  char *text = malloc(len * 4 + 1);
  char *p = text;
  for (size_t i = 0; i < len; i++) {
    if (i > 0)
      *p++ = ',';
    int b = bytes[i];
    if (b >= 100)
      *p++ = '0' + b / 100;
    if (b >= 10)
      *p++ = '0' + b / 10 % 10;
    *p++ = '0' + b % 10;
  }
  *p = '\0';
  free(bytes);

  Token *nums = tokenize(new_file(hash->file->name, hash->file->file_no, text));
  return splice(args.prefix, splice(nums, splice(args.suffix, rest)));
}

// __has_embed(...) in #if: 0 if the file isn't found (or a parameter
// isn't supported), 2 if it's empty (or limit(0)), otherwise 1. These are
// __STDC_EMBED_NOT_FOUND__, __STDC_EMBED_EMPTY__ and __STDC_EMBED_FOUND__.
static int has_embed(Token **rest, Token *tok) {
  Token *start = tok;
  Token *inner = read_embed_param(rest, tok->next);
  EmbedArgs args = read_embed_args(inner, start);
  if (!args.path || args.unknown)
    return 0;

  // An empty file or limit(0) embeds nothing.
  args.limit = args.limit < 0 ? 1 : MIN(args.limit, 1);
  size_t len;
  free(read_embed_file(&args, &len));
  return len ? 1 : 2;
}

//---------- Directives: the main preprocessor loop --------------------------

// Read #line arguments
static void read_line_marker(Token **rest, Token *tok) {
  Token *start = tok;
  tok = preprocess(copy_line(rest, tok));

  if (tok->kind != TK_NUM || tok->ty->kind != TY_INT)
    error_tok(tok, "invalid line marker");
  start->file->line_delta = tok->val - start->line_no;

  tok = tok->next;
  if (tok->kind == TK_EOF)
    return;

  if (tok->kind != TK_STR)
    error_tok(tok, "filename expected");
  start->file->display_name = tok->str;
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(Token *tok) {
  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    // If it is a macro, expand it.
    if (expand_macro(&tok, tok))
      continue;

    // Pass through if it is not a "#".
    if (!is_hash(tok)) {
      tok->line_delta = tok->file->line_delta;
      tok->filename = tok->file->display_name;
      cur = cur->next = tok;
      tok = tok->next;
      continue;
    }

    Token *start = tok;
    tok = tok->next;

    if (equal(tok, "include")) {
      bool is_dquote;
      char *filename = read_include_filename(&tok, tok->next, &is_dquote);

      if (filename[0] != '/' && is_dquote) {
        char *path = format("%s/%s", dirname(strdup(start->file->name)), filename);
        if (file_exists(path)) {
          tok = include_file(tok, path, start->next->next);
          continue;
        }
      }

      char *path = search_include_paths(filename);
      tok = include_file(tok, path ? path : filename, start->next->next);
      continue;
    }

    if (equal(tok, "include_next")) {
      bool ignore;
      char *filename = read_include_filename(&tok, tok->next, &ignore);
      char *path = search_include_next(filename);
      tok = include_file(tok, path ? path : filename, start->next->next);
      continue;
    }

    if (equal(tok, "embed")) {
      tok = embed(start, tok->next);
      continue;
    }

    if (equal(tok, "define")) {
      read_macro_definition(&tok, tok->next);
      continue;
    }

    if (equal(tok, "undef")) {
      tok = tok->next;
      if (tok->kind != TK_IDENT)
        error_tok(tok, "macro name must be an identifier");
      undef_macro(strndup(tok->loc, tok->len));
      tok = skip_line(tok->next);
      continue;
    }

    if (equal(tok, "if")) {
      long val = eval_const_expr(&tok, tok);
      push_cond_incl(start, val);
      if (!val)
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "ifdef")) {
      bool defined = find_macro(tok->next);
      push_cond_incl(tok, defined);
      tok = skip_line(tok->next->next);
      if (!defined)
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "ifndef")) {
      bool defined = find_macro(tok->next);
      push_cond_incl(tok, !defined);
      tok = skip_line(tok->next->next);
      if (defined)
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "elif")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #elif");
      cond_incl->ctx = IN_ELIF;

      if (!cond_incl->included && eval_const_expr(&tok, tok))
        cond_incl->included = true;
      else
        tok = skip_cond_incl(tok);
      continue;
    }

    // C23: #elifdef X and #elifndef X are #elif defined(X) and
    // #elif !defined(X).
    if (equal(tok, "elifdef") || equal(tok, "elifndef")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #%.*s", tok->len, tok->loc);
      cond_incl->ctx = IN_ELIF;

      bool want = equal(tok, "elifdef") ? !!find_macro(tok->next) : !find_macro(tok->next);
      if (!cond_incl->included && want) {
        cond_incl->included = true;
        tok = skip_line(tok->next->next);
      } else {
        tok = skip_cond_incl(tok);
      }
      continue;
    }

    if (equal(tok, "else")) {
      if (!cond_incl || cond_incl->ctx == IN_ELSE)
        error_tok(start, "stray #else");
      cond_incl->ctx = IN_ELSE;
      tok = skip_line(tok->next);

      if (cond_incl->included)
        tok = skip_cond_incl(tok);
      continue;
    }

    if (equal(tok, "endif")) {
      if (!cond_incl)
        error_tok(start, "stray #endif");
      cond_incl = cond_incl->next;
      tok = skip_line(tok->next);
      continue;
    }

    if (equal(tok, "line")) {
      read_line_marker(&tok, tok->next);
      continue;
    }

    if (tok->kind == TK_PP_NUM) {
      read_line_marker(&tok, tok);
      continue;
    }

    if (equal(tok, "pragma") && equal(tok->next, "once")) {
      hashmap_put(&pragma_once, tok->file->name, (void *)1);
      tok = skip_line(tok->next->next);
      continue;
    }

    if (equal(tok, "pragma")) {
      do {
        tok = tok->next;
      } while (!tok->at_bol);
      continue;
    }

    // #error stops with the rest of the line as the message. #warning
    // (C23) prints it and carries on.
    if (equal(tok, "error") || equal(tok, "warning")) {
      Token *dir = tok;
      char *msg = join_tokens(copy_line(&tok, tok->next), NULL);
      char *text = format("#%.*s%s%s", dir->len, dir->loc, *msg ? " " : "", msg);
      if (equal(dir, "error"))
        error_tok(dir, "%s", text);
      warn_tok(dir, "%s", text);
      continue;
    }

    // `#`-only line is legal. It's called a null directive.
    if (tok->at_bol)
      continue;

    error_tok(tok, "invalid preprocessor directive");
  }

  cur->next = tok;
  return head.next;
}

//---------- Predefined and builtin macros -----------------------------------

void define_macro(char *name, char *buf) {
  Token *tok = tokenize(new_file("<built-in>", 1, buf));
  add_macro(name, true, tok);
}

void undef_macro(char *name) {
  hashmap_delete(&macros, name);
}

static Macro *add_builtin(char *name, macro_handler_fn *fn) {
  Macro *m = add_macro(name, true, NULL);
  m->handler = fn;
  return m;
}

static Token *file_macro(Token *tmpl) {
  while (tmpl->origin)
    tmpl = tmpl->origin;
  return new_str_token(tmpl->file->display_name, tmpl);
}

static Token *line_macro(Token *tmpl) {
  while (tmpl->origin)
    tmpl = tmpl->origin;
  int i = tmpl->line_no + tmpl->file->line_delta;
  return new_num_token(i, tmpl);
}

// __COUNTER__ is expanded to serial values starting from 0.
static Token *counter_macro(Token *tmpl) {
  static int i = 0;
  return new_num_token(i++, tmpl);
}

// __TIMESTAMP__ is expanded to a string describing the last
// modification time of the current file. E.g.
// "Fri Jul 24 01:32:50 2020"
static Token *timestamp_macro(Token *tmpl) {
  struct stat st;
  if (stat(tmpl->file->name, &st) != 0)
    return new_str_token("??? ??? ?? ??:??:?? ????", tmpl);

  char buf[30];
  ctime_r(&st.st_mtime, buf);
  buf[24] = '\0';
  return new_str_token(buf, tmpl);
}

static Token *base_file_macro(Token *tmpl) {
  return new_str_token(base_file, tmpl);
}

// __DATE__ is expanded to the current date, e.g. "May 17 2020".
static char *format_date(struct tm *tm) {
  static char mon[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
  };

  return format("\"%s %2d %d\"", mon[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
}

// __TIME__ is expanded to the current time, e.g. "13:34:03".
static char *format_time(struct tm *tm) {
  return format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void init_macros(void) {
  // Define predefined macros
  define_macro("_LP64", "1");
  define_macro("__C99_MACRO_WITH_VA_ARGS", "1");
  define_macro("__ELF__", "1");
  define_macro("__LP64__", "1");
  define_macro("__SIZEOF_DOUBLE__", "8");
  define_macro("__SIZEOF_FLOAT__", "4");
  define_macro("__SIZEOF_INT__", "4");
  define_macro("__SIZEOF_LONG_DOUBLE__", "16");
  define_macro("__SIZEOF_LONG_LONG__", "8");
  define_macro("__SIZEOF_LONG__", "8");
  define_macro("__SIZEOF_POINTER__", "8");
  define_macro("__SIZEOF_PTRDIFF_T__", "8");
  define_macro("__SIZEOF_SHORT__", "2");
  define_macro("__SIZEOF_SIZE_T__", "8");
  define_macro("__SIZE_TYPE__", "unsigned long");
  define_macro("__STDC_HOSTED__", "1");
  define_macro("__STDC_NO_COMPLEX__", "1");
  define_macro("__STDC_UTF_16__", "1");
  define_macro("__STDC_UTF_32__", "1");
  define_macro("__STDC_VERSION__", "202311L");
  define_macro("__STDC__", "1");
  define_macro("__USER_LABEL_PREFIX__", "");
  define_macro("__alignof__", "_Alignof");
  define_macro("__amd64", "1");
  define_macro("__amd64__", "1");
  define_macro("__mucc__", "1");
  define_macro("__const__", "const");
  define_macro("__gnu_linux__", "1");
  define_macro("__inline__", "inline");
  define_macro("__linux", "1");
  define_macro("__linux__", "1");
  define_macro("__signed__", "signed");
  define_macro("__typeof__", "typeof");
  define_macro("__unix", "1");
  define_macro("__unix__", "1");
  define_macro("__volatile__", "volatile");
  define_macro("__x86_64", "1");
  define_macro("__x86_64__", "1");
  define_macro("linux", "1");
  define_macro("unix", "1");

  // C23 keywords that are new spellings of C11 ones, and the GNU
  // spellings of asm. (true, false and nullptr are real keywords.)
  define_macro("alignas", "_Alignas");
  define_macro("alignof", "_Alignof");
  define_macro("bool", "_Bool");
  define_macro("static_assert", "_Static_assert");
  define_macro("thread_local", "_Thread_local");
  define_macro("typeof_unqual", "typeof");
  define_macro("__asm__", "asm");
  define_macro("__asm", "asm");

  // Lets `#if defined(__has_include)` etc. work. The operators themselves
  // are handled in read_const_expr().
  define_macro("__has_include", "__has_include");
  define_macro("__has_embed", "__has_embed");
  define_macro("__has_c_attribute", "__has_c_attribute");

  // What __has_embed returns (see "#embed").
  define_macro("__STDC_EMBED_NOT_FOUND__", "0");
  define_macro("__STDC_EMBED_FOUND__", "1");
  define_macro("__STDC_EMBED_EMPTY__", "2");

  add_builtin("__FILE__", file_macro);
  add_builtin("__LINE__", line_macro);
  add_builtin("__COUNTER__", counter_macro);
  add_builtin("__TIMESTAMP__", timestamp_macro);
  add_builtin("__BASE_FILE__", base_file_macro);

  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  define_macro("__DATE__", format_date(tm));
  define_macro("__TIME__", format_time(tm));
}

//---------- Joining adjacent string literals --------------------------------

typedef enum {
  STR_NONE, STR_UTF8, STR_UTF16, STR_UTF32, STR_WIDE,
} StringKind;

static StringKind getStringKind(Token *tok) {
  // tok->loc points into the source and isn't NUL-terminated after the
  // prefix, so compare the prefix and the opening quote only.
  if (!strncmp(tok->loc, "u8\"", 3))
    return STR_UTF8;

  switch (tok->loc[0]) {
  case '"': return STR_NONE;
  case 'u': return STR_UTF16;
  case 'U': return STR_UTF32;
  case 'L': return STR_WIDE;
  }
  unreachable();
}

// Concatenate adjacent string literals into a single string literal
// as per the C spec.
static void join_adjacent_string_literals(Token *tok) {
  // First pass: If regular string literals are adjacent to wide
  // string literals, regular string literals are converted to a wide
  // type before concatenation. In this pass, we do the conversion.
  for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
    if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
      tok1 = tok1->next;
      continue;
    }

    StringKind kind = getStringKind(tok1);
    Type *basety = tok1->ty->base;

    for (Token *t = tok1->next; t->kind == TK_STR; t = t->next) {
      StringKind k = getStringKind(t);
      if (kind == STR_NONE) {
        kind = k;
        basety = t->ty->base;
      } else if (k != STR_NONE && kind != k) {
        error_tok(t, "unsupported non-standard concatenation of string literals");
      }
    }

    if (basety->size > 1)
      for (Token *t = tok1; t->kind == TK_STR; t = t->next)
        if (t->ty->base->size == 1)
          *t = *tokenize_string_literal(t, basety);

    while (tok1->kind == TK_STR)
      tok1 = tok1->next;
  }

  // Second pass: concatenate adjacent string literals.
  for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
    if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
      tok1 = tok1->next;
      continue;
    }

    Token *tok2 = tok1->next;
    while (tok2->kind == TK_STR)
      tok2 = tok2->next;

    int len = tok1->ty->array_len;
    for (Token *t = tok1->next; t != tok2; t = t->next)
      len = len + t->ty->array_len - 1;

    char *buf = calloc(tok1->ty->base->size, len);

    int i = 0;
    for (Token *t = tok1; t != tok2; t = t->next) {
      memcpy(buf + i, t->str, t->ty->size);
      i = i + t->ty->size - t->ty->base->size;
    }

    *tok1 = *copy_token(tok1);
    tok1->ty = array_of(tok1->ty->base, len);
    tok1->str = buf;
    tok1->next = tok2;
    tok1 = tok2;
  }
}

//---------- Entry point -----------------------------------------------------

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
  tok = preprocess2(tok);
  if (cond_incl)
    error_tok(cond_incl->tok, "unterminated conditional directive");
  convert_pp_tokens(tok);
  join_adjacent_string_literals(tok);

  for (Token *t = tok; t; t = t->next)
    t->line_no += t->line_delta;
  return tok;
}
