#!/bin/bash
mucc=$1

# With --libc=mucc, programs are only linked statically: shared libraries
# are an error.
musl=
[[ $mucc == *--libc=mucc* ]] && musl=1

tmp=`mktemp -d /tmp/mucc-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
echo > $tmp/empty.c

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

# -o
rm -f $tmp/out
./mucc -c -o $tmp/out $tmp/empty.c
[ -f $tmp/out ]
check -o

# Temporary files go in $TMPDIR (or /tmp, or the current directory with
# no /tmp, as in an empty container)
echo 'int main() { return 0; }' > $tmp/tmpdir.c
TMPDIR=$tmp/nothere $mucc -o $tmp/tmpdir $tmp/tmpdir.c 2>&1 |
    grep -q "cannot create a temporary file $tmp/nothere/mucc-"
check 'TMPDIR'

# --help
$mucc --help 2>&1 | grep -q mucc
check --help

# --version
$mucc --version | grep -q '^mucc [0-9]'
check --version

# -S
echo 'int main() {}' | $mucc -S -o- -xc - | grep -q 'main:'
check -S

# Default output file
rm -f $tmp/out.o $tmp/out.s
echo 'int main() {}' > $tmp/out.c
(cd $tmp; $OLDPWD/$mucc -c out.c)
[ -f $tmp/out.o ]
check 'default output file'

(cd $tmp; $OLDPWD/$mucc -c -S out.c)
[ -f $tmp/out.s ]
check 'default output file'

# Multiple input files
rm -f $tmp/foo.o $tmp/bar.o
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$mucc -c $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.o ] && [ -f $tmp/bar.o ]
check 'multiple input files'

rm -f $tmp/foo.s $tmp/bar.s
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$mucc -c -S $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.s ] && [ -f $tmp/bar.s ]
check 'multiple input files'

# Run linker
rm -f $tmp/foo
echo 'int main() { return 0; }' | $mucc -o $tmp/foo -xc -xc -
$tmp/foo
check linker

rm -f $tmp/foo
echo 'int bar(); int main() { return bar(); }' > $tmp/foo.c
echo 'int bar() { return 42; }' > $tmp/bar.c
$mucc -o $tmp/foo $tmp/foo.c $tmp/bar.c
$tmp/foo
[ "$?" = 42 ]
check linker

# a.out
rm -f $tmp/a.out
echo 'int main() {}' > $tmp/foo.c
(cd $tmp; $OLDPWD/$mucc foo.c)
[ -f $tmp/a.out ]
check a.out

# -E
echo foo > $tmp/out
echo "#include \"$tmp/out\"" | $mucc -E -xc - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $mucc -E -o $tmp/out2 -xc -
cat $tmp/out2 | grep -q foo
check '-E and -o'

# -E line markers, as gcc writes them: 1 entering an include, 2 returning,
# 3 a system header. A few empty lines stand in for a short jump.
mkdir $tmp/lm
echo 'int a;' > $tmp/lm/a.h
printf '#include "a.h"\nint b;\n\n\nint c = __LINE__;\n#line 100 "other.c"\nint d;\n' > $tmp/lm/main.c
(cd $tmp/lm; $OLDPWD/$mucc -E main.c) > $tmp/lm/out
cat > $tmp/lm/expected <<'EOF'
# 1 "main.c"

# 1 "./a.h" 1
int a;
# 2 "main.c" 2
int b;


int c = 5;
# 100 "other.c"
int d;
EOF
diff $tmp/lm/out $tmp/lm/expected
check '-E line markers'

echo '#include <errno.h>' | $mucc -E -xc - | grep -q '/errno\.h" 1 3$'
check '-E line marker of a system header'

echo '#include <errno.h>' | $mucc -E -P -xc - | grep -q '^#'
[ $? = 1 ]
check '-E -P'

# -E keeps adjacent string literals as they are, on their line: they are
# joined after preprocessing.
printf '#define S(a) f(#a "b\\n", L"c" "d")\nS(x);\n' | $mucc -E -P -xc - |
  grep -qx 'f("x" "b\\n", L"c" "d");'
check '-E keeps adjacent strings'

# -E output compiled again reports errors where they were written, and
# no warnings from system headers.
printf 'int x;\nint y = ;\n' > $tmp/lm/bad.h
printf '\n\n#include "bad.h"\n' > $tmp/lm/bad.c
(cd $tmp/lm; $OLDPWD/$mucc -E bad.c) > $tmp/lm/bad.i
$mucc -c -o /dev/null -xc $tmp/lm/bad.i 2>&1 | grep -q '^\./bad\.h:2:'
check '-E output compiled again'

printf 'int f(void) {\n  return 1;\n}\n' > $tmp/lm/f.h
echo '#include "f.h"' > $tmp/lm/f.c
(cd $tmp/lm; $OLDPWD/$mucc -E f.c) > $tmp/lm/f.i
$mucc -g -c -o $tmp/lm/f.o -xc $tmp/lm/f.i
objdump --dwarf=decodedline $tmp/lm/f.o | grep -q '^\./f\.h  *2  *0x'
check '-E output compiled again, debug info'

printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n' |
  $mucc -Iinclude -E -o $tmp/lm/sys.i -xc -
[ -z "$($mucc -c -o /dev/null -xc $tmp/lm/sys.i 2>&1)" ]
check '-E output of system headers compiled again'

# -I
mkdir $tmp/dir
echo foo > $tmp/dir/i-option-test
echo "#include \"i-option-test\"" | $mucc -I$tmp/dir -E -xc - | grep -q foo
check -I

# -D
echo foo | $mucc -Dfoo -E -xc - | grep -qx 1
check -D

# -D
echo foo | $mucc -Dfoo=bar -E -xc - | grep -q bar
check -D

# -U
echo foo | $mucc -Dfoo=bar -Ufoo -E -xc - | grep -q foo
check -U

# --libc=system is the system's C library, as without it; others are errors.
echo 'int main() { return 0; }' | $mucc --libc=system -o $tmp/libc -xc - && $tmp/libc
check --libc=system

echo 'int main() { return 0; }' | $mucc --libc=nope -o $tmp/libc -xc - 2>&1 |
  grep -q 'unknown C library: --libc=nope'
check '--libc= unknown'

# ignored options
$mucc -c -O -Wall -g -std=c11 -ffreestanding -fno-builtin \
         -fno-omit-frame-pointer -fno-stack-protector -fno-strict-aliasing \
         -m64 -mno-red-zone -w -o /dev/null $tmp/empty.c
check 'ignored options'

# BOM marker
printf '\xef\xbb\xbfxyz\n' | $mucc -E -o- -xc - | grep -q '^xyz'
check 'BOM marker'

# Inline functions
echo 'inline void foo() {}' > $tmp/inline1.c
echo 'inline void foo() {}' > $tmp/inline2.c
echo 'int main() { return 0; }' > $tmp/inline3.c
$mucc -o /dev/null $tmp/inline1.c $tmp/inline2.c $tmp/inline3.c
check inline

echo 'extern inline void foo() {}' > $tmp/inline1.c
echo 'int foo(); int main() { foo(); }' > $tmp/inline2.c
$mucc -o /dev/null $tmp/inline1.c $tmp/inline2.c
check inline

echo 'static inline void f1() {}' | $mucc -o- -S -xc - | grep -v -q f1:
check inline

echo 'static inline void f1() {} void foo() { f1(); }' | $mucc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f1(); }' | $mucc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f1(); }' | $mucc -o- -S -xc - | grep -v -q f2:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f2(); }' | $mucc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f1() {} static inline void f2() { f1(); } void foo() { f2(); }' | $mucc -o- -S -xc - | grep -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() {}' | $mucc -o- -S -xc - | grep -v -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() {}' | $mucc -o- -S -xc - | grep -v -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f1(); }' | $mucc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f1(); }' | $mucc -o- -S -xc - | grep -q f2:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f2(); }' | $mucc -o- -S -xc - | grep -q f1:
check inline

echo 'static inline void f2(); static inline void f1() { f2(); } static inline void f2() { f1(); } void foo() { f2(); }' | $mucc -o- -S -xc - | grep -q f2:
check inline

# -idirafter
mkdir -p $tmp/dir1 $tmp/dir2
echo foo > $tmp/dir1/idirafter
echo bar > $tmp/dir2/idirafter
echo "#include \"idirafter\"" | $mucc -I$tmp/dir1 -I$tmp/dir2 -E -xc - | grep -q foo
check -idirafter
echo "#include \"idirafter\"" | $mucc -idirafter $tmp/dir1 -I$tmp/dir2 -E -xc - | grep -q bar
check -idirafter

# -fcommon
echo 'int foo;' | $mucc -S -o- -xc - | grep -q '\.comm foo'
check '-fcommon (default)'

echo 'int foo;' | $mucc -fcommon -S -o- -xc - | grep -q '\.comm foo'
check '-fcommon'

# -fno-common
echo 'int foo;' | $mucc -fno-common -S -o- -xc - | grep -q '^foo:'
check '-fno-common'

# attributes common and nocommon override -fcommon and -fno-common
echo 'int foo __attribute__((nocommon));' | $mucc -fcommon -S -o- -xc - | grep -q '^foo:'
check 'attribute nocommon'

echo 'int foo __attribute__((common));' | $mucc -fno-common -S -o- -xc - | grep -q '\.comm foo'
check 'attribute common'

# Data: runs of zeros as .zero, other bytes 16 to a line
echo 'int a[1000] = {[999] = 1};' | $mucc -S -o- -xc - | grep -A2 '^a:' | tr -d '\n ' |
  grep -q '^a:\.zero3996\.byte1,0,0,0$'
check 'zeros in data as .zero'

# -include
echo foo > $tmp/out.h
echo bar | $mucc -include $tmp/out.h -E -o- -xc - | grep -q -z 'foo.*bar'
check -include
echo NULL | $mucc -Iinclude -include stdio.h -E -o- -xc - | grep -q 0
check -include

# Output read by something that stops early is no crash: no message.
seq 100000 | sed 's/.*/int x&;/' > $tmp/long.c
$mucc -E -o- $tmp/long.c 2> $tmp/pipe.err | head -1 > /dev/null
[ ! -s $tmp/pipe.err ]
check 'output pipe closed early'

# -x
echo 'int x;' | $mucc -c -xc -o $tmp/foo.o -
check -xc
echo 'x:' | $mucc -c -x assembler -o $tmp/foo.o -
check '-x assembler'

echo 'int x;' > $tmp/foo.c
$mucc -c -x assembler -x none -o $tmp/foo.o $tmp/foo.c
check '-x none'

# -x applies to the files after it, not to those before it
echo 'int xmain(void); int main() { return xmain(); }' > $tmp/xmain.c
$mucc -c -o $tmp/xmain.o $tmp/xmain.c
echo 'int xmain(void) { return 0; }' > $tmp/xother
$mucc -o $tmp/xprog $tmp/xmain.o -xc $tmp/xother && $tmp/xprog
check '-x after an object file'

# -E
echo foo | $mucc -E - | grep -q foo
check -E

# .a file
echo 'void foo() {}' | $mucc -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | $mucc -c -xc -o $tmp/bar.o -
ar rcs $tmp/foo.a $tmp/foo.o $tmp/bar.o
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$mucc -o $tmp/foo $tmp/main.c $tmp/foo.a
check '.a'

# .so file
echo 'void foo() {}' | cc -fPIC -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | cc -fPIC -c -xc -o $tmp/bar.o -
cc -shared -o $tmp/foo.so $tmp/foo.o $tmp/bar.o
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
if [ $musl ]; then
    $mucc -o $tmp/foo $tmp/main.c $tmp/foo.so 2>&1 |
        grep -q 'foo.so: a shared library needs --libc=system'
else
    $mucc -o $tmp/foo $tmp/main.c $tmp/foo.so
fi
check '.so'

$mucc -hashmap-test
check 'hashmap'

# -M
echo '#include "out2.h"' > $tmp/out.c
echo '#include "out3.h"' >> $tmp/out.c
touch $tmp/out2.h $tmp/out3.h
$mucc -M -I$tmp $tmp/out.c | grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h'
check -M

# -MF
$mucc -MF $tmp/mf -M -I$tmp $tmp/out.c
grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h' $tmp/mf
check -MF

# -MP
$mucc -MF $tmp/mp -MP -M -I$tmp $tmp/out.c
grep -q '^.*/out2.h:' $tmp/mp
check -MP
grep -q '^.*/out3.h:' $tmp/mp
check -MP

# -MT
$mucc -MT foo -M -I$tmp $tmp/out.c | grep -q '^foo:'
check -MT
$mucc -MT foo -MT bar -M -I$tmp $tmp/out.c | grep -q '^foo bar:'
check -MT

# -MD
echo '#include "out2.h"' > $tmp/md2.c
echo '#include "out3.h"' > $tmp/md3.c
(cd $tmp; $OLDPWD/$mucc -c -MD -I. md2.c md3.c)
grep -q -z '^md2.o:.* md2\.c .* ./out2\.h' $tmp/md2.d
check -MD
grep -q -z '^md3.o:.* md3\.c .* ./out3\.h' $tmp/md3.d
check -MD

$mucc -c -MD -MF $tmp/md-mf.d -I. $tmp/md2.c
grep -q -z '^md2.o:.*md2\.c .*/out2\.h' $tmp/md-mf.d
check -MD

# ... with -c -o, the .d goes next to the object, as with gcc
mkdir -p $tmp/objdir
echo 'int md4;' > $tmp/md4.c
$mucc -c -MD -o $tmp/objdir/md4.o $tmp/md4.c
grep -q 'md4\.c' $tmp/objdir/md4.d
check '-MD with -o in another directory'

# A dependency file doesn't end with a blank line (Linux's fixdep fails
# on one), and -MP's targets are separated by blank lines, as with gcc.
[ -n "$(tail -n 1 $tmp/objdir/md4.d)" ] && [ -n "$(tail -n 1 $tmp/mp)" ]
check 'no blank line at the end of a dependency file'
[ "$(grep -c '^$' $tmp/mp)" -eq 2 ]
check '-MP blank lines'

# -Wp,-MD,file, as Linux's kbuild writes it
$mucc -c -Wp,-MD,$tmp/wp.d -o $tmp/wp.o -I$tmp $tmp/md2.c
grep -q -z '^md2.o:.*md2\.c .*/out2\.h' $tmp/wp.d
check -Wp,-MD
$mucc -E -Wp,-DWP=42 -xc - <<< 'WP' | grep -q 42
check -Wp,-D

# -pthread defines _REENTRANT and links the threads library.
printf '#ifndef _REENTRANT\n#error\n#endif\nint main() { return 0; }\n' > $tmp/pthread.c
$mucc -pthread -o $tmp/pthread $tmp/pthread.c && $tmp/pthread
check -pthread

# Options that only tune optimization, diagnostics or hardening are
# accepted.
$mucc -pedantic -fvisibility=hidden -ffunction-sections -fdata-sections \
  -fwrapv -fPIE -fno-plt -fstack-protector-strong -fcf-protection=full \
  -c -o $tmp/ignored.o $tmp/empty.c
check 'tuning and hardening options'

# Linker flags that mean nothing in a static program don't need the
# system linker.
if [ $musl ]; then
    echo 'int main() { return 0; }' > $tmp/wl.c
    $mucc -o $tmp/wl $tmp/wl.c -rdynamic -Wl,--gc-sections,--as-needed \
      -Wl,-z,relro,-z,now,-O1,--build-id,--start-group,--end-group \
      -Wl,--hash-style=gnu 2> $tmp/wl.err && $tmp/wl && [ ! -s $tmp/wl.err ]
    check 'harmless -Wl, flags with the built-in linker'

    # ... but not one that changes what links, like -z muldefs
    $mucc -o $tmp/wl $tmp/wl.c -Wl,-z,muldefs 2>&1 |
        grep -q 'using the system linker for -z'
    check '-z muldefs needs the system linker'
fi

# -r: objects linked into one object file, as kbuild makes built-in.o;
# statics of the same name stay apart, a strong definition beats a weak
# one, and -r output can go into -r again.
cat > $tmp/ra.c <<'EOF'
static int helper(void) { return 1; }
int counter = 10;
int a_value(void) { return helper() + counter; }
EOF
cat > $tmp/rb.c <<'EOF'
static int helper(void) { return 2; }
extern int counter;
static int table[] = {100, 200};
int *b_ptr = &table[1];
int b_value(void) { return helper() + counter + *b_ptr; }
__attribute__((weak)) int pick(void) { return -1; }
EOF
echo 'int pick(void) { return 7; }' > $tmp/rc.c
cat > $tmp/rmain.c <<'EOF'
int a_value(void), b_value(void), pick(void);
int main(void) { return a_value() == 11 && b_value() == 212 && pick() == 7 ? 0 : 1; }
EOF
for f in ra rb rc rmain; do $mucc -c -o $tmp/$f.o $tmp/$f.c; done
$mucc -nostdlib -r -o $tmp/rab.o $tmp/ra.o $tmp/rb.o &&
  $mucc -nostdlib -r -o $tmp/rabc.o $tmp/rab.o $tmp/rc.o 2> $tmp/r.err &&
  [ ! -s $tmp/r.err ] && $mucc -o $tmp/r $tmp/rmain.o $tmp/rabc.o && $tmp/r
check -r

# -Map FILE: where everything went
if [ $musl ]; then
    $mucc -o $tmp/mapped $tmp/rmain.o $tmp/rabc.o -Wl,-Map,$tmp/out.map 2> $tmp/map.err &&
      [ ! -s $tmp/map.err ] && grep -q '^\.text ' $tmp/out.map &&
      grep -q ' a_value$' $tmp/out.map
    check -Map
fi

# -nostdlib: a program with its own _start, and no C library
cat > $tmp/nostdlib.c <<'EOF'
void _start(void) {
  __asm__ volatile("mov $60, %eax; mov $7, %edi; syscall");
}
EOF
$mucc -nostdlib -static -o $tmp/nostdlib $tmp/nostdlib.c
$tmp/nostdlib; [ $? -eq 7 ]
check -nostdlib

echo 'extern int bar; int foo() { return bar; }' | $mucc -fPIC -xc -c -o $tmp/foo.o -
cc -shared -o $tmp/foo.so $tmp/foo.o
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/main.c
if [ $musl ]; then
    $mucc -o $tmp/foo $tmp/main.c $tmp/foo.o
else
    $mucc -o $tmp/foo $tmp/main.c $tmp/foo.so
fi
check -fPIC

# #include_next
mkdir -p $tmp/next1 $tmp/next2 $tmp/next3
echo '#include "file1.h"' > $tmp/file.c
echo '#include_next "file1.h"' > $tmp/next1/file1.h
echo '#include_next "file2.h"' > $tmp/next2/file1.h
echo 'foo' > $tmp/next3/file2.h
$mucc -I$tmp/next1 -I$tmp/next2 -I$tmp/next3 -E $tmp/file.c | grep -q foo
check '#include_next'

# __has_include_next: a wrapper header that only includes a next one if
# there is one, as mucc's <sys/cdefs.h> does
mkdir -p $tmp/hin1 $tmp/hin2
printf '#if __has_include_next(<hin.h>)\n#include_next <hin.h>\n#else\nnone\n#endif\n' > $tmp/hin1/hin.h
echo 'wrapped' > $tmp/hin2/hin.h
echo '#include <hin.h>' > $tmp/hin.c
$mucc -I$tmp/hin1 -I$tmp/hin2 -E $tmp/hin.c | grep -q wrapped &&
    $mucc -I$tmp/hin1 -E $tmp/hin.c | grep -q none
check '__has_include_next'

# #include_next continues after the directory the current file was found
# in, even when the header was found earlier and another #include ran since.
# (It once found the same wrapper again and recursed until out of memory.)
mkdir -p $tmp/wrap0 $tmp/wrap1 $tmp/wrap2
echo 'other' > $tmp/wrap0/other.h
echo '#include_next <wrap.h>' > $tmp/wrap1/wrap.h
echo 'wrapped' > $tmp/wrap2/wrap.h
printf '#include <wrap.h>\n#include <other.h>\n#include <wrap.h>\n' > $tmp/wrap.c
[ "$( (ulimit -v 1000000; timeout 10 $mucc -I$tmp/wrap0 -I$tmp/wrap1 -I$tmp/wrap2 \
  -E $tmp/wrap.c) | grep -c wrapped)" = 2 ]
check '#include_next of a header found before'

# <tgmath.h>'s macros nested three deep expand to 1.4 MB, from tokens
# whose hidesets name many macros. Hidesets are shared, not copied for
# each token: this took over 600 MB, and takes 210.
printf '#include <tgmath.h>\nfloat f;\ndouble g(void) { return cos(sin(tan(f))); }\n' > $tmp/tgmath.c
(ulimit -v 500000; $mucc -Iinclude -c -o $tmp/tgmath.o $tmp/tgmath.c)
check 'nested <tgmath.h> macros in bounded memory'

# A default include directory also given with -I is searched once, so
# mucc's own <sys/cdefs.h> wrapper, which does #include_next of itself, is
# read once. (Read twice, it put a path that depends on where the mucc
# binary is into the debug info, and self-hosting stopped being reproducible.)
echo '#include <sys/cdefs.h>' > $tmp/cdefs.c
[ "$($mucc -Iinclude -M $tmp/cdefs.c | tr ' ' '\n' | grep 'sys/cdefs.h' | grep -vc '^/usr/')" = 1 ]
check '-I of a default include directory'

# As with gcc, -I of a system directory doesn't move it before mucc's own
# headers. (CPython passes -I/usr/include/x86_64-linux-gnu; glibc's
# <sys/cdefs.h> then came first and dropped the packed from epoll_event.)
# Only for a mucc with its include/ next to it, as stage2/mucc has none,
# and with glibc.
if [ -d "$(dirname ${mucc%% *})/include" ] && [ ! $musl ]; then
  printf '#include <sys/epoll.h>\n_Static_assert(sizeof(struct epoll_event) == 12, "");\n' > $tmp/sysdir.c
  $mucc -I/usr/include/x86_64-linux-gnu -I/usr/include -c -o $tmp/sysdir.o $tmp/sysdir.c
  check '-I of a system directory'
fi

# -static
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$mucc -static -o $tmp/foo $tmp/foo.c $tmp/bar.c
check -static
file $tmp/foo | grep -q 'statically linked'
check -static

# -shared
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
if [ $musl ]; then
    $mucc -fPIC -shared -o $tmp/foo.so $tmp/foo.c $tmp/bar.c 2>&1 |
        grep -q 'error: -shared needs --libc=system'
else
    $mucc -fPIC -shared -o $tmp/foo.so $tmp/foo.c $tmp/bar.c
fi
check -shared

# -L
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
if [ $musl ]; then
    $mucc -c -o $tmp/foo.o $tmp/foo.c && ar rcs $tmp/libfoobar.a $tmp/foo.o
else
    $mucc -fPIC -shared -o $tmp/libfoobar.so $tmp/foo.c
fi
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$mucc -o $tmp/foo $tmp/bar.c -L$tmp -lfoobar
check -L

# -Wl,
echo 'int foo() {}' | $mucc -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $mucc -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $mucc -c -o $tmp/baz.o -xc -
cc -Wl,-z,muldefs,--gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
check -Wl,

# -Xlinker
echo 'int foo() {}' | $mucc -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $mucc -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $mucc -c -o $tmp/baz.o -xc -
cc -Xlinker -z -Xlinker muldefs -Xlinker --gc-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
check -Xlinker

# __attribute__((weak)), with ld and with mucc's own linker (-static):
# a weak definition gives way to a strong one, a weak variable too, and a
# used weak declaration with no definition anywhere is 0.
cat > $tmp/weak1.c <<'EOF'
int __attribute__((weak)) val(void) { return 1; }
int wv __attribute__((weak)) = 5;
extern int maybe(void) __attribute__((weak));
int main(void) { return val() * 100 + wv * 10 + (maybe ? maybe() : 0); }
EOF
cat > $tmp/weak2.c <<'EOF'
int val(void) { return 2; }
int wv = 7;
int maybe(void) { return 3; }
EOF
for ld in '' -static; do
  $mucc $ld -o $tmp/weak $tmp/weak1.c && $tmp/weak
  [ $? = 150 ]
  check "weak symbols, alone $ld"
  $mucc $ld -o $tmp/weak $tmp/weak1.c $tmp/weak2.c && $tmp/weak
  [ $? = $(( (273) % 256 )) ]
  check "weak symbols, overridden $ld"
done

# __attribute__((used)) keeps a static inline function nothing calls.
printf 'static inline int dropped(void) { return 1; }\n__attribute__((used)) static inline int kept(void) { return 2; }\n' > $tmp/used.c
$mucc -S -o $tmp/used.s $tmp/used.c && grep -q '^kept:' $tmp/used.s && ! grep -q '^dropped:' $tmp/used.s
check 'attribute used'

# alias, visibility and gnu_inline in the symbol table
cat > $tmp/sym.c <<'EOF'
int target(void) { return 1; }
int alias_fn(void) __attribute__((alias("target")));
__attribute__((visibility("hidden"))) int hid(void) { return 2; }
extern inline __attribute__((gnu_inline)) int gi_ext(void) { return 3; }
inline __attribute__((gnu_inline)) int gi_plain(void) { return 4; }
int use(void) { return gi_ext() + gi_plain(); }
EOF
$mucc -c -o $tmp/sym.o $tmp/sym.c && readelf -sW $tmp/sym.o > $tmp/sym.txt &&
  grep -qE 'FUNC +GLOBAL +DEFAULT +[0-9]+ alias_fn$' $tmp/sym.txt &&
  grep -qE 'FUNC +GLOBAL +HIDDEN +[0-9]+ hid$' $tmp/sym.txt &&
  grep -qE 'FUNC +GLOBAL +DEFAULT +[0-9]+ gi_plain$' $tmp/sym.txt &&
  grep -qE 'NOTYPE +GLOBAL +DEFAULT +UND gi_ext$' $tmp/sym.txt
check 'attributes alias, visibility and gnu_inline'

# -std= sets __STDC_VERSION__ (none in C89); the default is C17.
std_version() {
  echo __STDC_VERSION__ | $mucc "$@" -E -xc - | tail -1
}
[ "$(std_version)" = 201710L ] && [ "$(std_version -std=gnu99)" = 199901L ] &&
  [ "$(std_version -std=c11)" = 201112L ] && [ "$(std_version -std=c2x)" = 202311L ] &&
  [ "$(std_version -ansi)" = __STDC_VERSION__ ] &&
  ! $mucc -std=c3000 -E -xc - < /dev/null 2> /dev/null
check -std=

# -dumpmachine prints the target, as gcc does
[ "$($mucc -dumpmachine)" = x86_64-linux-gnu ]
check -dumpmachine

# -dumpversion, -print-multiarch, -print-file-name= and -print-prog-name=,
# which build tools ask
$mucc -dumpversion | grep -q '^[0-9][0-9.]*$'
check -dumpversion
[ "$($mucc -print-multiarch)" = x86_64-linux-gnu ]
check -print-multiarch
[ "$($mucc -print-file-name=libfoo.a)" = libfoo.a ]
check -print-file-name=
[ "$($mucc -print-prog-name=ld)" = ld ]
check -print-prog-name=

# -v prints the version, and with no input files, does nothing else
$mucc -v 2>&1 | grep -q '^mucc version [0-9]'
check '-v'
echo 'int main() { return 0; }' > $tmp/v.c
$mucc -v -o $tmp/v $tmp/v.c 2> /dev/null && $tmp/v
check '-v with a file'

# -isystem: searched after -I, as a system directory
mkdir -p $tmp/isys1 $tmp/isys2
echo foo > $tmp/isys1/isys
echo bar > $tmp/isys2/isys
echo '#include "isys"' | $mucc -isystem $tmp/isys1 -I$tmp/isys2 -E -xc - | grep -q bar
check -isystem
echo '#include <isys>' | $mucc -isystem $tmp/isys1 -E -xc - | grep -q foo
check -isystem

# Flags for optimizations, debug paths and hardening are accepted and
# ignored, as in Debian's default CFLAGS
$mucc -funroll-loops -finline-functions -fno-inline -ftree-vectorize \
    -ffile-prefix-map=/a=/b -fdebug-prefix-map=/a=/b -pie -static-libgcc \
    -ftrivial-auto-var-init=zero -fmax-errors=5 -save-temps \
    -o $tmp/v $tmp/v.c && $tmp/v
check 'ignored flags'

# Wide code: a chain of 50,000 `+` nests that deep in the parser, and
# 50,000 globals must not take quadratic time
{
    printf 'int f(int a) { return a'
    for i in $(seq 50000); do printf '+a'; done
    printf '; }\nint main() { return f(1) != 50001; }\n'
} > $tmp/wide.c
$mucc -o $tmp/wide $tmp/wide.c && $tmp/wide
check 'a chain of 50,000 operators'
{
    for i in $(seq 50000); do echo "int g$i;"; done
    echo 'int main() { return g7; }'
} > $tmp/globals.c
timeout 10 $mucc -o $tmp/globals $tmp/globals.c && $tmp/globals
check '50,000 globals'

# -MD with -c -o: the target is the object, as with gcc
mkdir -p $tmp/obj
echo 'int x;' > $tmp/md.c
$mucc -MD -c -o $tmp/obj/md.o $tmp/md.c && grep -q "^$tmp/obj/md.o:" $tmp/obj/md.d
check '-MD target with -o'

# -march= and -mtune= are accepted and ignored
echo 'int main() { return 0; }' > $tmp/march.c
$mucc -march=native -mtune=generic -o $tmp/march $tmp/march.c && $tmp/march
check '-march= -mtune='

# x86-64's SSE and SSE2 are on; -msse4.1 and the like define the macros of
# their extensions and those before; AVX's are accepted and define nothing
printf '#if !defined __SSE__ || !defined __SSE2__ || !defined __MMX__ || defined __SSE3__\n#error\n#endif\n' > $tmp/isa.c
$mucc -fsyntax-only $tmp/isa.c
check 'SSE2 macros'
$mucc -E -dM -msse4.1 -maes -mavx2 -mfma -x c /dev/null > $tmp/isa.out &&
  grep -q __SSE4_1__ $tmp/isa.out && grep -q __SSSE3__ $tmp/isa.out &&
  grep -q __AES__ $tmp/isa.out && ! grep -q __SSE4_2__ $tmp/isa.out &&
  ! grep -q __AVX $tmp/isa.out
check '-msse4.1 -maes -mavx2'

# An unknown extension is an object file for the linker, as with gcc
echo 'int seven(void) { return 7; }' > $tmp/seven-lo.c
echo 'int seven(void); int main() { return seven(); }' > $tmp/main-lo.c
$mucc -c -o $tmp/seven.lo $tmp/seven-lo.c
$mucc -o $tmp/lo $tmp/main-lo.c $tmp/seven.lo
$tmp/lo
[ $? = 7 ]
check '.lo input'
$mucc -static -o $tmp/lo-static $tmp/main-lo.c $tmp/seven.lo
$tmp/lo-static
[ $? = 7 ]
check '.lo input (static)'

# ... but a source file mucc can't compile is a clear error
echo '' > $tmp/foo.cpp
$mucc -c $tmp/foo.cpp 2>&1 | grep -q 'unsupported file type'
check 'unsupported file type'

# -I and -L also take their directory as the next argument, as with gcc
mkdir -p $tmp/incdir
echo '#define FROM_INCDIR 7' > $tmp/incdir/incdir.h
printf '#include "incdir.h"\nint seven(void);\nint main() { return seven() == FROM_INCDIR ? 7 : 1; }\n' > $tmp/incdir.c
echo 'int seven(void) { return 7; }' > $tmp/seven.c
$mucc -c -o $tmp/incdir/seven.o $tmp/seven.c
${mucc%% *} -ar rcs $tmp/incdir/libseven.a $tmp/incdir/seven.o
$mucc -I $tmp/incdir -o $tmp/incdir.out $tmp/incdir.c -L $tmp/incdir -lseven
$tmp/incdir.out
[ $? = 7 ]
check '-I dir and -L dir'

# Built-in assembler: a .s file and a C file link together
printf '  .globl answer\n  .text\nanswer:\n  mov $42, %%eax\n  ret\n' > $tmp/answer.s
printf 'int answer(void);\nint main(void) { return answer(); }\n' > $tmp/main.c
$mucc -o $tmp/answer $tmp/answer.s $tmp/main.c
$tmp/answer
[ $? = 42 ]
check '.s and .c linked together'

# .S: assembly through the preprocessor, as with gcc. `#` lines that
# aren't directives are comments, a quote in one is just a character, and
# $NAME is an immediate whose NAME is a macro.
cat > $tmp/sys.h <<'EOF'
#define ANSWER 40
EOF
cat > $tmp/answer2.S <<'EOF'
#include "sys.h"
#define STR(x) #x
# don't mind this comment
  .text
  .globl answer2
