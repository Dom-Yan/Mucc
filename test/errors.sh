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

#---------- Warnings ---------------------------------------------------------

expect_warning "3:7: warning: unused variable 'unused'" <<'EOF'
int f(void) {
  int used = 1;
  int unused = used;
  return 0;
}
EOF

expect_warning "4:1: warning: control reaches end of non-void function 'f'" <<'EOF'
int f(int x) {
  if (x)
    return 1;
}
EOF

expect_warning "1:49: warning: control reaches end of non-void function 'g'" <<'EOF'
int g(int x) { switch (x) { case 1: return 1; } }
EOF

expect_warning "1:35: warning: control reaches end of non-void function 'h'" <<'EOF'
int h(void) { for (;;) { break; } }
EOF

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

printf 'int f(void) { int unused; }\n' > $tmp/t.c
if $mucc -w -c -o $tmp/t.o $tmp/t.c 2> $tmp/err && [ ! -s $tmp/err ]; then
    echo "testing clean '-w silences warnings' ... passed"
else
    echo "testing clean '-w silences warnings' ... failed"; exit 1
fi

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
for a in retain weakref vector_size mode ifunc naked target target_clones \
         transparent_union common nocommon copy symver scalar_storage_order \
         noinit persistent hardbool; do
  expect_error "1:22: error: attribute '$a' is not supported" <<EOF
int x __attribute__((__${a}__(1)));
EOF
done

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

expect_error "2:24: error: alignment above 16 on a local variable is not supported yet" <<'EOF'
int main(void) {
  int x __attribute__((aligned(32)));
  return 0;
}
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

expect_warning "1:3: warning: unknown attribute 'bogus' ignored" <<'EOF'
[[bogus]] int x;
EOF

expect_warning "1:10: warning: unknown attribute 'clang::optnone' ignored" <<'EOF'
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

echo OK
