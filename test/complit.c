#include "test.h"

typedef struct Tree {
  int val;
  struct Tree *lhs;
  struct Tree *rhs;
} Tree;

Tree *tree = &(Tree){
  1,
  &(Tree){
    2,
    &(Tree){ 3, 0, 0 },
    &(Tree){ 4, 0, 0 }
  },
  0
};

static int complit_get(int *p) { return *p + 1; }

// [GNU] A struct or union at file scope set from a compound literal, a
// function's address in it included
typedef struct { int a; int (*f)(int *); char s[4]; } ComplitFn;
ComplitFn complit_fn = (ComplitFn){ 42, complit_get, "hi" };
typedef union { long l; char *p; } ComplitU;
ComplitU complit_u = (ComplitU){ .p = "str" };
struct { int z; ComplitFn t; } complit_nest = { 5, (ComplitFn){ 7, complit_get, "ab" } };

int main() {
  ASSERT(1, (int){1});
  ASSERT(2, ((int[]){0,1,2})[2]);
  ASSERT('a', ((struct {char a; int b;}){'a', 3}).a);
  ASSERT(3, ({ int x=3; (int){x}; }));
  (int){3} = 5;

  ASSERT(1, tree->val);
  ASSERT(2, tree->lhs->val);
  ASSERT(3, tree->lhs->lhs->val);
  ASSERT(4, tree->lhs->rhs->val);

  // A member or element of a compound literal, and its address in a loop
  // (where its variable must not be kept in a register)
  ASSERT(1, (union { int i; char c; }){1}.c);
  ASSERT(6, ({ int w = 2; (int[3]){4, 5, 6}[w]; }));
  ASSERT(55, ({ int e, n = 0; do e = complit_get(&(int){n}); while (++n < 5); e * 10 + n; }));
  ASSERT(5, ({ int *p = &(int){5}; *p; }));

  // A compound literal keeps its type's qualifiers.
  ASSERT(1, _Generic(&(const int){0}, const int *: 1, default: 0));
  ASSERT(1, ({ typedef const int CI; _Generic(&(CI){0}, const int *: 1, default: 0); }));
  ASSERT(0, _Generic(&(int){0}, const int *: 1, default: 0));

  // An array of unknown length has the length its initializer gives,
  // for sizeof too, with or without parentheses around the literal.
  ASSERT(12, sizeof((int[]){1, 2, 3}));
  ASSERT(12, (sizeof (int[]){1, 2, 3}));
  ASSERT(4, sizeof((char[]){"abc"}));
  ASSERT(2, sizeof((int[]){0, 1}) / sizeof(int));
  ASSERT(3, ({ int *p = (int[]){1, 2, 3}; p[2]; }));
  ASSERT(8, sizeof(struct { int a, b; }));

  ASSERT(42, complit_fn.a);
  ASSERT(4, ({ int x = 3; complit_fn.f(&x); }));
  ASSERT(0, strcmp(complit_fn.s, "hi"));
  ASSERT(0, strcmp(complit_u.p, "str"));
  ASSERT(5, complit_nest.z);
  ASSERT(7, complit_nest.t.a);
  ASSERT(1, complit_nest.t.f == complit_get);
  ASSERT(0, strcmp(complit_nest.t.s, "ab"));
  ASSERT(9, ({ static ComplitFn s = (ComplitFn){ 9, complit_get, "cd" }; s.a + strcmp(s.s, "cd"); }));

  printf("OK\n");
  return 0;
}