answer2:
#ifdef __ASSEMBLER__
  mov $ANSWER, %eax        # the answer, less 2
#endif
  add $(end - start), %eax
  xor %ecx, %ecx
1:
  inc %ecx                 # 1b and 1f are labels, not numbers
  cmp $3, %ecx
  jne 1b
  ret
  .section .rodata
start: .ascii "ab"
end:
EOF
printf 'int answer2(void);\nint main(void) { return answer2(); }\n' > $tmp/main2.c
$mucc -fno-as-fallback -o $tmp/answer2 $tmp/answer2.S $tmp/main2.c
$tmp/answer2
[ $? = 42 ]
check '.S'
$mucc -E $tmp/answer2.S | grep -q '^# don.t mind this comment$'
check '.S with -E keeps comments'
$mucc -E $tmp/answer2.S | grep -q 'mov \$40, %eax'
check '.S with -E expands $NAME'
cp $tmp/answer2.S $tmp/answer3.asm
$mucc -fno-as-fallback -c -o $tmp/answer3.o -x assembler-with-cpp $tmp/answer3.asm
check '-x assembler-with-cpp'

# ... an instruction it doesn't know in a .s file: a note, then `as`
printf '  .text\n  .globl f\nf:\n  fwait\n  ret\n' > $tmp/unknown.s
$mucc -c -o $tmp/unknown.o $tmp/unknown.s 2>&1 | grep -q 'using the system assembler'
check 'unknown instruction in a .s file falls back to as'

