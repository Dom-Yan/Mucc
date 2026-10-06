//============================================================================
// type.c - STAGE 3 of 6: PARSE (types)
//
// The C type system: built-in types, type constructors, and add_type(),
// which works out the type of every AST node.
//============================================================================

#include "mucc.h"

//---------- Built-in types --------------------------------------------------

Type *ty_void = &(Type){TY_VOID, 1, 1};
Type *ty_bool = &(Type){TY_BOOL, 1, 1};

Type *ty_char = &(Type){TY_CHAR, 1, 1};
Type *ty_schar = &(Type){TY_CHAR, 1, 1, .is_distinct = true};
Type *ty_short = &(Type){TY_SHORT, 2, 2};
Type *ty_int = &(Type){TY_INT, 4, 4};
Type *ty_long = &(Type){TY_LONG, 8, 8};
Type *ty_llong = &(Type){TY_LONG, 8, 8, .is_distinct = true};

Type *ty_uchar = &(Type){TY_CHAR, 1, 1, true};
Type *ty_ushort = &(Type){TY_SHORT, 2, 2, true};
Type *ty_uint = &(Type){TY_INT, 4, 4, true};
Type *ty_ulong = &(Type){TY_LONG, 8, 8, true};
Type *ty_ullong = &(Type){TY_LONG, 8, 8, true, .is_distinct = true};
Type *ty_int128 = &(Type){TY_INT128, 16, 16};
Type *ty_uint128 = &(Type){TY_INT128, 16, 16, true};

Type *ty_float = &(Type){TY_FLOAT, 4, 4};
Type *ty_double = &(Type){TY_DOUBLE, 8, 8};
Type *ty_ldouble = &(Type){TY_LDOUBLE, 16, 16};

//---------- Type predicates -------------------------------------------------

static Type *new_type(TypeKind kind, int size, int align) {
  Type *ty = arena_alloc(sizeof(Type));
  ty->kind = kind;
  ty->size = size;
  ty->align = align;
  return ty;
}

bool is_integer(Type *ty) {
  TypeKind k = ty->kind;
  return k == TY_BOOL || k == TY_CHAR || k == TY_SHORT ||
         k == TY_INT  || k == TY_LONG || k == TY_INT128 || k == TY_ENUM;
}

bool is_int128(Type *ty) {
  return ty->kind == TY_INT128;
}

bool is_complex(Type *ty) {
  return ty->kind == TY_STRUCT && ty->is_complex;
}

bool is_flonum(Type *ty) {
  return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
         ty->kind == TY_LDOUBLE;
}

bool is_numeric(Type *ty) {
  return is_integer(ty) || is_flonum(ty);
}

// Counts the scalars in `ty` that are long doubles (into *nld) and that
// aren't (into *nother).
static void count_ldouble(Type *ty, int *nld, int *nother) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (Member *mem = ty->members; mem; mem = mem->next)
      count_ldouble(mem->ty, nld, nother);
  } else if (ty->kind == TY_ARRAY) {
    if (ty->array_len > 0)
      count_ldouble(ty->base, nld, nother);
  } else if (ty->kind == TY_LDOUBLE) {
    (*nld)++;
  } else {
    (*nother)++;
  }
}

// Does `ty` have a member at an offset its type's alignment doesn't
// divide, as a packed struct may? The psABI passes and returns such a
// struct in memory.
bool has_unaligned_member(Type *ty) {
  if (ty->kind == TY_ARRAY)
    return has_unaligned_member(ty->base);
  if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
    return false;
  for (Member *mem = ty->members; mem; mem = mem->next)
    if (!mem->is_bitfield &&
        (mem->offset % mem->ty->align || has_unaligned_member(mem->ty)))
      return true;
  return false;
}

// Does struct or union `ty` hold a long double? Then, in 16 bytes or
// less (the psABI's X87 class), it's passed on the stack, not in
// registers.
bool has_ldouble(Type *ty) {
  int nld = 0, nother = 0;
  count_ldouble(ty, &nld, &nother);
  return nld > 0;
}

