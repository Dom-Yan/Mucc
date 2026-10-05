#!/bin/bash
# Checks -g: a program built with it, run in gdb, shows its variables,
# parameters, globals and their types, as with gcc -g. Built with the
# built-in assembler and, if there is one, with GNU as. Needs gdb, and is
# skipped without it.
mucc="$1 -Iinclude"

tmp=`mktemp -d /tmp/mucc-debug-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

if ! command -v gdb > /dev/null; then
    echo "testing debug ... skipped (no gdb)"
    exit 0
fi

cat > $tmp/t.c <<'EOF'
enum Color { RED, GREEN = 5, BLUE };
struct Node { int val; struct Node *next; char name[8]; };
union U { int i; float f; };
struct Bits { unsigned a : 3, b : 5; int c : 4; };
int counter = 42;
static double ratio = 2.5;
const char *greeting = "hello";
int table[3] = {10, 20, 30};
struct Node head = {1, 0, "head"};

static int add(int x, int y) {
  int sum = x + y;
  long big = (long)sum * 1000;
  return (int)(big / 1000); // line 14
}

int walk(struct Node *n, enum Color c) {
  int count = 0;
  union U u;
  u.f = 1.5f;
  struct Bits bits = {5, 17, -3};
  double arr[4] = {0.5, 1.5, 2.5, 3.5};
  int (*fp)(int, int) = add;
  _Alignas(32) int aligned = 7;
  for (; n; n = n->next)
    count += n->val;
  return count + c + fp(1, 2) + bits.a + (int)arr[1] + u.i * 0 + aligned; // line 27
}

int main(void) {
  struct Node second = {2, 0, "second"};
  head.next = &second;
  return walk(&head, BLUE) == 0;
}
EOF

# gdb's answers, one per command, then what they must be.
cmds=(
  'break 27' 'break 14' run
  'print n' 'print c' 'print count' 'print u' 'print bits' 'print arr'
  'print fp' 'print aligned' 'ptype struct Node' 'whatis fp'
  'print counter' 'print ratio' 'print greeting' 'print table'
  'print head.name' 'print head.next->name'
  continue
  'print x' 'print y' 'print sum' 'print big' 'ptype big' 'bt'
)
want=(
  '$1 = (struct Node *) 0x0' '$2 = BLUE' '$3 = 3'
  '$4 = {i = 1069547520, f = 1.5}' '$5 = {a = 5, b = 17, c = -3}'
  '$6 = {0.5, 1.5, 2.5, 3.5}' '$7 = (int (*)(int, int)) ADDR <add>'
  '$8 = 7'
  'type = struct Node {' '    int val;' '    struct Node *next;'
  '    char name[8];' '}' 'type = int (*)(int, int)'
  '$9 = 42' '$10 = 2.5' '$11 = ADDR "hello"' '$12 = {10, 20, 30}'
  '$13 = "head\000\000\000"' '$14 = "second\000"'
  '$15 = 1' '$16 = 2' '$17 = 3' '$18 = 3000' 'type = long'
  '#0  add (x=1, y=2) at t.c:14'
  '#1  ADDR in walk (n=0x0, c=BLUE) at t.c:27'
  '#2  ADDR in main () at t.c:33'
)

check() { # check NAME OPTIONS...
    name=$1
    shift
    $mucc -g "$@" -o $tmp/t $tmp/t.c || {
        echo "testing debug $name ... failed (did not compile)"; exit 1; }
    args=()
    for c in "${cmds[@]}"; do
        args+=(-ex "$c")
    done
    gdb -batch -nx "${args[@]}" $tmp/t 2>&1 |
        grep -E '^(\$|type =|    |\}|#[0-9])' |
        sed -E "s/0x[0-9a-f]{4,}/ADDR/g; s|$tmp/||g" > $tmp/got
    printf '%s\n' "${want[@]}" > $tmp/want
    if diff $tmp/want $tmp/got > $tmp/diff; then
        echo "testing debug $name ... passed"
    else
        echo "testing debug $name ... failed"
        cat $tmp/diff
        exit 1
    fi
}

check 'variables and types in gdb'

# GNU as's debug info, linked by ld. (Some distributions' as compresses
# it, which mucc's own linker, for static programs, leaves out.)
if command -v as > /dev/null && command -v ld > /dev/null; then
    check 'with GNU as and ld' -fno-integrated-as --libc=system
fi

# A compressed debug section from another compiler's object is left out by
# mucc's linker, not copied garbled: gdb reads the program without errors.
for i in $(seq 200); do
    printf '  .section .debug_str,"MS",@progbits,1\n  .string "string number %d"\n' $i
done > $tmp/c.s
if as --compress-debug-sections=zlib -c -o $tmp/c.o $tmp/c.s 2> /dev/null &&
   readelf -S -W $tmp/c.o | grep ' .debug_str ' | grep -q 'MSC'; then
    $mucc -g -c -o $tmp/t.o $tmp/t.c &&
        $mucc -static -o $tmp/t2 $tmp/t.o $tmp/c.o &&
        ! gdb -batch -nx -ex 'break add' $tmp/t2 2>&1 | grep -q 'DWARF Error' &&
        echo "testing debug a compressed section is left out ... passed" ||
        { echo "testing debug a compressed section is left out ... failed"; exit 1; }
fi

# The first instruction of each function has a line (gdb's `break main`
# finds one even when main is last), and code from a macro is on the line
# the macro is used on, not in the header defining it.
cat > $tmp/m.c <<'EOF'
#include <stddef.h>
#define TWICE(x) ((x) * 2)
int f(int v) {
  int *p = NULL;
  int r = TWICE(v);
  return p ? 0 : r;
}
int main(void) {
  return f(3) != 6;
}
EOF
$mucc -g -o $tmp/m $tmp/m.c &&
    gdb -batch -nx -ex 'break main' -ex 'break f' -ex run -ex continue -ex next $tmp/m 2>&1 |
    grep -E '^(Breakpoint|[0-9]+\s)' | sed "s|$tmp/||; s/ at 0x[0-9a-f]*//" > $tmp/got
printf '%s\n' 'Breakpoint 1: file m.c, line 9.' 'Breakpoint 2: file m.c, line 4.' \
    'Breakpoint 1, main () at m.c:9' '9	  return f(3) != 6;' \
    'Breakpoint 2, f (v=3) at m.c:4' '4	  int *p = NULL;' '5	  int r = TWICE(v);' > $tmp/want
if diff $tmp/want $tmp/got > $tmp/diff; then
    echo "testing debug lines of functions and macros ... passed"
else
    echo "testing debug lines of functions and macros ... failed"
    cat $tmp/diff
    exit 1
fi
