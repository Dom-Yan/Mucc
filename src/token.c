//============================================================================
// token.c - STAGE 1 of 4: TOKENIZE
//
// Turns source text into a linked list of tokens (identifiers,
// keywords, punctuators, numbers, strings). Also holds error reporting.
//============================================================================

#include "mucc.h"

//---------- Tokenizer state -------------------------------------------------

// Input file
static File *current_file;

// A list of all input files.
static File **input_files;

// True if the current position is at the beginning of a line
static bool at_bol;

// True if the current position follows a space character
static bool has_space;

// Line number of the current position
static int line_no;

//---------- Error reporting -------------------------------------------------

// Diagnostics look like this, which editors and terminals can jump to:
//
//   foo.c:10:7: error: undefined variable 'y'
//      10 |   x = y + 1;
//         |       ^
//
// Errors in the tokenizer and preprocessor stop mucc at once. Errors
// found while parsing don't: the parser points `error_recovery` at the
// statement or declaration it's in, and error_tok() jumps back there
// after printing. The parser skips that item and carries on, so one run
// reports many errors. cc1 then exits before generating any code.

jmp_buf *error_recovery;
int error_count;

// Past this many, the rest are probably caused by the earlier ones.
#define MAX_ERRORS 20

// After printing an error: resume parsing if we can, else exit.
static noreturn void after_error(void) {
  error_count++;
  if (error_count >= MAX_ERRORS) {
    fprintf(stderr, "mucc: too many errors, stopping\n");
    exit(1);
  }
  if (error_recovery)
    longjmp(*error_recovery, 1);
  exit(1);
}

// Returns "error:" or "warning:", in color if stderr is a terminal.
static char *label(char *kind) {
  if (!isatty(STDERR_FILENO))
    return format("%s:", kind);
  char *color = !strcmp(kind, "error") ? "1;31" : "1;35";
  return format("\033[%sm%s:\033[0m", color, kind);
}

// Reports an error that has no source location and exits.
void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "mucc: %s ", label("error"));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

static void print_diag(char *kind, char *filename, char *input, int line_no,
                       char *loc, char *msg) {
  // Find the line containing `loc`.
  char *line = loc;
  while (input < line && line[-1] != '\n')
    line--;

  char *end = loc;
  while (*end && *end != '\n')
    end++;

  // Columns count characters, not bytes.
  int col = 1;
  for (char *p = line; p < loc; p++)
    if ((*p & 0xC0) != 0x80)
      col++;

  fprintf(stderr, "%s:%d:%d: %s %s\n", filename, line_no, col, label(kind), msg);

  // Print the line, then a caret under `loc`. Tabs are copied so the
  // caret lines up however wide the terminal draws a tab.
  fprintf(stderr, " %4d | %.*s\n", line_no, (int)(end - line), line);
  fprintf(stderr, "      | ");
  for (char *p = line; p < loc;) {
    if (*p == '\t') {
      fputc('\t', stderr);
      p++;
      continue;
    }
    char *q;
    decode_utf8(&q, p);
    fprintf(stderr, "%*s", display_width(p, q - p), "");
    p = q;
  }
  fprintf(stderr, "^\n");
}

void error_at(char *loc, char *fmt, ...) {
  int line_no = 1;
  for (char *p = current_file->contents; p < loc; p++)
    if (*p == '\n')
      line_no++;

  va_list ap;
  va_start(ap, fmt);
  print_diag("error", current_file->name, current_file->contents, line_no,
             loc, vformat(fmt, ap));
  after_error();
}

// A token from a predefined macro like static_assert comes from a
// "<built-in>" pseudo-file with no source line to show, so report the
// place where the macro was used instead.
static Token *user_token(Token *tok) {
  while (tok->origin && !strcmp(tok->file->name, "<built-in>"))
    tok = tok->origin;
  return tok;
}

// The file `tok` is reported in: the name #line or a line marker gave,
// once the preprocessor has passed it on.
static char *tok_filename(Token *tok) {
  return tok->filename ? tok->filename : tok->file->name;
}

void error_tok(Token *tok, char *fmt, ...) {
  tok = user_token(tok);
  va_list ap;
  va_start(ap, fmt);
  print_diag("error", tok_filename(tok), tok->file->contents, tok->line_no,
             tok->loc, vformat(fmt, ap));
  after_error();
}