// Is a struct or union returned through a hidden pointer? One larger than
// 16 bytes is, and so is one holding a long double and anything else. One
// holding only long doubles comes back in %st0, and a long double
// _Complex in %st0 and %st1.
bool is_ret_in_memory(Type *ty) {
  if (is_complex(ty))
    return false;
  if (ty->size > 16 || has_unaligned_member(ty))
    return true;
  int nld = 0, nother = 0;
  count_ldouble(ty, &nld, &nother);
  return nld > 0 && nother > 0;
}

// Are t1 and t2 compatible, leaving aside their own qualifiers (not those
// of what they point to)? Copies (see copy_type) lead back to the type
// they were made from.
static bool is_compatible_unqual(Type *t1, Type *t2);
static Type *enum_int_type(Type *ty);

// Is `ty` the type of a member of transparent union `u`? As gcc has it, a
// function with such a parameter is compatible with one that has the
// member's type there: glibc's `accept` is an
// `int (*)(int, struct sockaddr *, socklen_t *)`.
static bool transparent_member(Type *u, Type *ty) {
  if (!u->is_transparent)
    return false;
  for (Member *mem = u->members; mem; mem = mem->next)
    if (is_compatible_unqual(mem->ty, ty))
      return true;
  return false;
}

// `int f()` before C23: nothing is said of its parameters.
static bool is_unprototyped(Type *fn) {
  return fn->is_oldstyle;
}

// Can `fn`'s parameters take arguments as `int f()` passes them, after
// the default argument promotions (C11 6.7.6.3p15)?
static bool takes_promoted_args(Type *fn) {
  if (fn->is_variadic)
    return false;
  for (Type *p = fn->params; p; p = p->next)
    if (p->kind == TY_FLOAT || p->kind == TY_BOOL || (is_integer(p) && p->size < 4))
      return false;
  return true;
}

static bool is_compatible_unqual(Type *t1, Type *t2) {
  if (t1 == t2)
    return true;

  if (t1->origin)
    return is_compatible_unqual(t1->origin, t2);

  if (t2->origin)
    return is_compatible_unqual(t1, t2->origin);

  // An enum is compatible with the integer type it is stored as.
  if (t1->kind == TY_ENUM && t1->size >= 4 && t2->kind != TY_ENUM && is_integer(t2))
    return is_compatible_unqual(enum_int_type(t1), t2);
  if (t2->kind == TY_ENUM && t2->size >= 4 && t1->kind != TY_ENUM && is_integer(t1))
    return is_compatible_unqual(t1, enum_int_type(t2));

  // A variable-length array is compatible with any array of a compatible
  // element type.
  if ((t1->kind == TY_VLA || t1->kind == TY_ARRAY) && (t2->kind == TY_VLA || t2->kind == TY_ARRAY) &&
      (t1->kind == TY_VLA || t2->kind == TY_VLA))
    return is_compatible(t1->base, t2->base);

  if (t1->kind != t2->kind)
    return false;

  switch (t1->kind) {
  case TY_VOID:
  case TY_BOOL:
    return true;
  case TY_CHAR:
  case TY_SHORT:
  case TY_INT:
  case TY_LONG:
  case TY_INT128:
    return t1->is_unsigned == t2->is_unsigned &&
           t1->is_distinct == t2->is_distinct;
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
    return true;
  case TY_PTR:
    return is_compatible(t1->base, t2->base);
  case TY_FUNC: {
    // A parameter's own qualifiers, as in `int f(const int x)`, and the
    // return type's, don't count.
    if (!is_compatible_unqual(t1->return_ty, t2->return_ty))
      return false;
    if (is_unprototyped(t1) || is_unprototyped(t2))
      return (is_unprototyped(t1) || takes_promoted_args(t1)) &&
             (is_unprototyped(t2) || takes_promoted_args(t2));
    if (t1->is_variadic != t2->is_variadic)
      return false;

    Type *p1 = t1->params;
    Type *p2 = t2->params;
    for (; p1 && p2; p1 = p1->next, p2 = p2->next)
      if (!is_compatible_unqual(p1, p2) && !transparent_member(p1, p2) &&
          !transparent_member(p2, p1))
        return false;
    return p1 == NULL && p2 == NULL;
  }
  case TY_ARRAY:
    // Arrays are compatible if their lengths match or one is unknown.
    if (!is_compatible(t1->base, t2->base))
      return false;
    return t1->array_len < 0 || t2->array_len < 0 ||
           t1->array_len == t2->array_len;
  }
  return false;
}