# ... and in an asm() statement: quietly `as`
echo 'int main() { asm("fwait"); return 0; }' > $tmp/unknown.c
$mucc -o $tmp/unknown $tmp/unknown.c 2> $tmp/err && [ ! -s $tmp/err ] && $tmp/unknown
check 'unknown instruction in asm() falls back to as'

# -fno-as-fallback: either one is an error instead, naming the reason
$mucc -fno-as-fallback -c -o $tmp/unknown.o $tmp/unknown.s 2>&1 |
    grep -q "unknown.s: the built-in assembler can't assemble this: unknown instruction 'fwait'"
check '-fno-as-fallback with a .s file'

$mucc -fno-as-fallback -c -o $tmp/unknown.o $tmp/unknown.c 2>&1 |
    grep -q "unknown.c: the built-in assembler can't assemble an asm statement"
check '-fno-as-fallback with asm()'

$mucc -fno-as-fallback -fno-integrated-as -c -o $tmp/seven.o $tmp/empty.c 2>&1 |
    grep -q "can't be used together"
check '-fno-as-fallback with -fno-integrated-as'

# ... and what the built-in assembler knows still works, with no `as`
mkdir -p $tmp/noas
ln -sf $(command -v false) $tmp/noas/as
PATH=$tmp/noas:$PATH $mucc -fno-as-fallback -o $tmp/answer $tmp/answer.s $tmp/main.c
$tmp/answer
[ $? = 42 ]
check '-fno-as-fallback builds without as'