void warn_tok(Token *tok, char *fmt, ...) {
  if (opt_w)
    return;
  tok = user_token(tok);
  va_list ap;
  va_start(ap, fmt);
  print_diag("warning", tok_filename(tok), tok->file->contents, tok->line_no,
             tok->loc, vformat(fmt, ap));
  va_end(ap);
}

// Reports "expected X". If `tok` starts a new line, X most likely
// belongs at the end of the previous line (a forgotten ';' is the
// classic case), so point there instead of at `tok`.
void error_expected(Token *tok, char *what) {
  if (tok->at_bol && !tok->origin) {
    char *input = tok->file->contents;
    char *p = tok->loc;
    int line_no = tok->line_no;
    while (input < p && isspace(p[-1])) {
      if (p[-1] == '\n')
        line_no--;
      p--;
    }

    if (input < p) {
      print_diag("error", tok_filename(tok), input, line_no, p,
                 format("expected %s", what));
      after_error();
    }
  }

  if (tok->kind == TK_EOF)
    error_tok(tok, "expected %s at end of input", what);
  error_tok(tok, "expected %s before '%.*s'", what, tok->len, tok->loc);
}

//---------- Token matching helpers ------------------------------------------

// Returns true if the current token is `op`. The parser calls this
// constantly and nearly every call fails, usually on the first character,
// so check that before comparing the rest.
bool equal(Token *tok, char *op) {
  return tok->loc[0] == op[0] && memcmp(tok->loc, op, tok->len) == 0 &&
         op[tok->len] == '\0';
}

// Ensure that the current token is `op`.
Token *skip(Token *tok, char *op) {
  if (!equal(tok, op))
    error_expected(tok, format("'%s'", op));
  return tok->next;
}

bool consume(Token **rest, Token *tok, char *str) {
  if (equal(tok, str)) {
    *rest = tok->next;
    return true;
  }
  *rest = tok;
  return false;
}

//---------- Identifiers, punctuators and keywords ---------------------------

// Create a new token.
static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *tok = arena_alloc(sizeof(Token));
  tok->kind = kind;
  tok->loc = start;
  tok->len = end - start;
  tok->file = current_file;
  tok->filename = current_file->display_name;
  tok->line_no = line_no;
  tok->at_bol = at_bol;
  tok->has_space = has_space;

  at_bol = has_space = false;
  return tok;
}