// C11 6.2.7: compatible types also have the same qualifiers, so neither
// `const int` and `int` nor `int *` and `const int *` are.
bool is_compatible(Type *t1, Type *t2) {
  return t1->is_const == t2->is_const && t1->is_volatile == t2->is_volatile &&
         is_compatible_unqual(t1, t2);
}

//---------- Type constructors -----------------------------------------------

Type *copy_type(Type *ty) {
  Type *ret = arena_alloc(sizeof(Type));
  *ret = *ty;
  ret->origin = ty;
  return ret;
}

// `ty` with const and volatile added, if they're true. Qualifying an
// array qualifies its elements. A struct or union that isn't complete yet
// keeps its qualified copies in `variants`, so complete_variants() can
// complete them too.
Type *qualified(Type *ty, bool is_const, bool is_volatile) {
  if ((!is_const || ty->is_const) && (!is_volatile || ty->is_volatile))
    return ty;

  if (ty->kind == TY_ARRAY) {
    Type *ret = array_of(qualified(ty->base, is_const, is_volatile), ty->array_len);
    ret->origin = ty;
    return ret;
  }

  Type *ret = copy_type(ty);
  ret->is_const |= is_const;
  ret->is_volatile |= is_volatile;
  if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->size < 0) {
    ret->variants = ty->variants;
    ty->variants = ret;
  }
  return ret;
}

// `ty` without its own qualifiers, as a value of it is (C11 6.3.2.1p2).
Type *unqual(Type *ty) {
  if (!ty->is_const && !ty->is_volatile)
    return ty;
  Type *ret = copy_type(ty);
  ret->is_const = ret->is_volatile = false;
  return ret;
}

// Struct or union `ty` was just completed and laid out: so are the
// qualified copies made of it before.
void complete_variants(Type *ty) {
  Type *variants = ty->variants;
  ty->variants = NULL;
  for (Type *v = variants, *next; v; v = next) {
    next = v->variants;
    Type saved = *v;
    *v = *ty;
    v->is_const = saved.is_const;
    v->is_volatile = saved.is_volatile;
    v->is_atomic = saved.is_atomic;
    v->origin = saved.origin;
    v->name = saved.name;
    v->name_pos = saved.name_pos;
    v->variants = NULL;
  }
}

Type *pointer_to(Type *base) {
  Type *ty = new_type(TY_PTR, 8, 8);
  ty->base = base;
  ty->is_unsigned = true;
  return ty;
}

Type *func_type(Type *return_ty) {
  // The C spec disallows sizeof(<function type>), but
  // GCC allows that and the expression is evaluated to 1.
  Type *ty = new_type(TY_FUNC, 1, 1);
  ty->return_ty = return_ty;
  return ty;
}

Type *array_of(Type *base, int len) {
  Type *ty = new_type(TY_ARRAY, base->size * len, base->align);
  ty->base = base;
  ty->array_len = len;
  return ty;
}

Type *vla_of(Type *base, Node *len) {
  Type *ty = new_type(TY_VLA, 8, 8);
  ty->base = base;
  ty->vla_len = len;
  return ty;
}

Type *enum_type(void) {
  return new_type(TY_ENUM, 4, 4);
}

Type *struct_type(void) {
  return new_type(TY_STRUCT, 0, 1);
}