# -fno-integrated-as uses `as`, with the same result
echo 'int main() { return 7; }' > $tmp/seven.c
$mucc -fno-integrated-as -o $tmp/seven $tmp/seven.c
$tmp/seven
[ $? = 7 ]
check -fno-integrated-as

# Reproducible builds: the same source links to the same bytes every time
echo 'static int s = 1; int main() { return s; }' > $tmp/repro.c
$mucc -o $tmp/repro1 $tmp/repro.c
$mucc -o $tmp/repro2 $tmp/repro.c
cmp -s $tmp/repro1 $tmp/repro2
check 'reproducible builds'

# .incbin puts a file in an object cheaply: 10 MB in under 50 MB of memory
# (the most mucc had in RAM at once), byte for byte. (A C program can
# refer to `blob` and `blob_end`.)
head -c 10000000 /dev/urandom > $tmp/big.bin
printf '.section .rodata\n.globl blob\nblob:\n.incbin "%s"\n.globl blob_end\nblob_end:\n' \
  $tmp/big.bin > $tmp/blob.s
kb=$(python3 -c '
import resource, subprocess, sys
subprocess.run(sys.argv[1:], check=True)
print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)' \
  $mucc -c -o $tmp/blob.o $tmp/blob.s 2> $tmp/err) &&
  [ "$kb" -lt 51200 ] && [ ! -s $tmp/err ] &&
  objcopy -O binary --only-section=.rodata $tmp/blob.o $tmp/blob.bin &&
  cmp $tmp/big.bin $tmp/blob.bin