static bool startswith(char *p, char *q) {
  while (*q)
    if (*p++ != *q++)
      return false;
  return true;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(char *start) {
  char *p = start;
  uint32_t c = decode_utf8(&p, p);
  if (!is_ident1(c))
    return 0;

  for (;;) {
    // Fast path: plain ASCII letters, digits, _ and $.
    if (isalnum((unsigned char)*p) || *p == '_' || *p == '$') {
      p++;
      continue;
    }
    if ((unsigned char)*p < 128)
      return p - start;

    char *q;
    c = decode_utf8(&q, p);
    if (!is_ident2(c))
      return p - start;
    p = q;
  }
}

static int from_hex(char c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

// Read a punctuator token from p and returns its length.
static int read_punct(char *p) {
  static char *kw[] = {
    "<<=", ">>=", "...", "==", "!=", "<=", ">=", "->", "+=",
    "-=", "*=", "/=", "++", "--", "%=", "&=", "|=", "^=", "&&",
    "||", "<<", ">>", "##",
  };

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
    if (p[0] == kw[i][0] && startswith(p, kw[i]))
      return strlen(kw[i]);

  return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
  static HashMap map;

  if (map.capacity == 0) {
    static char *kw[] = {
      "return", "if", "else", "for", "while", "int", "sizeof", "char",
      "struct", "union", "short", "long", "void", "typedef", "_Bool",
      "enum", "static", "goto", "break", "continue", "switch", "case",
      "default", "extern", "_Alignof", "_Alignas", "do", "signed",
      "unsigned", "const", "volatile", "auto", "register", "restrict",
      "__restrict", "__restrict__", "_Noreturn", "float", "double",
      "typeof", "asm", "_Thread_local", "__thread", "_Atomic",
      "__attribute__", "_Static_assert",
    };

    // Before C23, these are ordinary names.
    static char *c23_kw[] = {"true", "false", "nullptr", "constexpr"};

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
      hashmap_put(&map, kw[i], (void *)1);
    for (int i = 0; opt_std >= 2023 && i < sizeof(c23_kw) / sizeof(*c23_kw); i++)
      hashmap_put(&map, c23_kw[i], (void *)1);
  }

  return hashmap_get2(&map, tok->loc, tok->len);
}

//---------- String and character literals -----------------------------------

static int read_escaped_char(char **new_pos, char *p) {
  if ('0' <= *p && *p <= '7') {
    // Read an octal number.
    int c = *p++ - '0';
    if ('0' <= *p && *p <= '7') {
      c = (c << 3) + (*p++ - '0');
      if ('0' <= *p && *p <= '7')
        c = (c << 3) + (*p++ - '0');
    }
    *new_pos = p;
    return c;
  }

  if (*p == 'x') {
    // Read a hexadecimal number.
    p++;
    if (!isxdigit(*p))
      error_at(p, "invalid hex escape sequence");

    int c = 0;
    for (; isxdigit(*p); p++)
      c = (c << 4) + from_hex(*p);
    *new_pos = p;
    return c;
  }

  *new_pos = p + 1;

  // Escape sequences are defined using themselves here. E.g.
  // '\n' is implemented using '\n'. This tautological definition
  // works because the compiler that compiles our compiler knows
  // what '\n' actually is. In other words, we "inherit" the ASCII
  // code of '\n' from the compiler that compiles our compiler,
  // so we don't have to teach the actual code here.
  //
  // This fact has huge implications not only for the correctness
  // of the compiler but also for the security of the generated code.
  // For more info, read "Reflections on Trusting Trust" by Ken Thompson.
  // https://github.com/rui314/chibicc/wiki/thompson1984.pdf
  switch (*p) {
  case 'a': return '\a';
  case 'b': return '\b';
  case 't': return '\t';
  case 'n': return '\n';
  case 'v': return '\v';
  case 'f': return '\f';
  case 'r': return '\r';
  // [GNU] \e for the ASCII escape character is a GNU C extension.
  case 'e': return 27;
  default: return *p;
  }
}

// Find a closing double-quote.
static char *string_literal_end(char *p) {
  char *start = p;
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0')
      error_at(start, "unclosed string literal");
    if (*p == '\\')
      p++;
  }
  return p;
}

static Token *read_string_literal(char *start, char *quote) {
  char *end = string_literal_end(quote + 1);
  char *buf = calloc(1, end - quote);
  int len = 0;

  for (char *p = quote + 1; p < end;) {
    if (*p == '\\')
      buf[len++] = read_escaped_char(&p, p + 1);
    else
      buf[len++] = *p++;
  }

  Token *tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty_char, len + 1);
  tok->str = buf;
  return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
static Token *read_utf16_string_literal(char *start, char *quote) {
  char *end = string_literal_end(quote + 1);
  uint16_t *buf = calloc(2, end - start);
  int len = 0;

  for (char *p = quote + 1; p < end;) {
    if (*p == '\\') {
      buf[len++] = read_escaped_char(&p, p + 1);
      continue;
    }

    uint32_t c = decode_utf8(&p, p);
    if (c < 0x10000) {
      // Encode a code point in 2 bytes.
      buf[len++] = c;
    } else {
      // Encode a code point in 4 bytes.
      c -= 0x10000;
      buf[len++] = 0xd800 + ((c >> 10) & 0x3ff);
      buf[len++] = 0xdc00 + (c & 0x3ff);
    }
  }

  Token *tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty_ushort, len + 1);
  tok->str = (char *)buf;
  return tok;
}

// Read a UTF-8-encoded string literal and transcode it in UTF-32.
//
// UTF-32 is a fixed-width encoding for Unicode. Each code point is
// encoded in 4 bytes.
static Token *read_utf32_string_literal(char *start, char *quote, Type *ty) {
  char *end = string_literal_end(quote + 1);
  uint32_t *buf = calloc(4, end - quote);
  int len = 0;

  for (char *p = quote + 1; p < end;) {
    if (*p == '\\')
      buf[len++] = read_escaped_char(&p, p + 1);
    else
      buf[len++] = decode_utf8(&p, p);
  }

  Token *tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty, len + 1);
  tok->str = (char *)buf;
  return tok;
}

static Token *read_char_literal(char *start, char *quote, Type *ty) {
  char *p = quote + 1;
  if (*p == '\0')
    error_at(start, "unclosed char literal");

  int c;
  if (*p == '\\')
    c = read_escaped_char(&p, p + 1);
  else
    c = decode_utf8(&p, p);

  char *end = strchr(p, '\'');
  if (!end)
    error_at(p, "unclosed char literal");

  Token *tok = new_token(TK_NUM, start, end + 1);
  tok->val = c;
  tok->ty = ty;
  return tok;
}

