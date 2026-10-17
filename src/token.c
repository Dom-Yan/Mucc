//============================================================================
// token.c - STAGE 1 of 6: TOKENIZE
//
// Turns source text into a linked list of tokens (identifiers,
// keywords, punctuators, numbers, strings).
//============================================================================

#include "mucc.h"
#include <float.h>

//---------- Tokenizer state -------------------------------------------------

// The file being tokenized
static File *current_file;

// A list of all input files.
static File **input_files;

// True if the current position is at the beginning of a line
static bool at_bol;

// True if the current position follows a space character
static bool has_space;

// Line number of the current position
static int line_no;

//---------- Errors in the file being tokenized ------------------------------

// Like error_tok() (see diag.c), at a place in the file being tokenized,
// before there is a token for it.
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

//---------- Token matching helpers ------------------------------------------

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

// Tokens nothing points to any more, those of a skipped #if group (see
// skip_cond_incl() in preprocess.c), are reused before new memory is
// taken: in a big file they are a third of all tokens.
static Token *free_list;

Token *alloc_token(void) {
  Token *tok = free_list;
  if (!tok)
    return arena_alloc(sizeof(Token));
  free_list = tok->next;
  memset(tok, 0, sizeof(Token));
  return tok;
}

// Frees the tokens from `tok` up to `end`.
void free_tokens(Token *tok, Token *end) {
  while (tok != end) {
    Token *next = tok->next;
    tok->next = free_list;
    free_list = tok;
    tok = next;
  }
}

static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *tok = alloc_token();
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

static bool startswith_(char *p, char *q) {
  while (*q)
    if (*p++ != *q++)
      return false;
  return true;
}
// The first character is tested inline, as in equal() (see mucc.h), and
// the same rule holds: no arguments with side effects.
#define startswith(p, q) (*(p) == *(q) && startswith_(p, q))

// Tests for ASCII characters. <ctype.h>'s find their table through a
// function call each time, which the tokenizer's loops feel.
static bool is_digit(char c) {
  return '0' <= c && c <= '9';
}