// _Complex float, double or long double: laid out as a struct of the real
// and imaginary parts, which is also how the psABI passes it (and
// returns it, but for long double; see cgen.c). The parts have no names,
// so `z.re` is an error; parser.c reaches them through `members`. There
// is one type for each part type, so they compare as the same.
Type *complex_type(Type *part) {
  static Type *types[3];
  int i = part->kind == TY_FLOAT ? 0 : part->kind == TY_DOUBLE ? 1 : 2;
  if (types[i])
    return types[i];

  Type *ty = new_type(TY_STRUCT, part->size * 2, part->align);
  ty->is_complex = true;
  Member *re = arena_alloc(sizeof(Member));
  Member *im = arena_alloc(sizeof(Member));
  re->ty = im->ty = part;
  re->align = im->align = part->align;
  re->unit = im->unit = part->size;
  im->idx = 1;
  im->offset = part->size;
  re->next = im;
  ty->members = re;
  return types[i] = ty;
}

Type *complex_part(Type *ty) {
  return ty->members->ty;
}

// The real type of `ty`: a complex type's part type, or `ty` itself
static Type *real_type(Type *ty) {
  return is_complex(ty) ? complex_part(ty) : ty;
}

//---------- Typing AST nodes ------------------------------------------------

// The integer type an enum of 4 or 8 bytes is stored as: int or unsigned,
// or for a wider one (`enum { BIG = 1LL << 40 }`, `enum : long long`),
// long or long long
static Type *enum_int_type(Type *ty) {
  if (ty->size == 8 && ty->is_distinct)
    return ty->is_unsigned ? ty_ullong : ty_llong;
  if (ty->size == 8)
    return ty->is_unsigned ? ty_ulong : ty_long;
  return ty->is_unsigned ? ty_uint : ty_int;
}

static Type *get_common_type(Type *ty1, Type *ty2) {
  if (ty1->base)
    return pointer_to(ty1->base);

  if (ty1->kind == TY_FUNC)
    return pointer_to(ty1);
  if (ty2->kind == TY_FUNC)
    return pointer_to(ty2);

  // With a complex operand, the result is complex, of the real types'
  // common type.
  if (is_complex(ty1) || is_complex(ty2))
    return complex_type(get_common_type(real_type(ty1), real_type(ty2)));

  if (ty1->kind == TY_LDOUBLE || ty2->kind == TY_LDOUBLE)
    return ty_ldouble;
  if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
    return ty_double;
  if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
    return ty_float;

  // Integer promotion: an enum is the integer type it is stored as.
  if (ty1->size < 4)
    ty1 = ty_int;
  else if (ty1->kind == TY_ENUM)
    ty1 = enum_int_type(ty1);
  if (ty2->size < 4)
    ty2 = ty_int;
  else if (ty2->kind == TY_ENUM)
    ty2 = enum_int_type(ty2);

  if (ty1->size != ty2->size)
    return unqual(ty1->size < ty2->size ? ty2 : ty1);

  // long long outranks long, so with it the result is long long, unsigned
  // if either is.
  if (ty1->size == 8 && ty1->is_distinct != ty2->is_distinct)
    return ty1->is_unsigned || ty2->is_unsigned ? ty_ullong : ty_llong;

  return unqual(ty2->is_unsigned ? ty2 : ty1);
}

// For many binary operators, we implicitly promote operands so that
// both operands have the same type. Any integral type smaller than
// int is always promoted to int. If the type of one operand is larger
// than the other's (e.g. "long" vs. "int"), the smaller operand will
// be promoted to match with the other.
//
// This operation is called the "usual arithmetic conversion".
static void usual_arith_conv(Node **lhs, Node **rhs) {
  Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);
  *lhs = new_cast(*lhs, ty);
  *rhs = new_cast(*rhs, ty);
}

// A condition, or an operand of an arithmetic, comparison or logical
// operator, must be a number or a pointer: `if (s)` or `s * 2` on a
// struct s is an error, not code that reads its address.
void check_scalar(Node *node) {
  add_type(node);
  Type *ty = node->ty;
  if (ty && (ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_VOID) &&
      !is_complex(ty))
    error_tok(node->tok, "'%s' used where a scalar is required", type_name(ty));
}