//---------- Numeric literals ------------------------------------------------

// Converts `tok` to an integer constant, reading its text from `s`
// (`len` characters). Returns false if it isn't an integer.
static bool convert_pp_int(Token *tok, char *s, int len) {
  char *p = s;

  // Read a binary, octal, decimal or hexadecimal number.
  int base = 10;
  if (!strncasecmp(p, "0x", 2) && isxdigit(p[2])) {
    p += 2;
    base = 16;
  } else if (!strncasecmp(p, "0b", 2) && (p[2] == '0' || p[2] == '1')) {
    p += 2;
    base = 2;
  } else if (*p == '0') {
    base = 8;
  }

  int64_t val = strtoul(p, &p, base);

  // Read U, L or LL suffixes.
  bool l = false;
  bool u = false;

  if (startswith(p, "LLU") || startswith(p, "LLu") ||
      startswith(p, "llU") || startswith(p, "llu") ||
      startswith(p, "ULL") || startswith(p, "Ull") ||
      startswith(p, "uLL") || startswith(p, "ull")) {
    p += 3;
    l = u = true;
  } else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2)) {
    p += 2;
    l = u = true;
  } else if (startswith(p, "LL") || startswith(p, "ll")) {
    p += 2;
    l = true;
  } else if (*p == 'L' || *p == 'l') {
    p++;
    l = true;
  } else if (*p == 'U' || *p == 'u') {
    p++;
    u = true;
  }

  if (p != s + len)
    return false;

  // Infer a type.
  Type *ty;
  if (base == 10) {
    if (l && u)
      ty = ty_ulong;
    else if (l)
      ty = ty_long;
    else if (u)
      ty = (val >> 32) ? ty_ulong : ty_uint;
    else
      ty = (val >> 31) ? ty_long : ty_int;
  } else {
    if (l && u)
      ty = ty_ulong;
    else if (l)
      ty = (val >> 63) ? ty_ulong : ty_long;
    else if (u)
      ty = (val >> 32) ? ty_ulong : ty_uint;
    else if (val >> 63)
      ty = ty_ulong;
    else if (val >> 32)
      ty = ty_long;
    else if (val >> 31)
      ty = ty_uint;
    else
      ty = ty_int;
  }

  tok->kind = TK_NUM;
  tok->val = val;
  tok->ty = ty;
  return true;
}

// The definition of the numeric literal at the preprocessing stage
// is more relaxed than the definition of that at the later stages.
// In order to handle that, a numeric literal is tokenized as a
// "pp-number" token first and then converted to a regular number
// token after preprocessing.
//
// This function converts a pp-number token to a regular number token.
static void convert_pp_number(Token *tok) {
  // C23 allows ' between digits, as in 1'000'000. Parse a copy without
  // them; the token itself keeps pointing at the source for errors.
  char *s = tok->loc;
  int len = tok->len;
  if (memchr(s, '\'', len)) {
    char *buf = arena_alloc(len + 1);
    int n = 0;
    for (int i = 0; i < len; i++)
      if (s[i] != '\'')
        buf[n++] = s[i];
    s = buf;
    len = n;
  }

  // Try to parse as an integer constant.
  if (convert_pp_int(tok, s, len))
    return;

  // If it's not an integer, it must be a floating point constant.
  char *end;
  long double val = strtold(s, &end);

  Type *ty;
  if (*end == 'f' || *end == 'F') {
    ty = ty_float;
    end++;
  } else if (*end == 'l' || *end == 'L') {
    ty = ty_ldouble;
    end++;
  } else {
    ty = ty_double;
  }

  if (s + len != end)
    error_tok(tok, "invalid numeric constant");

  tok->kind = TK_NUM;
  tok->fval = val;
  tok->ty = ty;
}

void convert_pp_tokens(Token *tok) {
  for (Token *t = tok; t->kind != TK_EOF; t = t->next) {
    if (is_keyword(t))
      t->kind = TK_KEYWORD;
    else if (t->kind == TK_PP_NUM)
      convert_pp_number(t);
  }
}

//---------- Main tokenizer loop ---------------------------------------------