static bool is_alnum(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || is_digit(c);
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(char *start) {
  char *p = start;
  uint32_t c = (unsigned char)*p < 128 ? *p++ : decode_utf8(&p, p);
  if (!is_ident1(c) || (opt_asm_cpp && c == '$'))
    return 0;

  for (;;) {
    // Fast path: plain ASCII letters, digits, _ and $. In assembly, $ is
    // the immediate prefix, as in `$SYS_write`, so it ends a name.
    if (is_alnum(*p) || *p == '_' || (*p == '$' && !opt_asm_cpp)) {
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

  // Most punctuators are one character. These are the second characters
  // of the longer ones.
  if (!p[1] || !strchr("=<>.+-&|#", p[1]))
    return ispunct(*p) ? 1 : 0;

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
      "unsigned", "const", "volatile", "auto", "__auto_type", "register",
      "restrict", "__restrict", "__restrict__", "_Noreturn", "float", "double",
      "typeof", "__typeof_unqual__", "asm", "_Thread_local", "__thread",
      "_Atomic", "__attribute__", "_Static_assert", "__int128", "_Complex",
      "__complex__", "__complex",
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

    unsigned c = 0;
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

// A character literal, with its opening quote at `quote`. In a plain one
// ('a'), each character is a byte (UTF-8 text is its bytes), and several
// make a multi-character constant, as with gcc: 'RIFF' is 0x52494646.
// One byte is a char, so '\xff' is -1. The other kinds (L'a', u'a', ...)
// read UTF-8 characters and, as gcc does, keep the last of several.
static Token *read_char_literal(char *start, char *quote, Type *ty) {
  bool is_plain = start == quote;
  char *p = quote + 1;
  int64_t val = 0;
  int len = 0;

  while (*p != '\'') {
    if (*p == '\0' || *p == '\n')
      error_at(start, "unclosed char literal");
    int c;
    if (*p == '\\')
      c = read_escaped_char(&p, p + 1);
    else if (is_plain)
      c = (unsigned char)*p++;
    else
      c = decode_utf8(&p, p);
    // (Only the last 4 bytes of a plain one count, so it's kept to 32
    // bits: 9 or more would overflow val.)
    val = is_plain ? (uint32_t)(val << 8 | (c & 0xff)) : c;
    len++;
  }
  if (len == 0)
    error_at(start, "empty character constant");
  if (is_plain)
    val = (len == 1) ? (opt_funsigned_char ? (uint8_t)val : (int8_t)val) : (int32_t)val;

  Token *tok = new_token(TK_NUM, start, p + 1);
  tok->val = val;
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
  bool ll = false;
  bool u = false;

  if (startswith(p, "LLU") || startswith(p, "LLu") ||
      startswith(p, "llU") || startswith(p, "llu") ||
      startswith(p, "ULL") || startswith(p, "Ull") ||
      startswith(p, "uLL") || startswith(p, "ull")) {
    p += 3;
    l = ll = u = true;
  } else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2)) {
    p += 2;
    l = u = true;
  } else if (startswith(p, "LL") || startswith(p, "ll")) {
    p += 2;
    l = ll = true;
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

  // With LL, a long is a long long.
  if (ll)
    ty = ty->is_unsigned ? ty_ullong : ty_llong;

  tok->kind = TK_NUM;
  tok->val = val;
  tok->ty = ty;
  return true;
}

// A floating literal, the `len` characters at `s`, that strtold() made
// `inf`. Cross-compiling for ARM64 on x86-64, ARM64's long double has 113
// bits to this machine's 64, with the same exponents, so its largest
// values, like its LDBL_MAX, are just above this machine's largest: by
// less than 2^-64 of it. Those get this machine's largest, the nearest
// it has, as every long double constant gets this machine's 64 bits. Any
// other overflow is `inf`, as with gcc. (Within 2^-60: this machine can't
// tell ARM64's largest from 0x1p16384, a hair above it.)
static long double ldouble_overflow(char *s, int len, long double val) {
  if (!target_arm64 || LDBL_MANT_DIG >= 113)
    return val;

  // The literal with its exponent one less, so this machine can read it,
  // compared with LDBL_MAX divided the same way
  bool hex = len > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  char *buf = format("%.*s", len, s);
  char *e = strpbrk(buf, hex ? "pP" : "eE");
  if (!e)
    return val;
  long exp = strtol(e + 1, NULL, 10);
  long double less = strtold(format("%.*s%c%ld", (int)(e - buf), buf, *e, exp - 1), NULL);
  long double base = hex ? 2 : 10;
  return less <= LDBL_MAX / base * (1 + 0x1p-60L) ? LDBL_MAX : val;
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
  if (val > LDBL_MAX)
    val = ldouble_overflow(s, end - s, val);

  // [GNU] An `i` or `j` before or after the suffix makes an imaginary
  // constant, as in 2.0i or 1.0fi: a complex number with no real part.
  bool imag = false;
  if (*end == 'i' || *end == 'j') {
    imag = true;
    end++;
  }

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

  if (!imag && (*end == 'i' || *end == 'j')) {
    imag = true;
    end++;
  }

  if (s + len != end)
    error_tok(tok, "invalid numeric constant");

  // A float or double is rounded from the decimal once: through a long
  // double first, 9.3365409393994098e30 would be 1 ULP off.
  if (ty == ty_double)
    val = strtod(s, NULL);
  else if (ty == ty_float)
    val = strtof(s, NULL);

  if (imag)
    ty = complex_type(ty);

  tok->kind = TK_NUM;
  tok->fval = arena_alloc(sizeof(long double));
  *tok->fval = val;
  tok->ty = ty;
}

void convert_pp_tokens(Token *tok) {
  for (Token *t = tok; t->kind != TK_EOF; t = t->next) {
    if (t->kind == TK_IDENT && is_keyword(t))
      t->kind = TK_KEYWORD;
    else if (t->kind == TK_PP_NUM)
      convert_pp_number(t);
    else if (t->kind == TK_PUNCT && t->len == 1 && (*t->loc == '\'' || *t->loc == '"'))
      error_tok(t, "unclosed %s literal", *t->loc == '"' ? "string" : "char");
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

// Whether the quote at p has a closing one later on its line.
static bool closes_on_line(char *p) {
  for (char *q = p + 1; *q && *q != '\n'; q++) {
    if (*q == '\\' && q[1] && q[1] != '\n')
      q++;
    else if (*q == *p)
      return true;
  }
  return false;
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
    // Skip line comments. (Text that isn't a file, like a -D value or
    // a ## paste, may end without a newline.)
    if (p[0] == '/' && p[1] == '/') {
      p += 2;
      while (*p && *p != '\n')
        p++;
      has_space = true;
      continue;
    }

    // Skip block comments.
    if (p[0] == '/' && p[1] == '*') {
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "unclosed block comment");
      for (char *nl = memchr(p, '\n', q - p); nl; nl = memchr(nl + 1, '\n', q - nl - 1))
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
    if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\v' || *p == '\f') {
      p++;
      has_space = true;
      continue;
    }

    // Numeric literal
    if (is_digit(*p) || (*p == '.' && is_digit(p[1]))) {
      char *q = p++;
      for (;;) {
        if (p[0] && p[1] && strchr("eEpP", p[0]) && strchr("+-", p[1]))
          p += 2;
        else if (is_alnum(*p) || *p == '.' || *p == '_')
          p++; // `802_2` is one token, as ETH_P_##802_2 needs
        else if (*p == '\'' && is_alnum(p[-1]) && is_alnum(p[1]))
          p++; // C23 digit separator, as in 1'000'000
        else
          break;
      }
      cur = cur->next = new_token(TK_PP_NUM, q, p);
      continue;
    }

    // A quote that doesn't close on its line is just a character, as with
    // gcc: in `#error don't`, a skipped `#if 0` block, or a `# don't`
    // comment in assembly (.S). In C that survives preprocessing,
    // convert_pp_tokens() reports it.
    if ((*p == '\'' || *p == '"') && !closes_on_line(p)) {
      cur = cur->next = new_token(TK_PUNCT, p, p + 1);
      p++;
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
      cur = cur->next = read_utf32_string_literal(p, p + 1, ty_wchar());
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
      cur = cur->next = read_char_literal(p, p + 1, ty_wchar());
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

    // Assembly has characters C doesn't, like the `@` of `@function`.
    if (opt_asm_cpp) {
      char *q = p;
      decode_utf8(&q, p);
      cur = cur->next = new_token(TK_PUNCT, p, q);
      p = q;
      continue;
    }

    error_at(p, "invalid token");
  }

  // (It starts a line even after text with no newline: see new_eof().)
  cur = cur->next = new_token(TK_EOF, p, p);
  cur->at_bol = true;
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
    fp = open_input_file(path);
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
// The text between splices is moved in pieces, which is much faster than
// a character at a time: this runs from the first splice to the end of
// the file, and a big file like sqlite3.c has splices near its start.
static void remove_backslash_newline(char *p) {
  char *in = p, *out = p;

  // We want to keep the number of newline characters so that
  // the logical line number matches the physical one.
  // This counter maintain the number of newlines we have removed.
  int n = 0;

  for (;;) {
    char *splice = strchr(in, '\\');
    while (splice && splice[1] != '\n')
      splice = strchr(splice + 1, '\\');
    char *end = splice ? splice : in + strlen(in);

    // The removed newlines go back after the next newline.
    if (n) {
      char *nl = memchr(in, '\n', end - in);
      if (nl) {
        memmove(out, in, nl + 1 - in);
        out += nl + 1 - in;
        in = nl + 1;
        memset(out, '\n', n);
        out += n;
        n = 0;
      }
    }
    memmove(out, in, end - in);
    out += end - in;
    if (!splice)
      break;
    in = splice + 2;
    n++;
  }

  memset(out, '\n', n);
  out[n] = '\0';
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

// Replace \u or \U escape sequences with corresponding UTF-8 bytes. An
// escaped backslash, `\\`, is a pair. The text between backslashes is
// moved in pieces, as in remove_backslash_newline(): this runs from the
// line of the first escape to the end of the file, which in sqlite3.c is
// one in a comment near its start.
static void convert_universal_chars(char *p) {
  char *q = p;

  for (;;) {
    char *bs = strchr(p, '\\');
    char *end = bs ? bs : p + strlen(p);
    memmove(q, p, end - p);
    q += end - p;
    p = end;
    if (!bs)
      break;

    int len = (p[1] == 'u') ? 4 : (p[1] == 'U') ? 8 : 0;
    uint32_t c = len ? read_universal_char(p + 2, len) : 0;
    if (c) {
      q += encode_utf8(q, c);
      p += 2 + len;
    } else if (p[1] == '\\') {
      *q++ = *p++;
      *q++ = *p++;
    } else {
      *q++ = *p++;
    }
  }

  *q = '\0';
}

// The start of the line `q` is on, in the text starting at `p`
static char *line_start(char *p, char *q) {
  while (q > p && q[-1] != '\n')
    q--;
  return q;
}

Token *tokenize_file(char *path) {
  char *p = read_file(path);
  if (!p)
    return NULL;

  // UTF-8 texts may start with a 3-byte "BOM" marker sequence.
  // If exists, just skip them because they are useless bytes.
  // (It is actually not recommended to add BOM markers to UTF-8
  // texts, but it's not uncommon particularly on Windows.)
  // (strncmp, not memcmp: an empty file's text is shorter than 3 bytes.)
  if (!strncmp(p, "\xef\xbb\xbf", 3))
    p += 3;

  // Most files need none of these rewrites, and each is a full pass over
  // the file, so first do a fast search for anything to rewrite: the
  // first splice and the first \u or \U escape. Both start with a
  // backslash, which few lines have. An escaped backslash, `\\`, is a
  // pair, as convert_universal_chars() reads it, so "\\u" in a string is
  // no escape. A backslash before a splice may make one once the splice
  // is gone, as in "\\\nu0041", so it counts as one.
  if (strchr(p, '\r'))
    canonicalize_newline(p);
  char *splice = NULL, *ucn = NULL;
  for (char *q = strchr(p, '\\'); q && !(splice && ucn); q = strchr(q + 1, '\\')) {
    if (q[1] == '\n') {
      if (!splice)
        splice = q;
    } else if (q[1] == '\\' && q[2] != '\n') {
      q++;
    } else if ((q[1] == 'u' || q[1] == 'U' || q[1] == '\\') && !ucn) {
      ucn = q;
    }
  }
  if (splice)
    remove_backslash_newline(splice);

  // From the start of the line of the escape, or of the first splice if
  // that's before it: the text before that is as it was.
  if (ucn)
    convert_universal_chars(line_start(p, splice && splice < ucn ? splice : ucn));

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