void add_type(Node *node) {
  if (!node || node->ty)
    return;

  add_type(node->lhs);
  add_type(node->rhs);
  add_type(node->cond);
  add_type(node->then);
  add_type(node->els);
  add_type(node->init);
  add_type(node->inc);

  for (Node *n = node->body; n; n = n->next)
    add_type(n);
  for (Node *n = node->args; n; n = n->next)
    add_type(n);

  switch (node->kind) {
  case ND_NUM:
    node->ty = ty_int;
    return;
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_MOD:
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR:
    check_scalar(node->lhs);
    check_scalar(node->rhs);
    // A complex result; a real operand stays real, as C wants for
    // `z * 2.0` (see lower_complex() in parser.c).
    if (is_complex(node->lhs->ty) || is_complex(node->rhs->ty)) {
      if (node->kind != ND_ADD && node->kind != ND_SUB && node->kind != ND_MUL &&
          node->kind != ND_DIV)
        error_tok(node->tok, "invalid operands to a complex number");
      node->ty = get_common_type(node->lhs->ty, node->rhs->ty);
      return;
    }
    usual_arith_conv(&node->lhs, &node->rhs);
    node->ty = node->lhs->ty;
    return;
  case ND_NEG: {
    check_scalar(node->lhs);
    Type *ty = get_common_type(ty_int, node->lhs->ty);
    node->lhs = new_cast(node->lhs, ty);
    node->ty = ty;
    return;
  }
  case ND_ASSIGN:
    if (node->lhs->ty->kind == TY_ARRAY)
      error_tok(node->lhs->tok, "not an lvalue");
    if (node->lhs->ty->kind != TY_STRUCT || is_complex(node->lhs->ty))
      node->rhs = new_cast(node->rhs, node->lhs->ty);
    node->ty = unqual(node->lhs->ty);
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    check_scalar(node->lhs);
    check_scalar(node->rhs);
    if ((node->kind == ND_LT || node->kind == ND_LE) &&
        (is_complex(node->lhs->ty) || is_complex(node->rhs->ty)))
      error_tok(node->tok, "complex numbers can't be compared with < or >");
    usual_arith_conv(&node->lhs, &node->rhs);
    node->ty = ty_int;
    return;
  case ND_FUNCALL:
    node->ty = unqual(node->func_ty->return_ty);
    return;
  case ND_NOT:
  case ND_LOGOR:
  case ND_LOGAND:
    check_scalar(node->lhs);
    if (node->rhs)
      check_scalar(node->rhs);
    node->ty = ty_int;
    return;
  case ND_BITNOT:
  case ND_SHL:
  case ND_SHR:
    check_scalar(node->lhs);
    if (node->rhs)
      check_scalar(node->rhs);
    if ((node->rhs && is_complex(node->rhs->ty)) ||
        (is_complex(node->lhs->ty) && node->kind != ND_BITNOT))
      error_tok(node->tok, "invalid operands to a complex number");
    // Integer promotion: a char or short operand becomes int, so
    // ~c on an unsigned char is a negative int, not an unsigned char,
    // and an enum its int or unsigned int.
    if (is_integer(node->lhs->ty) &&
        (node->lhs->ty->size < 4 || node->lhs->ty->kind == TY_ENUM))
      node->lhs = new_cast(node->lhs, get_common_type(ty_int, node->lhs->ty));
    node->ty = unqual(node->lhs->ty);
    return;
  case ND_VAR:
  case ND_VLA_PTR:
    node->ty = node->var->ty;
    return;
  case ND_COND:
    check_scalar(node->cond);
    if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
      node->ty = ty_void;
    } else {
      usual_arith_conv(&node->then, &node->els);
      node->ty = node->then->ty;
    }
    return;
  case ND_COMMA:
    // (Some nodes, like ones that only zero memory, have no type.)
    node->ty = node->rhs->ty ? unqual(node->rhs->ty) : NULL;
    return;
  case ND_MEMBER: {
    Member *mem = node->member;
    node->ty = mem->ty;

    // Integer promotion treats a bit-field narrower than int as int,
    // even an unsigned one: `unsigned x : 5` holds 0..31, which int can
    // represent, so `s.x >= -1` compares signed ints and is true. As with
    // gcc and clang, a long one too, and one of 32 bits is int or
    // unsigned int. The loaded bits are still zero- or sign-extended as
    // mem->ty says, since codegen looks at that.
    if (mem->is_bitfield && mem->ty->kind != TY_BOOL) {
      if (mem->bit_width < 32 && mem->ty->size < 4 && mem->ty->is_unsigned) {
        node->ty = copy_type(mem->ty);
        node->ty->is_unsigned = false;
      } else if (mem->bit_width < 32 && mem->ty->size >= 4) {
        node->ty = ty_int;
      } else if (mem->bit_width == 32 && mem->ty->size == 8) {
        node->ty = mem->ty->is_unsigned ? ty_uint : ty_int;
      }
    }

    // A member of a const struct is const.
    Type *base = node->lhs->ty;
    node->ty = qualified(node->ty, base->is_const, base->is_volatile);
    return;
  }
  case ND_ADDR:
    // &a on an array `int a[3]` is a pointer to the whole array,
    // int (*)[3], so &a + 1 steps over all 3 elements.
    node->ty = pointer_to(node->lhs->ty);
    return;
  case ND_DEREF:
    if (!node->lhs->ty->base)
      error_tok(node->tok, "invalid pointer dereference");

    // `*p` on a void *p is a void expression, as in gcc: <tgmath.h> takes
    // `__typeof__(*(0 ? (T *)0 : (void *)x))`.
    node->ty = node->lhs->ty->base;
    return;
  case ND_STMT_EXPR:
    // The value of the last statement, or void if it isn't an expression,
    // as in `({ if (x) f(); })`.
    node->ty = ty_void;
    if (node->body) {
      Node *stmt = node->body;
      while (stmt->next)
        stmt = stmt->next;
      if (stmt->kind == ND_EXPR_STMT)
        node->ty = stmt->lhs->ty;
    }
    return;
  case ND_LABEL_VAL:
    node->ty = pointer_to(ty_void);
    return;
  case ND_CAS:
    add_type(node->cas_addr);
    add_type(node->cas_old);
    add_type(node->cas_new);
    node->ty = ty_bool;

    if (node->cas_addr->ty->kind != TY_PTR)
      error_tok(node->cas_addr->tok, "pointer expected");
    if (node->cas_old->ty->kind != TY_PTR)
      error_tok(node->cas_old->tok, "pointer expected");
    return;
  case ND_EXCH:
    if (node->lhs->ty->kind != TY_PTR)
      error_tok(node->lhs->tok, "pointer expected");
    node->ty = node->lhs->ty->base;
    return;
  case ND_CLZ:
  case ND_CTZ:
  case ND_POPCOUNT:
    node->ty = ty_int;
    return;
  case ND_BSWAP:
    node->ty = node->lhs->ty;
    return;
  case ND_FENCE:
    node->ty = ty_void;
    return;
  case ND_FRAME_ADDR:
    node->ty = pointer_to(ty_void);
    return;
  case ND_OVERFLOW:
    add_type(node->cas_addr);
    node->ty = ty_bool;
    return;
  }
}