check ".incbin of 10 MB ($kb KB of memory)"

# #embed likewise: the bytes are one token, and go into the data without a
# node each. As a list of numbers, 2 MB took 1.7 GB.
printf 'const unsigned char blob[] = {\n#embed "%s"\n};\n' $tmp/big.bin > $tmp/embed.c
kb=$(python3 -c '
import resource, subprocess, sys
subprocess.run(sys.argv[1:], check=True)
print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)' \
  $mucc -std=c23 -c -o $tmp/embed.o $tmp/embed.c 2> $tmp/err) &&
  [ "$kb" -lt 204800 ] && [ ! -s $tmp/err ] &&
  objcopy -O binary --only-section=.rodata $tmp/embed.o $tmp/blob.bin &&
  cmp $tmp/big.bin $tmp/blob.bin
check "#embed of 10 MB ($kb KB of memory)"

echo 'extern char blob[], blob_end[];
int main() { return blob_end - blob == 10000000 && blob[0] == blob[0] ? 0 : 1; }' > $tmp/blob.c
$mucc -o $tmp/blob $tmp/blob.c $tmp/blob.o && $tmp/blob
check '.incbin linked into a program'

# String literals and const objects are read-only: writing to one faults
# (the shell reports a signal as an exit status above 128).
for target in '"abc"' '(char *)&ci' '(char *)carr'; do
  echo "const int ci = 1; const int carr[2] = {1, 2};