// Re-reads string literal `tok` as a wide string of `basety` characters.
// The result keeps tok's source position; only its type and bytes change.
Token *tokenize_string_literal(Token *tok, Type *basety) {
  Token *t;
  if (basety->size == 2)
    t = read_utf16_string_literal(tok->loc, tok->loc);
  else
    t = read_utf32_string_literal(tok->loc, tok->loc, basety);

  Token *res = arena_alloc(sizeof(Token));
  *res = *tok;
  res->ty = t->ty;
  res->str = t->str;
  return res;
}

// Tokenize a given string and returns new tokens.
Token *tokenize(File *file) {
  current_file = file;

  char *p = file->contents;
  Token head = {};
  Token *cur = &head;

  at_bol = true;
  has_space = false;
  line_no = 1;

  while (*p) {
    // Skip line comments.
    if (startswith(p, "//")) {
      p += 2;
      while (*p != '\n')
        p++;
      has_space = true;
      continue;
    }

    // Skip block comments.
    if (startswith(p, "/*")) {
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "unclosed block comment");
      for (; p < q; p++)
        if (*p == '\n')
          line_no++;
      p = q + 2;
      has_space = true;
      continue;
    }

    // Skip newline.
    if (*p == '\n') {
      p++;
      line_no++;
      at_bol = true;
      has_space = false;
      continue;
    }

    // Skip whitespace characters.
    if (isspace(*p)) {
      p++;
      has_space = true;
      continue;
    }

    // Numeric literal
    if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
      char *q = p++;
      for (;;) {
        if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1]))
          p += 2;
        else if (isalnum(*p) || *p == '.')
          p++;
        else if (*p == '\'' && isalnum(p[-1]) && isalnum(p[1]))
          p++; // C23 digit separator, as in 1'000'000
        else
          break;
      }
      cur = cur->next = new_token(TK_PP_NUM, q, p);
      continue;
    }

    // String literal
    if (*p == '"') {
      cur = cur->next = read_string_literal(p, p);
      p += cur->len;
      continue;
    }

    // UTF-8 string literal
    if (startswith(p, "u8\"")) {
      cur = cur->next = read_string_literal(p, p + 2);
      p += cur->len;
      continue;
    }

    // UTF-16 string literal
    if (startswith(p, "u\"")) {
      cur = cur->next = read_utf16_string_literal(p, p + 1);
      p += cur->len;
      continue;
    }

    // Wide string literal
    if (startswith(p, "L\"")) {
      cur = cur->next = read_utf32_string_literal(p, p + 1, ty_int);
      p += cur->len;
      continue;
    }

    // UTF-32 string literal
    if (startswith(p, "U\"")) {
      cur = cur->next = read_utf32_string_literal(p, p + 1, ty_uint);
      p += cur->len;
      continue;
    }

    // Character literal
    if (*p == '\'') {
      cur = cur->next = read_char_literal(p, p, ty_int);
      cur->val = (char)cur->val;
      p += cur->len;
      continue;
    }

    // UTF-8 character literal (C23): one byte, of type unsigned char
    if (startswith(p, "u8'")) {
      cur = cur->next = read_char_literal(p, p + 2, ty_uchar);
      if (p[3] != '\\' && cur->val > 0x7F)
        error_at(p, "u8 character literal must be a single byte; use a u8 string");
      p += cur->len;
      continue;
    }

    // UTF-16 character literal
    if (startswith(p, "u'")) {
      cur = cur->next = read_char_literal(p, p + 1, ty_ushort);
      cur->val &= 0xffff;
      p += cur->len;
      continue;
    }

    // Wide character literal
    if (startswith(p, "L'")) {
      cur = cur->next = read_char_literal(p, p + 1, ty_int);
      p += cur->len;
      continue;
    }

    // UTF-32 character literal
    if (startswith(p, "U'")) {
      cur = cur->next = read_char_literal(p, p + 1, ty_uint);
      p += cur->len;
      continue;
    }

    // Identifier or keyword
    int ident_len = read_ident(p);
    if (ident_len) {
      cur = cur->next = new_token(TK_IDENT, p, p + ident_len);
      p += cur->len;
      continue;
    }

    // Punctuators
    int punct_len = read_punct(p);
    if (punct_len) {
      cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
      p += cur->len;
      continue;
    }

    error_at(p, "invalid token");
  }

  cur = cur->next = new_token(TK_EOF, p, p);
  return head.next;
}

//---------- Reading source files --------------------------------------------