//---------- Checking implicit conversions -----------------------------------

static char *param_names(Type *fn);

// type_name() without ty's own qualifiers.
static char *unqual_type_name(Type *ty) {
  char *u = ty->is_unsigned ? "unsigned " : "";

  switch (ty->kind) {
  case TY_VOID: return "void";
  case TY_BOOL: return "_Bool";
  case TY_CHAR: return ty->is_distinct ? "signed char" : format("%schar", u);
  case TY_SHORT: return format("%sshort", u);
  case TY_INT: return format("%sint", u);
  case TY_LONG: return format("%slong%s", u, ty->is_distinct ? " long" : "");
  case TY_INT128: return format("%s__int128", u);
  case TY_FLOAT: return "float";
  case TY_DOUBLE: return "double";
  case TY_LDOUBLE: return "long double";
  case TY_ENUM: return "enum";
  case TY_FUNC: return format("%s (%s)", type_name(ty->return_ty), param_names(ty));
  case TY_VLA: return format("%s[*]", type_name(ty->base));
  case TY_ARRAY:
    if (ty->array_len < 0) // of unknown length
      return format("%s[]", type_name(ty->base));
    return format("%s[%d]", type_name(ty->base), ty->array_len);
  case TY_PTR: {
    if (ty->base->kind == TY_FUNC)
      return format("%s (*)(%s)", type_name(ty->base->return_ty),
                    param_names(ty->base));
    if (ty->base->kind == TY_ARRAY)
      return format("%s (*)[%d]", type_name(ty->base->base), ty->base->array_len);
    char *base = type_name(ty->base);
    return format("%s%s*", base, base[strlen(base) - 1] == '*' ? "" : " ");
  }
  case TY_STRUCT:
  case TY_UNION: {
    if (ty->is_complex)
      return format("_Complex %s", type_name(complex_part(ty)));
    char *kw = ty->kind == TY_STRUCT ? "struct" : "union";
    if (ty->tag)
      return format("%s %.*s", kw, ty->tag->len, ty->tag->loc);
    return format("%s (anonymous)", kw);
  }
  }
  unreachable();
}