int main() { char *p = $target; *p = 2; return 0; }" > $tmp/ro.c
  $mucc -o $tmp/ro $tmp/ro.c && { $tmp/ro; [ $? -gt 128 ]; } 2> /dev/null
  check "writing to $target faults"
done

# Out of memory is an error, not a crash. (mucc reserves memory 64 MB at a
# time, so it can't start under a 50 MB address space limit.)
(ulimit -v 51200; $mucc -c -o $tmp/oom.o $tmp/empty.c) 2>&1 | grep -q 'error: out of memory'
check 'out of memory'

# gcc's options that build tools use
mkdir -p $tmp/opt/q
echo '#define H 42' > $tmp/opt/h.h
echo '#define H 7' > $tmp/opt/q/h.h
echo '#define IM 5' > $tmp/opt/im.h
printf '#include "h.h"\n#include <errno.h>\nint main(void) { return H; }\n' > $tmp/opt/a.c

# @file: the arguments in a file, quoted as in a shell, which may name more
printf -- "-DRF='3 + 1' @$tmp/opt/resp2\n" > $tmp/opt/resp
printf -- '-o "%s"\n' $tmp/opt/resp.out > $tmp/opt/resp2
echo 'int main(void) { return RF; }' > $tmp/opt/r.c
$mucc @$tmp/opt/resp $tmp/opt/r.c && { $tmp/opt/resp.out; [ $? = 4 ]; }
check '@file'

