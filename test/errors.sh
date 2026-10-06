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

# Fails, and the error lines must be exactly these (one argument each).
expect_errors() {
    cat > $tmp/t.c
    if $mucc -c -o $tmp/t.o $tmp/t.c 2> $tmp/err; then
        echo "testing errors '$1' ... failed (compiled without error)"
        exit 1
    fi
    got=$(grep -E 'error:|mucc:' $tmp/err | sed "s|^$tmp/t.c:||")
    want=$(printf '%s\n' "$@")
    if [ "$got" != "$want" ]; then
        echo "testing errors '$1' ... failed"
        echo "  got:"; echo "$got" | sed 's/^/    /'
        exit 1
    fi
    echo "testing errors '$1' ... passed"
}

# Compiles with nothing at all on stderr: no errors and no warnings.
expect_clean() {
    cat > $tmp/t.c
    if ! $mucc -c -o $tmp/t.o $tmp/t.c 2> $tmp/err || [ -s $tmp/err ]; then
        echo "testing clean '$1' ... failed"
        cat $tmp/err
        exit 1
    fi
    echo "testing clean '$1' ... passed"
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

# C23's static_assert with no message, and nullptr
saved_mucc=$mucc
mucc="$saved_mucc -std=c23"
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
mucc=$saved_mucc

#---------- Several errors in one run ----------------------------------------

expect_errors "2:11: error: undeclared identifier 'y'" \
              "3:3: error: call to undeclared function 'foo' (missing #include?)" \
              "4:11: error: expected ';'" <<'EOF'
int main(void) {
  int x = y;
  foo(1);
  return 0
}
EOF

expect_errors "1:22: error: undeclared identifier 'a'" \
              "2:22: error: undeclared identifier 'b'" <<'EOF'
int f(void) { return a; }
int g(void) { return b; }
int h(void) { return 1; }
EOF

expect_errors "3:13: error: undeclared identifier 'q'" \
              "4:13: error: undeclared identifier 'r'" \
              "6:10: error: undeclared identifier 'z'" <<'EOF'
int main(void) {
  if (1) {
    int x = q;
    int y = r;
  }
  return z;
}
EOF

expect_errors "2:20: error: undeclared identifier 'e1'" \
              "3:16: error: undeclared identifier 'e2'" \
              "4:10: error: undeclared identifier 'e3'" <<'EOF'
int main(void) {
  if (1) { int a = e1; } else { int b = 2; }
  do { int c = e2; } while (0);
  return e3;
}
EOF

# A goto whose label was in a skipped statement isn't a second error.
expect_errors "3:11: error: undeclared identifier 'bad'" <<'EOF'
int main(void) {
  goto out;
  int y = bad;
out: return 0;
}
EOF

expect_errors "3:12: error: expected '}'" <<'EOF'
int main(void) {
  int x = 1;
  return x;
EOF

# After 20 errors, mucc stops.
printf 'int main(void) {\n' > $tmp/many.c
for i in $(seq 25); do printf '  a%d;\n' $i >> $tmp/many.c; done
printf '}\n' >> $tmp/many.c
$mucc -c -o $tmp/t.o $tmp/many.c 2> $tmp/err
[ $(grep -c 'error:' $tmp/err) = 20 ] && tail -1 $tmp/err | grep -q 'too many errors'
if [ $? = 0 ]; then
    echo "testing errors 'stops after 20' ... passed"
else
    echo "testing errors 'stops after 20' ... failed"; exit 1
fi

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

#---------- C23: constexpr, auto, u8'', #embed -------------------------------

mucc="$saved_mucc -std=c23"

expect_error "2:21: error: constexpr 'x' needs a constant initializer" <<'EOF'
int f(int n) {
  constexpr int x = n;
  return x;
}
EOF

expect_errors "1:29: error: value doesn't fit in constexpr 'a' of type 'unsigned char'" \
              "2:24: error: value doesn't fit in constexpr 'b' of type 'unsigned int'" \
              "3:19: error: constexpr 'c' of type 'int' can't be initialized with a floating value" \
              "4:20: error: a constexpr pointer can only be null" \
              "5:20: error: value doesn't fit in constexpr 'd' of type '_Bool'" <<'EOF'
constexpr unsigned char a = 300;
constexpr unsigned b = -1;
constexpr int c = 1.5;
constexpr int *p = (int *)8;
constexpr bool d = 2;
constexpr int ok = 255;
EOF

expect_errors "1:15: error: constexpr 'z' needs an initializer" \
              "4:3: error: cannot modify constexpr 'n'" \
              "5:3: error: cannot modify constexpr 'n'" \
              "6:3: error: cannot modify constexpr 'n'" <<'EOF'
constexpr int z;
int f(void) {
  constexpr int n = 1;
  n = 2;
  n++;
  n += 3;
  return n;
}
EOF

expect_error "1:32: error: 'auto' can only declare a plain variable, as in 'auto x = 1'" <<'EOF'
int f(void) { int x = 1; auto *p = &x; return 0; }
EOF

expect_error "2:24: error: cannot infer a type from a void expression" <<'EOF'
void g(void);
int f(void) { auto v = g(); return 0; }
EOF

expect_error "1:9: error: u8 character literal must be a single byte; use a u8 string" <<'EOF'
int c = u8'é';
EOF

expect_error "1:8: error: no-such-file.bin: cannot open file: No such file or directory" <<'EOF'
#embed "no-such-file.bin"
EOF

expect_error "2:14: error: unknown #embed parameter 'frobnicate'" <<'EOF'
char a[] = {
#embed "t.c" frobnicate(1)
};
EOF

# [[noreturn]] and unreachable() end a function as surely as return.
expect_clean 'no missing-return warning after [[noreturn]] or unreachable()' <<'EOF'
#include <stddef.h>
[[noreturn]] void die(void);
int f(int x) { if (x) return 1; die(); }
int g(int x) { if (x) return 1; unreachable(); }
EOF

# unreachable() only comes with a direct #include <stddef.h>.
expect_clean 'a program may define its own unreachable' <<'EOF'
#include <stdio.h>
#include <stdlib.h>
static void unreachable(void) { puts("x"); }
int main(void) { unreachable(); return 0; }
EOF

# A negative array size is an error, as compile-time checks rely on.
expect_error "1:21: error: size of array is negative" <<'EOF'
struct S { char bug[1 == 2 ? 1 : -1]; };
EOF

expect_error "1:7: error: size of array is negative" <<'EOF'
int a[-4];
EOF

# enum E : type: values must fit the type, and it must be an integer type.
expect_error "1:30: error: enumerator value 256 is outside the range of 'unsigned char'" <<'EOF'
enum E : unsigned char { A = 256 };
EOF

expect_error "1:35: error: enumerator value 256 is outside the range of 'unsigned char'" <<'EOF'
enum E : unsigned char { A = 255, B };
EOF

expect_error "1:10: error: an enum's underlying type must be an integer type" <<'EOF'
enum E : float { A };
EOF

mucc=$saved_mucc

#---------- Warnings ---------------------------------------------------------

# -Wunused-variable and -Wreturn-type are off without -Wall, as with gcc.
expect_clean 'unused variable and missing return without -Wall' <<'EOF'
int f(int x) { int unused; if (x) return 1; }
EOF

mucc="$saved_mucc -Wunused-variable -Wreturn-type"
expect_warning "3:7: warning: unused variable 'unused' [-Wunused-variable]" <<'EOF'
int f(void) {
  int used = 1;
  int unused = used;
  return 0;
}
EOF

expect_warning "4:1: warning: control reaches end of non-void function 'f' [-Wreturn-type]" <<'EOF'
int f(int x) {
  if (x)
    return 1;
}
EOF

expect_warning "1:49: warning: control reaches end of non-void function 'g' [-Wreturn-type]" <<'EOF'
int g(int x) { switch (x) { case 1: return 1; } }
EOF

expect_warning "1:35: warning: control reaches end of non-void function 'h' [-Wreturn-type]" <<'EOF'
int h(void) { for (;;) { break; } }
EOF
mucc=$saved_mucc

# None of these can reach the end of the function without a return.
expect_clean 'no false warnings' <<'EOF'
#include <stdlib.h>
#include <stdnoreturn.h>
noreturn void die(const char *msg);
int a(int x) { if (x) return 1; else return 2; }
int b(void) { for (;;) {} }
int c(void) { while (1) { if (rand()) return 1; } }
int d(int x) { switch (x) { case 1: return 1; default: return 2; } }
int e(void) { exit(1); }
int f(void) { die("x"); }
int g(void) { abort(); }
int h(void) { do { return 1; } while (0); }
int i(int x) { goto end; end: return x; }
int j(void) { do { if (rand()) return 1; } while (1); }
int k(void) { int v; (void)v; int w = 1; return sizeof(w); }
int main(void) { }
EOF

# A case value given twice is an error; Tcl's configure tests
# sizeof(long) == 8 this way.
expect_error '1:41: error: duplicate case value' <<'EOF'
int f(void) { switch (0) { case 1: case (sizeof(long) == 8): ; } return 0; }
EOF

expect_error '1:58: error: duplicate case value' <<'EOF'
int g(int x) { switch (x) { case 1 ... 5: return 1; case 3: return 2; } return 0; }
EOF

expect_error '1:48: error: multiple default labels in one switch' <<'EOF'
int h(int x) { switch (x) { default: return 1; default: return 2; } }
EOF

expect_ok 'distinct case values' <<'EOF'
int f(unsigned x, int y) {
  switch (x) { case 0xffffffff: return 1; case 0x7fffffff: return 2;
               case 1 ... 5: return 3; case 6 ... 9: return 4; }
  switch (y) { case -1: switch (y) { case -1: return 5; default: ; } default: ; }
  return 0;
}
EOF

printf 'int f(void) { int unused; }\n' > $tmp/t.c
if $mucc -w -c -o $tmp/t.o $tmp/t.c 2> $tmp/err && [ ! -s $tmp/err ]; then
    echo "testing clean '-w silences warnings' ... passed"
else
    echo "testing clean '-w silences warnings' ... failed"; exit 1
fi

# On by default, as with gcc
expect_warning "1:34: warning: function returns address of local variable 'x' [-Wreturn-local-addr]" <<'EOF'
int *f(void) { int x = 1; return &x; }
EOF

expect_warning "1:25: warning: division by zero [-Wdiv-by-zero]" <<'EOF'
int f(int a) { return a / 0; }
EOF

expect_warning "1:25: warning: left shift count >= width of type [-Wshift-count-overflow]" <<'EOF'
int f(int a) { return a << 32; }
EOF

expect_clean 'no default warning for what -Wall adds' <<'EOF'
static int unused(void) { return 0; }
int f(int a) { if (a = 1) a == 2; return a; }
EOF

# -Wall's
mucc_plain=$mucc
mucc="$mucc_plain -Wall"

expect_warning "1:22: warning: suggest parentheses around assignment used as truth value [-Wparentheses]" <<'EOF'
int f(int a) { if (a = 1) return 1; return 0; }
EOF

expect_warning "1:24: warning: statement with no effect [-Wunused-value]" <<'EOF'
void f(int a, int b) { a == b; }
EOF

expect_warning "1:33: warning: comparison with string literal results in unspecified behavior [-Waddress]" <<'EOF'
int f(const char *s) { return s == "abc" ? 1 : 0; }
EOF

expect_warning "1:36: warning: the address of 'f' will always evaluate as 'true' [-Waddress]" <<'EOF'
void f(void); int g(void) { return f ? 1 : 0; } int h(void) { if (f) return 1; return 0; }
EOF

expect_warning "2:45: warning: format '%d' expects argument of type 'int', but argument 2 has type 'long' [-Wformat]" <<'EOF'
int printf(const char *, ...);
void f(long l, char *s) { printf("%d %s\n", l, s); }
EOF

expect_warning "2:24: warning: format '%s' expects a matching 'char *' argument [-Wformat]" <<'EOF'
int printf(const char *, ...);
void f(int a) { printf("%d %s\n", a); }
EOF

expect_warning "2:35: warning: too many arguments for format [-Wformat-extra-args]" <<'EOF'
int printf(const char *, ...);
void f(int a) { printf("%d\n", a, a); }
EOF

expect_warning "1:12: warning: 'unused' defined but not used [-Wunused-function]" <<'EOF'
static int unused(void) { return 0; }
EOF

expect_warning "3:3: warning: enumeration value 'BLUE' not handled in switch [-Wswitch]" <<'EOF'
enum Color { RED, GREEN, BLUE };
int f(enum Color c) {
  switch (c) { case RED: return 1; case GREEN: return 2; }
  return 0;
}
EOF

expect_clean '-Wall on correct code' <<'EOF'
int printf(const char *, ...);
int sscanf(const char *, const char *, ...);
enum Color { RED, GREEN, BLUE };
static int used(int x) { return x * 2; }
int *keep(int *p) { static int s; return p ? &p[1] : &s; }
int f(enum Color c, long l, unsigned long z, double d, char *s) {
  int a, n = 0;
  if ((a = used(1))) n++;
  while ((a = a - 1) > 0) n++;
  (void)(a == 1);
  n += ({ int t = a; t; });
  switch (c) { case RED: case GREEN: n++; break; case BLUE: break; }
  switch (c) { case RED: n++; default: break; }
  printf("%d %ld %zu %f %s %p %5.2f %-3d %%\n", n, l, z, d, s, (void *)s, d, a);
  printf("%*d %.*s %c %hhd %llx\n", a, n, a, s, a, (char)a, 1ULL);
  sscanf(s, "%d %lf %s", &a, &d, s);
  return n / 1 + (a << 3) + (s == 0);
}
EOF

# -Wno-<name>, -Werror and -Werror=<name>
mucc="$mucc_plain -Wall -Wno-parentheses"
expect_clean '-Wno-parentheses' <<'EOF'
int f(int a) { if (a = 1) return 1; return 0; }
EOF
mucc="$mucc_plain -Wall -Werror=parentheses"
expect_error "1:22: error: suggest parentheses around assignment used as truth value [-Werror=parentheses]" <<'EOF'
int f(int a) { if (a = 1) return 1; return 0; }
EOF
mucc="$mucc_plain -Wall -Werror"
expect_error "1:19: error: unused variable 'x' [-Werror=unused-variable]" <<'EOF'
int f(void) { int x; return 0; }
EOF
mucc="$mucc_plain -Wall -Werror -Wno-error=unused-variable"
expect_warning "1:19: warning: unused variable 'x' [-Wunused-variable]" <<'EOF'
int f(void) { int x; return 0; }
EOF

# #pragma GCC diagnostic changes a warning from where it is, until a pop.
mucc="$mucc_plain -Wall -Werror"
expect_error "5:12: error: 'loud' defined but not used [-Werror=unused-function]" <<'EOF'
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static int quiet(void) { return 1; }
#pragma GCC diagnostic pop
static int loud(void) { return 2; }
EOF
expect_clean '_Pragma("GCC diagnostic ignored") from a macro' <<'EOF'
#define QUIET _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wunused\"")
#define END _Pragma("GCC diagnostic pop")
QUIET
static int quiet(void) { return 1; }
END
EOF
expect_warning "2:22: warning: suggest parentheses around assignment used as truth value [-Wparentheses]" <<'EOF'
#pragma GCC diagnostic warning "-Wparentheses"
int f(int a) { if (a = 1) return 1; return 0; }
EOF
mucc="$mucc_plain -Wall"
expect_error "2:22: error: suggest parentheses around assignment used as truth value [-Werror=parentheses]" <<'EOF'
#pragma GCC diagnostic error "-Wparentheses"
int f(int a) { if (a = 1) return 1; return 0; }
EOF

# scanf's %ms takes a char **.
expect_clean '%ms in scanf' <<'EOF'
#include <stdio.h>
int f(void) { char *s; return scanf("%ms %10ms %m[a-z]", &s, &s, &s); }
EOF
mucc=$mucc_plain

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

expect_ok 'glibc getrlimit64 rename, struct passed by pointer' <<'EOF'
#define _FILE_OFFSET_BITS 64
#include <sys/resource.h>
int main(void) {
  struct rlimit r;
  return getrlimit(RLIMIT_NOFILE, &r);
}
EOF

expect_error "4:17: error: cannot convert 'struct s *' to 'struct s64 *' in initialization (use a cast if this is intended)" <<'EOF'
struct s { int a; };
struct s64 { long a; };
struct s x;
struct s64 *p = &x;
EOF

# Attributes: hints are ignored, `unused` and `noreturn` are used, and
# anything that would change the program but isn't implemented is an error.
expect_clean 'attribute unused, in each syntax' <<'EOF'
int main(void) {
  int a __attribute__((unused));
  __attribute__((unused)) int b;
  [[maybe_unused]] int c;
  [[gnu::unused]] int d;
  return 0;
}
EOF

expect_clean 'attribute noreturn' <<'EOF'
void die(void) __attribute__((noreturn));
[[gnu::noreturn]] void die2(void);
int f(int x) { if (x) return 1; die(); }
int g(int x) { if (x) return 1; die2(); }
EOF

expect_error "1:22: error: unknown attribute 'bogus'" <<'EOF'
int x __attribute__((bogus));
EOF

expect_error "1:8: error: unknown attribute 'bogus'" <<'EOF'
[[gnu::bogus]] int x;
EOF

# Every attribute gcc has that would change what a program does, but mucc
# doesn't implement, is an error (unsupported_attributes in src/parser.c).
for a in retain weakref vector_size ifunc naked target target_clones \
         copy symver scalar_storage_order noinit persistent hardbool; do
  expect_error "1:22: error: attribute '$a' is not supported" <<EOF
int x __attribute__((__${a}__(1)));
EOF
done

# In C23, `int f()` means `int f(void)`; before, any arguments go.
saved_mucc=$mucc
mucc="$saved_mucc -std=c23"
expect_error "1:36: error: too many arguments to 'f' (expected 0)" <<'EOF'
int f(); int main(void) { return f(1); }
EOF
mucc=$saved_mucc
expect_ok 'int f() with arguments before C23' <<'EOF'
int f(); int main(void) { return f(1); }
EOF

expect_error "1:28: error: alias target 'nothere' is not defined in this file" <<'EOF'
int f(void) __attribute__((alias("nothere")));
EOF

expect_error "1:33: error: visibility must be default, hidden, protected or internal" <<'EOF'
int x __attribute__((visibility("secret")));
EOF

expect_error "1:39: error: attribute 'section' is not supported on a local variable" <<'EOF'
int main(void) { int x __attribute__((section("s"))); return 0; }
EOF

# cleanup(fn): jumping into a cleanup variable's scope skips its
# initialization, so that is an error, as is misuse.
expect_error "3:3: error: jump into the scope of a variable with a cleanup" <<'EOF'
void f(int *p);
int main(void) {
  goto l;
  int x __attribute__((cleanup(f))) = 0;
l:
  return 0;
}
EOF

expect_error "5:3: error: jump into the scope of a variable with a cleanup" <<'EOF'
void f(int *p);
int main(int argc, char **argv) {
  switch (argc) {
    int x __attribute__((cleanup(f)));
  case 1:
    return 1;
  }
  return 0;
}
EOF

# The same for a variable-length array: a jump into its scope would
# skip its allocation.
expect_error "2:3: error: jump into the scope of a variable-length array" <<'EOF'
void f(int n) {
  goto l;
  int a[n];
l:
  a[0] = 0;
}
EOF

expect_error "4:3: error: jump into the scope of a variable-length array" <<'EOF'
void f(int n) {
  switch (n) {
    int a[n];
  case 1:
    a[0] = 1;
  }
}
EOF

# GNU extensions mucc leaves out, which used to miscompile
expect_error "1:30: error: nested functions are not supported" <<'EOF'
int main(void) { int f(void) { return 1; } return f(); }
EOF

expect_error "1:30: error: a variable length array in a struct is not supported" <<'EOF'
void f(int n) { struct { int a[n]; } s; }
EOF

expect_error "1:14: error: an empty union takes no value" <<'EOF'
union {} u = {1};
EOF

expect_error "1:47: error: 'nothere' is not a function" <<'EOF'
int main(void) { int x __attribute__((cleanup(nothere))); return 0; }
EOF

expect_error "1:38: error: attribute 'cleanup' is not supported on a global variable" <<'EOF'
void f(int *p); int x __attribute__((cleanup(f)));
EOF

expect_error "1:53: error: cleanup function 'f' must take one parameter" <<'EOF'
void f(void); int main(void) { int x __attribute__((cleanup(f))); return 0; }
EOF

expect_clean 'cleanup and return: no false warnings' <<'EOF'
void f(int *p);
int g(void) {
  int x __attribute__((cleanup(f))) = 1;
  if (x)
    return x;
  {
    int y __attribute__((cleanup(f))) = 2;
    return y;
  }
}
EOF

expect_error "1:35: error: attribute 'aligned' is not supported here" <<'EOF'
int f(int (*p)(int __attribute__((aligned(16)))));
EOF

expect_error "1:27: error: a weak function must not be static" <<'EOF'
static int __attribute__((weak)) f(void) { return 0; }
EOF

expect_error "1:39: error: attribute 'weak' is not supported on a local variable" <<'EOF'
int main(void) { int x __attribute__((weak)); return 0; }
EOF

expect_error "1:30: error: attribute 'weak' is not supported on a typedef" <<'EOF'
typedef int T __attribute__((weak));
EOF

expect_error "1:29: error: attribute 'weak' is not supported on a parameter" <<'EOF'
void f(int x __attribute__((weak)));
EOF


expect_error "1:22: error: requested alignment is not a positive power of 2" <<'EOF'
int x __attribute__((aligned(3)));
EOF

expect_error "1:22: error: attribute 'constructor' is not supported on a variable" <<'EOF'
int x __attribute__((constructor));
EOF

expect_error "1:39: error: attribute 'destructor' is not supported on a local variable" <<'EOF'
int main(void) { int x __attribute__((destructor)); return 0; }
EOF

expect_error "1:16: error: constructor priorities must be from 0 to 65535" <<'EOF'
__attribute__((constructor(70000))) void f(void) {}
EOF

# __int128
expect_error "1:18: error: _Atomic __int128 is not supported" <<'EOF'
_Atomic __int128 x;
EOF

expect_error "1:25: error: a bit-field of type __int128 is not supported" <<'EOF'
struct S { __int128 a : 3; };
EOF

expect_error "1:30: error: switch on __int128 is not supported" <<'EOF'
void f(__int128 x) { switch (x) {} }
EOF

expect_error "1:26: error: a 128-bit constant must be a 64-bit integer constant converted to __int128" <<'EOF'
__int128 x = (__int128)1 << 64;
EOF

# A directive's macro name must be on its line.
expect_error "1:2: error: no macro name given in #define directive" <<'EOF'
#define
int main(void) { return 0; }
EOF

expect_error "2:2: error: no macro name given in #undef directive" <<'EOF'
int x = 1;
#undef
int y = 2;
EOF

expect_error "1:2: error: no macro name given in #ifdef directive" <<'EOF'
#ifdef
int z;
#endif
EOF

expect_error "1:8: error: macro name must be an identifier" <<'EOF'
#ifdef 3
#endif
EOF

# #include of a name that isn't a header name
expect_error "1:10: error: expected a filename" <<'EOF'
#include foo
EOF

expect_error "2:10: error: expected a filename" <<'EOF'
#define H int
#include H
EOF

expect_error "1:40: error: overflow builtins on __int128 are not supported" <<'EOF'
int f(__int128 a) { __int128 r; return __builtin_add_overflow(a, a, &r); }
EOF

# _Complex
expect_error "1:10: error: only floating-point complex types are supported" <<'EOF'
_Complex int x;
EOF

expect_error "1:56: error: complex numbers can't be compared with < or >" <<'EOF'
int f(_Complex double z, _Complex double w) { return z < w; }
EOF

expect_error "1:37: error: invalid operands to a complex number" <<'EOF'
int f(_Complex double z) { return z % 2; }
EOF

expect_error "1:42: error: invalid operands" <<'EOF'
void f(double *p, _Complex double z) { p + z; }
EOF

expect_error "1:23: error: invalid operands" <<'EOF'
void f(double *p) { p + 1.5; }
EOF

expect_error "1:38: error: __builtin_complex needs two floating-point numbers of the same type" <<'EOF'
double f(_Complex double z) { return __builtin_complex(1.0, 2.0f); }
EOF

expect_error "1:32: error: a bit-field can't be complex" <<'EOF'
struct S { _Complex double z : 3; };
EOF

expect_error "1:25: error: _Atomic _Complex is not supported" <<'EOF'
_Atomic _Complex double z;
EOF

expect_error "1:37: error: switch on '_Complex double', which is not an integer" <<'EOF'
void f(_Complex double z) { switch (z) {} }
EOF

# asm statements with operands
expect_error "1:29: error: labels in an asm statement need 'asm goto'" <<'EOF'
void f(void) { asm("" : : : : out); out:; }
EOF

expect_error "1:36: error: use of undeclared label" <<'EOF'
void f(void) { asm goto("" : : : : nowhere); }
EOF

expect_error "1:16: error: %l in an asm template must name one of its goto labels" <<'EOF'
void f(void) { asm goto("jmp %l1" : : : : out); out:; }
EOF

expect_error "1:26: error: asm constraint 't' is not supported" <<'EOF'
void f(int x) { asm("" : "=t"(x)); }
EOF

expect_error "1:34: error: an operand of type 'long double' can't go in an SSE register" <<'EOF'
void f(long double x) { asm("" : "=x"(x)); }
EOF

expect_error "1:26: error: alternative asm constraints are not supported" <<'EOF'
void f(int x) { asm("" : "=r,m"(x)); }
EOF

expect_error "1:26: error: an output operand's constraint must start with '=' or '+'" <<'EOF'
void f(int x) { asm("" : "r"(x)); }
EOF

expect_error "1:33: error: an asm output must be an lvalue" <<'EOF'
void f(int x) { asm("" : "=r"(x + 1)); }
EOF

expect_error "1:34: error: an asm memory operand must be an lvalue" <<'EOF'
void f(int x) { asm("" : : "m"(x + 1)); }
EOF

expect_error "1:31: error: an operand of type 'double' can't go in a general register" <<'EOF'
void f(double d) { asm("" : : "r"(d)); }
EOF

expect_error "1:37: error: unknown register name 'foo' in asm" <<'EOF'
void f(int x) { asm("" : : "r"(x) : "foo"); }
EOF

expect_error "1:37: error: an asm statement can't clobber rsp" <<'EOF'
void f(int x) { asm("" : : "r"(x) : "rsp"); }
EOF

expect_error "1:36: error: register rax is used twice in this asm statement" <<'EOF'
void f(int x) { asm("" : : "a"(x), "a"(x)); }
EOF

expect_error "1:37: error: register rax is used twice in this asm statement" <<'EOF'
void f(int x) { asm("" : "=&a"(x) : "a"(x)); }
EOF

expect_error "1:28: error: register rax is used twice in this asm statement" <<'EOF'
void f(int x) { asm("" : : "a"(x) : "rax"); }
EOF

expect_error "1:28: error: a matching constraint must name an output" <<'EOF'
void f(int x) { asm("" : : "1"(x)); }
EOF

expect_error "1:32: error: an asm constant operand must be a constant" <<'EOF'
void f(int x) { asm("" : : "i"(x)); }
EOF

expect_error "1:17: error: asm operand number 2 out of range" <<'EOF'
void f(int x) { asm("%2" : : "r"(x)); }
EOF

expect_error "1:17: error: undefined asm operand name 'y'" <<'EOF'
void f(int x) { asm("%[y]" : : [x] "r"(x)); }
EOF

expect_error "1:17: error: asm operand modifier 'z' is not supported" <<'EOF'
void f(int x) { asm("%z0" : : "r"(x)); }
EOF

expect_error "1:210: error: not enough registers for this asm statement's operands" <<'EOF'
void f(int a,int b,int c,int d,int e,int g,int h,int i,int j,int k,int l,int m,int n,int o,int p) { asm("" : : "r"(a),"r"(b),"r"(c),"r"(d),"r"(e),"r"(g),"r"(h),"r"(i),"r"(j),"r"(k),"r"(l),"r"(m),"r"(n),"r"(o),"r"(p)); }
EOF

expect_ok 'an input and an output in the same register, and other clobbers' <<'EOF'
void f(int x) { asm("" : "=a"(x) : "a"(x)); asm volatile("" ::: "memory", "cc", "xmm0", "%r11"); }
EOF

# asm labels: a local one only names a register variable's register
expect_error "1:22: error: an asm label is only supported on a register variable" <<'EOF'
void f(void) { int x asm("rax"); }
EOF

expect_error "1:29: error: attribute 'common' is not supported on a function" <<'EOF'
void f(void) __attribute__((common));
EOF

# mode and transparent_union
expect_error "1:51: error: mode 'QI' is not supported for type 'struct S'" <<'EOF'
typedef struct S { int a; } T __attribute__((mode(QI)));
EOF

expect_error "1:30: error: attribute 'transparent_union' needs a union" <<'EOF'
typedef int T __attribute__((transparent_union));
EOF

expect_error "4:24: error: argument 1 of 'f' fits no member of 'union (anonymous)'" <<'EOF'
typedef union { int *i; long *l; } U __attribute__((transparent_union));
int f(U u);
double d;
int g(void) { return f(&d) + f(&d); }
EOF

expect_error "1:31: error: invalid register name 'foo'" <<'EOF'
void f(void) { register int x asm("foo"); }
EOF

expect_error "1:34: error: only an integer or a pointer can be a register variable" <<'EOF'
void f(void) { register double x asm("rax"); }
EOF

expect_warning "1:3: warning: unknown attribute 'bogus' ignored [-Wattributes]" <<'EOF'
[[bogus]] int x;
EOF

expect_warning "1:10: warning: unknown attribute 'clang::optnone' ignored [-Wattributes]" <<'EOF'
[[clang::optnone]] int f(void) { return 0; }
EOF

# Unused parameters get no warning (as with gcc without -Wextra)
expect_clean 'unused parameter' <<'EOF'
int f(int unused, int n, int a[n]) { return 0; }
EOF

# A struct is in scope for its own members' function pointer parameters
expect_clean 'struct in its own member function pointer' <<'EOF'
struct ar { const char *name; int (*w)(const struct ar *, int); };
static int wz(const struct ar *a, int x) { return a->name[0] + x; }
static struct ar z = { "zip", wz };
int g(const struct ar *p) { p = &z; return p->w(p, 1); }
EOF

# A local array's address isn't a constant, even where a global of that
# name exists.
expect_error "2:41: error: not a compile-time constant" <<'EOF'
int a[3];
int f(void) { int a[3]; static int *p = a; return p == a; }
EOF

expect_error "1:24: error: invalid argument type to unary '+'" <<'EOF'
int f(int *p) { return +p; }
EOF

expect_error "1:14: error: #pragma pack takes 1, 2, 4, 8 or 16" <<'EOF'
#pragma pack(3)
EOF

# A packed bit-field is loaded and stored in at most 8 bytes.
expect_error "2:34: error: a packed bit-field spanning more than 8 bytes is not supported" <<'EOF'
#pragma pack(1)
struct s { char c; int a:4; long b:61; };
EOF

# An enumerator can't reuse a name from its own scope, but may hide one
# from an outer scope.
expect_error "2:14: error: redeclaration of 'A'" <<'EOF'
enum E { A };
enum E2 { B, A };
EOF

expect_error "2:8: error: redeclaration of 'x'" <<'EOF'
int x;
enum { x };
EOF

expect_ok 'an enumerator hiding a global' <<'EOF'
int A;
int f(void) { enum { A = 5 }; return A; }
EOF

# Nothing const can be modified.
expect_error "2:16: error: cannot modify read-only variable 'x'" <<'EOF'
const int x = 1;
void f(void) { x = 2; }
EOF

expect_error "1:24: error: cannot modify a read-only location" <<'EOF'
void f(const int *p) { *p = 3; }
EOF

expect_error "1:25: error: cannot modify a read-only location" <<'EOF'
void f(const int *p) { (*p)++; }
EOF

expect_error "2:32: error: cannot modify a read-only location" <<'EOF'
struct S { int a; };
void f(const struct S *p) { p->a = 1; }
EOF

expect_error "2:35: error: cannot modify a read-only location" <<'EOF'
struct S { const int a; };
void f(struct S *p, struct S q) { *p = q; }
EOF

expect_error "1:25: error: cannot modify read-only variable 'p'" <<'EOF'
void f(char *const p) { p = 0; }
EOF

expect_error "2:28: error: cannot convert 'const char **' to 'char **' in argument 1 of 'g' (use a cast if this is intended)" <<'EOF'
void g(char **);
void f(const char **p) { g(p); }
EOF

expect_warning "1:45: warning: initialization discards the 'const' qualifier of 'const int *' [-Wdiscarded-qualifiers]" <<'EOF'
void f(int x) { const int *p = &x; int *q = p; *q = 1; }
EOF

# A pointer to const, and a const pointer to a struct completed later.
expect_clean 'const pointers' <<'EOF'
struct S;
void g(const struct S *);
struct S { int a; };
void h(struct S *s, const char *p, char *q) {
  g(s);
  void (*fp)(const struct S *) = g;
  fp(s);
  p = q;
  p++;
}
EOF

# A header found through -I is the user's code, not a system header: its
# type errors are reported, and -MMD lists it.
mkdir -p $tmp/inc
echo 'static inline int *conv(char *c) { return c; }' > $tmp/inc/conv.h
echo '#include "conv.h"' > $tmp/useconv.c
if $mucc -I$tmp/inc -c -o $tmp/useconv.o $tmp/useconv.c 2> $tmp/err ||
   ! grep -q "cannot convert 'char \*' to 'int \*'" $tmp/err; then
  echo "testing error in a header found through -I ... failed"
  cat $tmp/err
  exit 1
fi
echo "testing error in a header found through -I ... passed"

echo 'static inline int one(void) { return 1; }' > $tmp/inc/one.h
printf '#include "one.h"\nint main(void) { return one(); }\n' > $tmp/useone.c
$mucc -I$tmp/inc -MMD -c -o $tmp/useone.o $tmp/useone.c
if ! grep -q 'one\.h' $tmp/useone.d; then
  echo "testing -MMD lists a header found through -I ... failed"
  cat $tmp/useone.d
  exit 1
fi
echo "testing -MMD lists a header found through -I ... passed"

# A struct is not a number: as a condition, an operand or a cast target
# it's an error, not code that quietly uses its address.
expect_error "2:20: error: 'struct T' used where a scalar is required" <<'EOF'
struct T { int v; } t;
void f(void) { if (t) {} }
EOF

expect_error "2:29: error: 'struct T' used where a scalar is required" <<'EOF'
struct T { int v; } t;
void f(void) { long p = 1 * t; }
EOF

expect_error "2:24: error: switch on 'struct T', which is not an integer" <<'EOF'
struct T { int v; } t;
void f(void) { switch (t) {} }
EOF

expect_error "2:21: error: cannot convert 'struct T' to 'int'" <<'EOF'
struct T { int v; } t;
void f(void) { (int)t; }
EOF

# s[0] on a struct s, which used to crash
expect_error "2:17: error: invalid operands" <<'EOF'
struct T { int v; } t;
void f(void) { t[0].v; }
EOF

expect_error "2:32: error: 'struct T' used where a scalar is required" <<'EOF'
struct T { int v; };
struct T *p = &(struct T){1} & (struct T){2};
EOF

expect_error "2:5: error: redefinition of x" <<'EOF'
int x = 1;
int x = 1;
EOF

# These used to crash.
expect_error "1:20: error: member name omitted" <<'EOF'
struct T { int *a, ; } t;
EOF

expect_error "1:9: error: unclosed char literal" <<'EOF'
int x = 'abc;
EOF

expect_error "1:11: error: unclosed string literal" <<'EOF'
char *s = "abc;
EOF

expect_error "1:9: error: empty character constant" <<'EOF'
int x = '';
EOF

expect_error "1:1: error: unterminated attribute" <<'EOF'
[[gnu::constr(ctor(101)]] static void f(void) {}
EOF

expect_ok 'a thread-local tentative definition twice' <<'EOF'
_Thread_local int v1;
_Thread_local int v1;
int *get(void) { return &v1; }
EOF

expect_ok 'a struct in its own places' <<'EOF'
struct T { int v; } t, u;
struct T get(int c) { return c ? t : u; }
void f(void) { (void)t; t = u; t = get(1); if (get(0).v) {} }
EOF

echo OK
