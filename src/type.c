//============================================================================
// type.c - STAGE 3 of 4: PARSE (types)
//
// The C type system: built-in types, type constructors, and add_type(),
// which works out the type of every AST node.
//============================================================================

#include "mucc.h"

//---------- Built-in types --------------------------------------------------

Type *ty_void = &(Type){TY_VOID, 1, 1};
Type *ty_bool = &(Type){TY_BOOL, 1, 1};

Type *ty_char = &(Type){TY_CHAR, 1, 1};
Type *ty_short = &(Type){TY_SHORT, 2, 2};
Type *ty_int = &(Type){TY_INT, 4, 4};
Type *ty_long = &(Type){TY_LONG, 8, 8};

Type *ty_uchar = &(Type){TY_CHAR, 1, 1, true};
Type *ty_ushort = &(Type){TY_SHORT, 2, 2, true};
Type *ty_uint = &(Type){TY_INT, 4, 4, true};
Type *ty_ulong = &(Type){TY_LONG, 8, 8, true};

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
         k == TY_INT  || k == TY_LONG || k == TY_ENUM;
}

bool is_flonum(Type *ty) {
  return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
         ty->kind == TY_LDOUBLE;
}

bool is_numeric(Type *ty) {
  return is_integer(ty) || is_flonum(ty);
}

