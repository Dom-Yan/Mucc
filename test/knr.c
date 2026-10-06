#include "test.h"

// K&R (old-style) function definitions: the parameters' types are
// declared between the list and the body. A caller passes arguments
// promoted, as to `int f()`.

int add(a, b)
int a, b;
{
  return a + b;
}

// Undeclared parameters are int.
int twice(x) { return x * 2; }

// Passed as int and double, seen as their declared types
int narrow(c, s, f, u)
char c;
short s;
float f;
unsigned char u;
{
  return c + s + (int)(f * 2) + u + (sizeof(c) == 1) * 1000 + (sizeof(f) == 4) * 10000;
}

double mix(n, d, p, arr, fp)
long n;
double d;
char *p;
int arr[];
int (*fp)();
{
  return n + d + *p + arr[1] + fp(3) + (sizeof(arr) == 8) * 100;
}

static int sq(int x) { return x * x; }

// register, a struct, and a pointer to a function
struct pt { int x, y; };
int manhattan(p, q, f)
register struct pt *p;
struct pt q;
int (*f)(int);
{
  return f(p->x - q.x) + f(p->y - q.y);
}

// A prototype in scope, with promoted types, then the definition
long scale(long v, int k);
long scale(v, k) long v; int k; { return v * k; }

// Recursion, and the parameter list's order, not the declarations'
int fact(n, acc)
int acc;
int n;
{
  return n <= 1 ? acc : fact(n - 1, acc * n);
}

int pick(a, b, which) char *a, *b; { return which ? *b : *a; }

int main() {
  ASSERT(7, add(3, 4));
  ASSERT(10, twice(5));
  ASSERT(11000 + 3 + 4 + 3 + 250, narrow(3, 4, 1.5f, 250));
  ASSERT(11000 - 1 + 7 + 2 + 255, narrow(-1, 7, 1.25, 511)); // 511 as unsigned char
  int arr[] = {1, 2, 3};
  ASSERT(100 + 10 + 2 + 'A' + 2 + 9, (int)mix(10L, 2.0, "A", arr, sq));
  struct pt p = {5, 9}, q = {2, 4};
  ASSERT(9 + 25, manhattan(&p, q, sq));
  ASSERT(42, scale(6, 7));
  ASSERT(120, fact(5, 1));
  ASSERT('y', pick("x", "y", 1));
  ASSERT('x', pick("x", "y", 0));

  printf("OK\n");
  return 0;
}