// Returns a type spelled the way C writes it, e.g. "const unsigned char *"
// or "char *const", for error messages.
char *type_name(Type *ty) {
  char *s = unqual_type_name(ty);
  char *q = ty->is_const ? (ty->is_volatile ? "const volatile" : "const")
                         : (ty->is_volatile ? "volatile" : NULL);
  if (!q)
    return s;
  if (ty->kind == TY_PTR)
    return format("%s%s", s, q);
  return format("%s %s", q, s);
}

// Returns a function's parameter list for type_name(), e.g. "int, char *".
static char *param_names(Type *fn) {
  if (!fn->params)
    return fn->is_variadic ? "" : "void";

  char *s = type_name(fn->params);
  for (Type *t = fn->params->next; t; t = t->next)
    s = format("%s, %s", s, type_name(t));
  return fn->is_variadic ? format("%s, ...", s) : s;
}

// A null pointer constant is an integer constant 0, e.g. `int *p = 0;`.
static bool is_null_const(Node *node) {
  while (node->kind == ND_CAST && is_integer(node->ty))
    node = node->lhs;
  return node->kind == ND_NUM && is_integer(node->ty) && node->val == 0;
}

// For compilers other than gcc, glibc's large-file support renames
// functions with macros, like `#define getrlimit getrlimit64`, so a
// `struct rlimit *` reaches a `struct rlimit64 *` parameter. The two have
// the same layout: allow `struct X` and `struct X64` of the same size.
static bool is_large_file_pair(Type *a, Type *b) {
  if (a->kind != TY_STRUCT || b->kind != TY_STRUCT || !a->tag || !b->tag ||
      a->size != b->size)
    return false;
  if (a->tag->len > b->tag->len) {
    Type *t = a;
    a = b;
    b = t;
  }
  return b->tag->len == a->tag->len + 2 &&
         !strncmp(a->tag->loc, b->tag->loc, a->tag->len) &&
         !strncmp(b->tag->loc + a->tag->len, "64", 2);
}