bool is_compatible(Type *t1, Type *t2) {
  if (t1 == t2)
    return true;

  if (t1->origin)
    return is_compatible(t1->origin, t2);

  if (t2->origin)
    return is_compatible(t1, t2->origin);

  if (t1->kind != t2->kind)
    return false;

  switch (t1->kind) {
  case TY_CHAR:
  case TY_SHORT:
  case TY_INT:
  case TY_LONG:
    return t1->is_unsigned == t2->is_unsigned;
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE:
    return true;
  case TY_PTR:
    return is_compatible(t1->base, t2->base);
  case TY_FUNC: {
    if (!is_compatible(t1->return_ty, t2->return_ty))
      return false;
    if (t1->is_variadic != t2->is_variadic)
      return false;

    Type *p1 = t1->params;
    Type *p2 = t2->params;
    for (; p1 && p2; p1 = p1->next, p2 = p2->next)
      if (!is_compatible(p1, p2))
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

//---------- Type constructors -----------------------------------------------

Type *copy_type(Type *ty) {
  Type *ret = arena_alloc(sizeof(Type));
  *ret = *ty;
  ret->origin = ty;
  return ret;
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

//---------- Typing AST nodes ------------------------------------------------

static Type *get_common_type(Type *ty1, Type *ty2) {
  if (ty1->base)
    return pointer_to(ty1->base);

  if (ty1->kind == TY_FUNC)
    return pointer_to(ty1);
  if (ty2->kind == TY_FUNC)
    return pointer_to(ty2);

  if (ty1->kind == TY_LDOUBLE || ty2->kind == TY_LDOUBLE)
    return ty_ldouble;
  if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
    return ty_double;
  if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
    return ty_float;

  if (ty1->size < 4)
    ty1 = ty_int;
  if (ty2->size < 4)
    ty2 = ty_int;

  if (ty1->size != ty2->size)
    return (ty1->size < ty2->size) ? ty2 : ty1;

  if (ty2->is_unsigned)
    return ty2;
  return ty1;
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
    usual_arith_conv(&node->lhs, &node->rhs);
    node->ty = node->lhs->ty;
    return;
  case ND_NEG: {
    Type *ty = get_common_type(ty_int, node->lhs->ty);
    node->lhs = new_cast(node->lhs, ty);
    node->ty = ty;
    return;
  }
  case ND_ASSIGN:
    if (node->lhs->ty->kind == TY_ARRAY)
      error_tok(node->lhs->tok, "not an lvalue");
    if (node->lhs->ty->kind != TY_STRUCT)
      node->rhs = new_cast(node->rhs, node->lhs->ty);
    node->ty = node->lhs->ty;
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    usual_arith_conv(&node->lhs, &node->rhs);
    node->ty = ty_int;
    return;
  case ND_FUNCALL:
    node->ty = node->func_ty->return_ty;
    return;
  case ND_NOT:
  case ND_LOGOR:
  case ND_LOGAND:
    node->ty = ty_int;
    return;
  case ND_BITNOT:
  case ND_SHL:
  case ND_SHR:
    // Integer promotion: a char or short operand becomes int, so
    // ~c on an unsigned char is a negative int, not an unsigned char.
    if (is_integer(node->lhs->ty) && node->lhs->ty->size < 4)
      node->lhs = new_cast(node->lhs, ty_int);
    node->ty = node->lhs->ty;
    return;
  case ND_VAR:
  case ND_VLA_PTR:
    node->ty = node->var->ty;
    return;
  case ND_COND:
    if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
      node->ty = ty_void;
    } else {
      usual_arith_conv(&node->then, &node->els);
      node->ty = node->then->ty;
    }
    return;
  case ND_COMMA:
    node->ty = node->rhs->ty;
    return;
  case ND_MEMBER: {
    Member *mem = node->member;
    node->ty = mem->ty;

    // Integer promotion treats a bit-field narrower than int as int,
    // even an unsigned one: `unsigned x : 5` holds 0..31, which int can
    // represent, so `s.x >= -1` compares signed ints and is true. The
    // loaded bits are still zero-extended, since codegen looks at
    // mem->ty for that.
    if (mem->is_bitfield && mem->bit_width < 32 && mem->ty->kind != TY_BOOL &&
        mem->ty->is_unsigned) {
      if (mem->ty->size == 4) {
        node->ty = ty_int;
      } else if (mem->ty->size < 4) {
        node->ty = copy_type(mem->ty);
        node->ty->is_unsigned = false;
      }
    }
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
    if (node->lhs->ty->base->kind == TY_VOID)
      error_tok(node->tok, "dereferencing a void pointer");

    node->ty = node->lhs->ty->base;
    return;
  case ND_STMT_EXPR:
    if (node->body) {
      Node *stmt = node->body;
      while (stmt->next)
        stmt = stmt->next;
      if (stmt->kind == ND_EXPR_STMT) {
        node->ty = stmt->lhs->ty;
        return;
      }
    }
    error_tok(node->tok, "statement expression returning void is not supported");
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
      error_tok(node->cas_addr->tok, "pointer expected");
    node->ty = node->lhs->ty->base;
    return;
  }
}

//---------- Checking implicit conversions -----------------------------------

static char *param_names(Type *fn);

// Returns a type spelled the way C writes it, e.g. "unsigned char *",
// for error messages.
char *type_name(Type *ty) {
  char *u = ty->is_unsigned ? "unsigned " : "";

  switch (ty->kind) {
  case TY_VOID: return "void";
  case TY_BOOL: return "_Bool";
  case TY_CHAR: return format("%schar", u);
  case TY_SHORT: return format("%sshort", u);
  case TY_INT: return format("%sint", u);
  case TY_LONG: return format("%slong", u);
  case TY_FLOAT: return "float";
  case TY_DOUBLE: return "double";
  case TY_LDOUBLE: return "long double";
  case TY_ENUM: return "enum";
  case TY_FUNC: return format("%s (%s)", type_name(ty->return_ty), param_names(ty));
  case TY_VLA: return format("%s[*]", type_name(ty->base));
  case TY_ARRAY: return format("%s[%d]", type_name(ty->base), ty->array_len);
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
    char *kw = ty->kind == TY_STRUCT ? "struct" : "union";
    if (ty->tag)
      return format("%s %.*s", kw, ty->tag->len, ty->tag->loc);
    return format("%s (anonymous)", kw);
  }
  }
  unreachable();
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

// Can a pointer to `from` be stored in a pointer to `to` without a cast?
static bool pointee_ok(Type *to, Type *from) {
  if (to->kind == TY_VOID || from->kind == TY_VOID)
    return true;
  if (is_compatible(to, from))
    return true;

  // Differing only in signedness (char * vs unsigned char *) is
  // allowed, as gcc does.
  if (is_integer(to) && is_integer(from) && to->size == from->size)
    return true;

  // A function declared with empty parentheses, like `int f()`,
  // matches any parameter list.
  if (to->kind == TY_FUNC && from->kind == TY_FUNC &&
      is_compatible(to->return_ty, from->return_ty) &&
      ((!to->params && to->is_variadic) || (!from->params && from->is_variadic)))
    return true;

  // Arrays of unknown or variable length, e.g. int (*)[n].
  if ((to->kind == TY_ARRAY || to->kind == TY_VLA) &&
      (from->kind == TY_ARRAY || from->kind == TY_VLA) &&
      (to->kind == TY_VLA || from->kind == TY_VLA))
    return pointee_ok(to->base, from->base);
  return false;
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

  if (is_numeric(to) && is_numeric(ty))
    return;
  if (to->kind == TY_BOOL && ty->kind == TY_PTR)
    return;

  if ((to->kind == TY_STRUCT || to->kind == TY_UNION) &&
      to->kind == ty->kind && is_compatible(to, ty))
    return;

  if (to->kind == TY_PTR) {
    if (ty->kind == TY_PTR && pointee_ok(to->base, ty->base))
      return;
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