$mucc -MM $tmp/opt/a.c | tr -d '\\\n' | grep -q '^a.o: *[^ ]*a.c *[^ ]*h.h *$'
check '-MM'

echo '#include "gen.h"' | $mucc -M -MG -xc - | grep -q 'gen.h'
check '-MG'

echo '#define MINE(a, ...) a + __VA_ARGS__' | $mucc -dM -E -xc - > $tmp/opt/dm
grep -qx '#define MINE(a, \.\.\.) a + __VA_ARGS__' $tmp/opt/dm &&
  grep -q '^#define __x86_64__ 1$' $tmp/opt/dm && ! grep -q '__LINE__' $tmp/opt/dm
check '-dM'

echo IM | $mucc -imacros $tmp/opt/im.h -E -P -xc - | grep -qx 5
check '-imacros'

[ "$($mucc -E -P -iquote $tmp/opt/q $tmp/opt/a.c | grep return)" = 'int main(void) { return 42; }' ]
check '-iquote after the file'"'"'s own directory'
printf '#include "h.h"\nH\n' | $mucc -E -P -iquote $tmp/opt/q -I$tmp/opt -xc - | grep -qx 7
check '-iquote before -I'

echo '#include <stdio.h>' | $mucc -nostdinc -E -xc - > /dev/null 2>&1
[ $? = 1 ]
check '-nostdinc'

