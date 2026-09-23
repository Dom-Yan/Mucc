#!/bin/bash
# Checks that mucc rejects bad programs with the right message at the right
# place, and still accepts valid code that looks similar.
#
#   expect_error '<line>:<col>: error: <message>' <<'EOF'   (C source)
#   expect_ok '<name>' <<'EOF'                              (C source)
# Like the Makefile's test rules, point at the repo's include/ so this
# also works for stage2/mucc, which has no include/ next to it.
mucc="$1 -Iinclude"

tmp=`mktemp -d /tmp/mucc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

expect_error() {
    cat > $tmp/t.c
    if $mucc -c -o $tmp/t.o $tmp/t.c 2> $tmp/err; then
        echo "testing error '$1' ... failed (compiled without error)"
        exit 1
    fi
    got=$(head -1 $tmp/err | sed "s|^$tmp/t.c:||")
    if [ "$got" != "$1" ]; then
        echo "testing error '$1' ... failed"
        echo "  got: $got"
        exit 1
    fi
    echo "testing error '$1' ... passed"
}

expect_ok() {
    cat > $tmp/t.c
    if ! $mucc -c -o $tmp/t.o $tmp/t.c 2> $tmp/err; then
        echo "testing ok '$1' ... failed"
        cat $tmp/err
        exit 1
    fi
    echo "testing ok '$1' ... passed"
}

# Compiles, but the first line of stderr must be this warning.
expect_warning() {
    cat > $tmp/t.c
    if ! $mucc -c -o $tmp/t.o $tmp/t.c 2> $tmp/err; then
        echo "testing warning '$1' ... failed (did not compile)"
        cat $tmp/err
        exit 1
    fi
    got=$(head -1 $tmp/err | sed "s|^$tmp/t.c:||")
    if [ "$got" != "$1" ]; then
        echo "testing warning '$1' ... failed"
        echo "  got: $got"
        exit 1
    fi
    echo "testing warning '$1' ... passed"
}

#---------- Syntax errors point at the right place ---------------------------

expect_error "2:12: error: expected ';'" <<'EOF'
int main(void) {
  int x = 1
  return x;
}
EOF

expect_error "3:8: error: expected ';'" <<'EOF'
int main(void) {
  int x;
  x = 2
  return x;
}
EOF

expect_error "3:13: error: expected ')' before '{'" <<'EOF'
int main(void) {
  int x = 2;
  if (x > 1 {
    return 1;
  }
  return 0;
}
EOF

expect_error "1:13: error: expected ',' or ')' before 'int'" <<'EOF'
int f(int a int b);
EOF

expect_error "4:6: error: expected ';'" <<'EOF'
int main(void) {
  return 0;
}
int x
EOF

#---------- Unknown names ----------------------------------------------------

expect_error "2:10: error: undeclared identifier 'y'" <<'EOF'
int main(void) {
  return y;
}
EOF

expect_error "2:3: error: call to undeclared function 'foo' (missing #include?)" <<'EOF'
int main(void) {
  foo(1);
  return 0;
}
EOF

expect_error "2:3: error: unknown type name 'foo'" <<'EOF'
int main(void) {
  foo x;
  return 0;
}
EOF

#---------- Type errors in =, initializers, arguments and return ------------

expect_error "2:12: error: cannot convert 'double' to 'int *' in initialization" <<'EOF'
int main(void) {
  int *p = 3.5;
  return 0;
}
EOF

expect_error "1:11: error: cannot convert 'double' to 'int *' in initialization" <<'EOF'
int *gp = 3.5;
EOF

expect_error "2:12: error: cannot convert 'int' to 'int *' in initialization (use a cast if this is intended)" <<'EOF'
int main(void) {
  int *p = 5;
  return 0;
}
EOF

expect_error "3:11: error: cannot convert 'int *' to 'int' in initialization (use a cast if this is intended)" <<'EOF'
int main(void) {
  int *p = 0;
  int x = p;
  return x;
}
EOF

expect_error "3:12: error: cannot convert 'char *' to 'int *' in initialization (use a cast if this is intended)" <<'EOF'
int main(void) {
  char *c = "hi";
  int *i = c;
  return 0;
}
EOF

expect_error "5:7: error: cannot convert 'struct B' to 'struct A' in assignment" <<'EOF'
struct A { int x; };
struct B { int x; };
int main(void) {
  struct A a; struct B b;
  a = b;
  return 0;
}
EOF

expect_error "3:5: error: cannot convert 'double' to 'int *' in argument 1 of 'f'" <<'EOF'
void f(int *p);
int main(void) {
  f(1.0);
  return 0;
}
EOF

expect_error "3:6: error: too few arguments to 'f' (expected 2, got 1)" <<'EOF'
int f(int a, int b);
int main(void) {
  f(1);
  return 0;
}
EOF

expect_error "3:8: error: too many arguments to 'f' (expected 1)" <<'EOF'
int f(int a);
int main(void) {
  f(1, 2);
  return 0;
}
EOF

expect_error "2:3: error: 'return' needs a value in function returning 'int'" <<'EOF'
int f(void) {
  return;
}
EOF

expect_error "2:10: error: void function 'g' should not return a value" <<'EOF'
void g(void) {
  return 1;
}
EOF

expect_error "3:10: error: cannot convert 'char *' to 'int' in return (use a cast if this is intended)" <<'EOF'
int h(void) {
  char *s = "x";
  return s;
}
EOF

expect_error "3:11: error: void value used in initialization" <<'EOF'
void v(void);
int main(void) {
  int x = v();
  return x;
}
EOF

expect_error "1:1: error: static assertion failed: int must be 8 bytes" <<'EOF'
_Static_assert(sizeof(int) == 8, "int must be 8 bytes");
EOF

expect_error "2:3: error: static assertion failed" <<'EOF'
int main(void) {
  static_assert(0);
  return 0;
}
EOF

expect_error "2:11: error: cannot convert 'void *' to 'int' in initialization (use a cast if this is intended)" <<'EOF'
int main(void) {
  int x = nullptr;
  return x;
}
EOF

#---------- Preprocessor directives ------------------------------------------

# The extra tokens are skipped, not compiled: `junk` would be an error.
expect_warning "3:8: warning: extra tokens at end of directive" <<'EOF'
#if 1
int a;
#endif junk
int b;
EOF

expect_error "2:2: error: #error int must be 4 bytes" <<'EOF'
#if __SIZEOF_INT__ != 8
#error int must be 4 bytes
#endif
EOF

expect_error "1:2: error: #error" <<'EOF'
#error
EOF

expect_error "1:17: error: unsupported non-standard concatenation of string literals" <<'EOF'
char *s = u8"a" u"b";
EOF

# "a" is re-read as a wide string to join it with L"b"; it must keep its line.
expect_error "3:11: error: cannot convert 'int *' to 'char *' in initialization (use a cast if this is intended)" <<'EOF'
int x;
int y;
char *p = "a" L"b";
EOF

expect_ok 'u8 string concatenated with a plain string' <<'EOF'
_Static_assert(sizeof(u8"ab" "cd") == 5, "");
EOF

expect_warning "1:2: warning: #warning this is deprecated" <<'EOF'
#warning this is deprecated
int x;
EOF

#---------- Valid code that must still compile -------------------------------

expect_ok 'implicit conversions' <<'EOF'
#include <stddef.h>
void *malloc(unsigned long);
void free(void *);
struct S { int x; };
typedef struct S T;
int cmp();
int realcmp(int a, int b) { return a - b; }
void nothing(void) {}
void call_nothing(void) { return nothing(); }
int main(void) {
  int *p = 0;
  int *q = NULL;
  void *v = p;
  p = v;
  char *c = "hi";
  unsigned char *u = c;
  int (*fp)() = realcmp;
  int (*fp2)(int, int) = cmp;
  int a[3];
  int (*pa)[3] = &a;
  struct S s = {1};
  T t = s;
  s = t;
  _Bool b = p;
  long l = 'c';
  double d = l;
  float f = d;
  int *m = malloc(4);
  free(m);
  char buf[4];
  char *bp = buf;
  void (*fn)(void) = 0;
  fn = nothing;
  return 0;
}
EOF

expect_ok 'glibc readdir64 rename in a system header' <<'EOF'
#define _FILE_OFFSET_BITS 64
#include <dirent.h>
int main(void) {
  DIR *d = opendir(".");
  struct dirent *e = readdir(d);
  closedir(d);
  return e == 0;
}
EOF

echo OK