// Returns the contents of a given file.
static char *read_file(char *path) {
  FILE *fp;

  if (strcmp(path, "-") == 0) {
    // By convention, read from stdin if a given filename is "-".
    fp = stdin;
  } else {
    fp = fopen(path, "r");
    if (!fp)
      return NULL;
  }

  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);

  // Read the entire file.
  for (;;) {
    char buf2[4096];
    int n = fread(buf2, 1, sizeof(buf2), fp);
    if (n == 0)
      break;
    fwrite(buf2, 1, n, out);
  }

  if (fp != stdin)
    fclose(fp);

  // Make sure that the last line is properly terminated with '\n'.
  fflush(out);
  if (buflen == 0 || buf[buflen - 1] != '\n')
    fputc('\n', out);
  fputc('\0', out);
  fclose(out);
  return buf;
}

File **get_input_files(void) {
  return input_files;
}

File *new_file(char *name, int file_no, char *contents) {
  File *file = arena_alloc(sizeof(File));
  file->name = name;
  file->display_name = name;
  file->file_no = file_no;
  file->contents = contents;
  return file;
}

// Replaces \r or \r\n with \n.
static void canonicalize_newline(char *p) {
  int i = 0, j = 0;

  while (p[i]) {
    if (p[i] == '\r' && p[i + 1] == '\n') {
      i += 2;
      p[j++] = '\n';
    } else if (p[i] == '\r') {
      i++;
      p[j++] = '\n';
    } else {
      p[j++] = p[i++];
    }
  }

  p[j] = '\0';
}

// Removes backslashes followed by a newline.
static void remove_backslash_newline(char *p) {
  int i = 0, j = 0;

  // We want to keep the number of newline characters so that
  // the logical line number matches the physical one.
  // This counter maintain the number of newlines we have removed.
  int n = 0;

  while (p[i]) {
    if (p[i] == '\\' && p[i + 1] == '\n') {
      i += 2;
      n++;
    } else if (p[i] == '\n') {
      p[j++] = p[i++];
      for (; n > 0; n--)
        p[j++] = '\n';
    } else {
      p[j++] = p[i++];
    }
  }

  for (; n > 0; n--)
    p[j++] = '\n';
  p[j] = '\0';
}

static uint32_t read_universal_char(char *p, int len) {
  uint32_t c = 0;
  for (int i = 0; i < len; i++) {
    if (!isxdigit(p[i]))
      return 0;
    c = (c << 4) | from_hex(p[i]);
  }
  return c;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static void convert_universal_chars(char *p) {
  char *q = p;

  while (*p) {
    if (startswith(p, "\\u")) {
      uint32_t c = read_universal_char(p + 2, 4);
      if (c) {
        p += 6;
        q += encode_utf8(q, c);
      } else {
        *q++ = *p++;
      }
    } else if (startswith(p, "\\U")) {
      uint32_t c = read_universal_char(p + 2, 8);
      if (c) {
        p += 10;
        q += encode_utf8(q, c);
      } else {
        *q++ = *p++;
      }
    } else if (p[0] == '\\') {
      *q++ = *p++;
      *q++ = *p++;
    } else {
      *q++ = *p++;
    }
  }

  *q = '\0';
}

Token *tokenize_file(char *path) {
  char *p = read_file(path);
  if (!p)
    return NULL;

  // UTF-8 texts may start with a 3-byte "BOM" marker sequence.
  // If exists, just skip them because they are useless bytes.
  // (It is actually not recommended to add BOM markers to UTF-8
  // texts, but it's not uncommon particularly on Windows.)
  if (!memcmp(p, "\xef\xbb\xbf", 3))
    p += 3;

  // Most files need none of these rewrites, and each is a full pass over
  // the file, so first do a fast search for anything to rewrite.
  if (strchr(p, '\r'))
    canonicalize_newline(p);
  char *splice = strstr(p, "\\\n");
  if (splice)
    remove_backslash_newline(splice);
  if (strstr(p, "\\u") || strstr(p, "\\U"))
    convert_universal_chars(p);

  return tokenize(add_input_file(path, p));
}

// Records a file this compilation reads, for the assembler's .file
// directives and for -M dependency lists.
File *add_input_file(char *path, char *contents) {
  static int file_no;
  File *file = new_file(path, file_no + 1, contents);

  input_files = realloc(input_files, sizeof(File *) * (file_no + 2));
  input_files[file_no] = file;
  input_files[file_no + 1] = NULL;
  file_no++;
  return file;
}