$mucc -fsyntax-only -o $tmp/opt/none $tmp/opt/a.c && [ ! -e $tmp/opt/none ]
check '-fsyntax-only'
echo 'int f(void) { return x; }' | $mucc -fsyntax-only -xc - 2>&1 | grep -q 'undeclared'
check '-fsyntax-only reports errors'

$mucc -x c-header -o $tmp/opt/h.h.gch $tmp/opt/h.h && [ -f $tmp/opt/h.h.gch ]
check '-x c-header'

$mucc -c -Xassembler --noexecstack -specs=x.specs --param max-inline-insns-single=9 \
  --param=l1-cache-size=32 -mcmodel=small -o $tmp/opt/a.o $tmp/opt/a.c
check 'options for as and tuning, ignored'

$mucc -print-search-dirs | grep -q '^libraries: ='
check '-print-search-dirs'

$mucc $tmp/opt/a.c -l 2>&1 | grep -q "missing argument to '-l'"
check '-l with no library'

# creal, cimag and conj are builtins, as with gcc: no -lm
echo '#include <complex.h>
int main(void) { double complex z = 1 + 2 * I; return creal(z) + cimag(conj(z)) == -1 ? 0 : 1; }' |
  $mucc -o $tmp/opt/cp -xc - && $tmp/opt/cp
check 'creal, cimag and conj without -lm'

# __builtin_trap() traps (SIGILL), unlike __builtin_unreachable()
echo 'int main(void) { __builtin_trap(); return 0; }' > $tmp/trap.c
$mucc -o $tmp/trap $tmp/trap.c && { $tmp/trap; [ $? -eq 132 ]; } 2> /dev/null
check '__builtin_trap'

# -g: code from a macro after #line is on #line's numbering, as with gcc
printf '#define ONE() 1\nint f(void) {\n#line 500 "foo.c"\n  return ONE();\n}\n' > $tmp/lm.c
$mucc -g -S -o $tmp/lm.s $tmp/lm.c && grep -q '\.loc [0-9]* 500$' $tmp/lm.s &&
  ! grep -q '\.loc [0-9]* 4$' $tmp/lm.s
check '-g line of macro code after #line'

# Without -g, no line table and only the source's base name, as with gcc:
# an object is the same bytes built in any directory.
mkdir -p $tmp/d1 $tmp/d2
printf 'int puts(const char *);\nint main(void) { puts("x"); }\n' | tee $tmp/d1/same.c > $tmp/d2/same.c
$mucc -S -o $tmp/same.s $tmp/d1/same.c && ! grep -q -e '\.loc ' -e '\.file 1' -e "$tmp" $tmp/same.s &&
  $mucc -c -o $tmp/same1.o $tmp/d1/same.c && $mucc -c -o $tmp/same2.o $tmp/d2/same.c &&
  cmp -s $tmp/same1.o $tmp/same2.o
check 'objects without -g do not depend on the directory'

echo OK