// Can a pointer to `from` be stored in a pointer to `to` without a cast?
// The pointees' own qualifiers are checked by check_assign.
static bool pointee_ok(Type *to, Type *from) {
  if (to->kind == TY_VOID || from->kind == TY_VOID)
    return true;
  if (is_compatible_unqual(to, from))
    return true;
  if (is_large_file_pair(to, from))
    return true;

  // Differing only in signedness (char * vs unsigned char *) is
  // allowed, as gcc does.
  if (is_integer(to) && is_integer(from) && to->size == from->size)
    return true;

  // A function declared with empty parentheses, like `int f()`,
  // matches any parameter list.
  if (to->kind == TY_FUNC && from->kind == TY_FUNC &&
      is_compatible_unqual(to->return_ty, from->return_ty) &&
      ((!to->params && to->is_variadic) || (!from->params && from->is_variadic)))
    return true;

  // Arrays of unknown or variable length, e.g. int (*)[n].
  if ((to->kind == TY_ARRAY || to->kind == TY_VLA) &&
      (from->kind == TY_ARRAY || from->kind == TY_VLA) &&
      (to->kind == TY_VLA || from->kind == TY_VLA))
    return pointee_ok(to->base, from->base);
  return false;
}

// Can `from` be assigned to type `to` with no cast and no warning? (For a
// transparent union's members: see funcall() in parser.c.)
bool is_assignable(Type *to, Node *from) {
  add_type(from);
  Type *ty = from->ty;
  if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
    ty = pointer_to(ty->base);
  else if (ty->kind == TY_FUNC)
    ty = pointer_to(ty);
  if (to->kind == TY_PTR)
    return (ty->kind == TY_PTR && pointee_ok(to->base, ty->base)) ||
           (is_integer(ty) && is_null_const(from));
  if (is_numeric(to) && is_numeric(ty))
    return true;
  return is_compatible_unqual(to, ty);
}

// Reports an error if `from` can't be converted to type `to` without a
// cast, as in `=`, initializers, function arguments and `return` (the
// rules are C11 6.5.16.1). `what` names the place, e.g. "assignment".
void check_assign(Type *to, Node *from, char *what) {
  add_type(from);
  Type *ty = from->ty;

  // Arrays and functions are used as pointers.
  if (ty->kind == TY_ARRAY || ty->kind == TY_VLA)
    ty = pointer_to(ty->base);
  else if (ty->kind == TY_FUNC)
    ty = pointer_to(ty);

  // Assigning to an array is reported elsewhere as "not an lvalue".
  if (to->kind == TY_ARRAY || to->kind == TY_VLA)
    return;

  if (ty->kind == TY_VOID)
    error_tok(from->tok, "void value used in %s", what);

  if ((is_numeric(to) || is_complex(to)) && (is_numeric(ty) || is_complex(ty)))
    return;
  if (to->kind == TY_BOOL && ty->kind == TY_PTR)
    return;

  if ((to->kind == TY_STRUCT || to->kind == TY_UNION) &&
      to->kind == ty->kind && is_compatible_unqual(to, ty))
    return;

  if (to->kind == TY_PTR) {
    // Dropping the pointee's const or volatile is allowed with a warning,
    // as gcc does.
    if (ty->kind == TY_PTR && pointee_ok(to->base, ty->base)) {
      Type *t = to->base, *f = ty->base;
      char *q = f->is_const && !t->is_const ? "const"
                : f->is_volatile && !t->is_volatile ? "volatile" : NULL;
      if (q && !in_system_header(from->tok))
        warn_opt("discarded-qualifiers", from->tok,
                 "%s discards the '%s' qualifier of '%s'", what, q, type_name(ty));
      return;
    }
    if (is_integer(ty) && is_null_const(from))
      return;

    // For compilers other than gcc, glibc renames functions with macros
    // like `#define readdir readdir64`, which returns a same-layout struct
    // under another name. That mismatch is the header's, not the user's.
    if (ty->kind == TY_PTR && in_system_header(from->tok))
      return;
  }

  char *hint = "";
  if ((to->kind == TY_PTR || is_integer(to)) &&
      (ty->kind == TY_PTR || is_integer(ty)))
    hint = " (use a cast if this is intended)";

  error_tok(from->tok, "cannot convert '%s' to '%s' in %s%s",
            type_name(ty), type_name(to), what, hint);
}
