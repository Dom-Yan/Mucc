#!/usr/bin/env python3
# gen_random.py - writes a random C program, for test/difftest.sh.
#
#   test/gen_random.py SEED > prog.c
#
# difftest.sh compiles each program with mucc and with gcc and compares
# what they print. For that to mean anything, the programs must have no
# undefined behavior, so this generator never produces it:
#
#   - signed + - * / % and negation go through overflow-checked helpers
#   - unsigned division uses a divisor that can't be zero
#   - shift counts are reduced below the operand's width
#   - array indexes are reduced modulo the array length
#   - pointers only ever point at globals
#   - every variable is initialized
#   - operands inside one expression never have side effects, so the
#     order compilers evaluate them in can't change the result
#
# Implementation-defined behavior, like converting 300 to signed char, is
# allowed: mucc and gcc agree on it for x86-64 Linux. So when the two
# compilers print different things, one of them has a bug.
import random
import sys

R = random.Random(int(sys.argv[1]))

#---------- Types -------------------------------------------------------------

INT = {  # name: (size in bytes, signed)
    'signed char': (1, True), 'unsigned char': (1, False),
    'short': (2, True), 'unsigned short': (2, False),
    'int': (4, True), 'unsigned int': (4, False),
    'long': (8, True), 'unsigned long': (8, False),
}
UNSIGNED = {'int': 'unsigned int', 'long': 'unsigned long'}


def limits(t):
    size, signed = INT[t]
    bits = size * 8
    if signed:
        return -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    return 0, (1 << bits) - 1


def promote(t):
    return 'int' if INT[t][0] < 4 else t


def common(a, b):
    """The usual arithmetic conversions, for LP64."""
    a, b = promote(a), promote(b)
    if a == b:
        return a
    if INT[a][0] != INT[b][0]:
        return a if INT[a][0] > INT[b][0] else b
    return b if INT[a][1] else a  # same size: the unsigned one wins


def int_type():
    return R.choice(list(INT))


#---------- Literals ----------------------------------------------------------

def int_lit(t):
    lo, hi = limits(t)
    r = R.random()
    if r < 0.35:
        v = R.choice([0, 1, 2, lo, lo + 1, hi, hi - 1] + ([-1] if lo < 0 else []))
    elif r < 0.75:
        v = R.randint(max(lo, -100), min(hi, 100))
    else:
        v = R.randint(lo, hi)

    if t == 'int' and v == lo:
        return '(-2147483647-1)'
    if t == 'long' and v == lo:
        return '(-9223372036854775807L-1)'
    suffix = {'int': '', 'unsigned int': 'U', 'long': 'L', 'unsigned long': 'UL'}
    if t not in suffix:
        return f'(({t}){v})'
    return f'({v}{suffix[t]})' if v < 0 else f'{v}{suffix[t]}'


def dbl_lit():
    if R.random() < 0.4:
        return R.choice(['0.0', '1.0', '-1.0', '0.5', '2.5', '-0.125', '1e10', '1e-5', '3.0e15'])
    return repr(R.uniform(-1e4, 1e4))


#---------- Program pieces ----------------------------------------------------

class Var:
    """kind: 'int', 'dbl', 'arr' (ty = element type), 'struct' (ty = Struct),
    'ptr' (ty = pointee int type)."""
    def __init__(self, name, kind, ty=None, n=0, writable=True):
        self.name, self.kind, self.ty, self.n, self.writable = name, kind, ty, n, writable


class Struct:
    """members: list of (name, kind, type, extra), kind in 'int', 'bit'
    (extra = width), 'dbl', 'arr' (extra = length)."""
    def __init__(self, name):
        self.name = name
        self.members = []
        for i in range(R.randint(1, 6)):
            k = R.choice(['int', 'int', 'bit', 'dbl', 'arr'])
            if k == 'bit':
                self.members.append((f'm{i}', 'bit', R.choice(['int', 'unsigned int']), R.randint(1, 31)))
            elif k == 'arr':
                self.members.append((f'm{i}', 'arr', int_type(), R.randint(1, 4)))
            elif k == 'dbl':
                self.members.append((f'm{i}', 'dbl', 'double', 0))
            else:
                self.members.append((f'm{i}', 'int', int_type(), 0))

    def decl(self):
        lines = [f'struct {self.name} {{']
        for name, kind, ty, extra in self.members:
            if kind == 'bit':
                lines.append(f'  {ty} {name} : {extra};')
            elif kind == 'arr':
                lines.append(f'  {ty} {name}[{extra}];')
            else:
                lines.append(f'  {ty} {name};')
        return '\n'.join(lines + ['};'])

    def init(self):
        vals = []
        for _, kind, ty, extra in self.members:
            if kind == 'arr':
                vals.append('{' + ', '.join(int_lit(ty) for _ in range(extra)) + '}')
            elif kind == 'dbl':
                vals.append(dbl_lit())
            elif kind == 'bit':
                vals.append(str(R.randint(0, (1 << (extra - 1)) - 1)))
            else:
                vals.append(int_lit(ty))
        return '{' + ', '.join(vals) + '}'


class Func:
    def __init__(self, name, ret, params):
        self.name, self.ret, self.params = name, ret, params  # params: list of Var


PRELUDE = r'''
int printf(const char *fmt, ...);

#define INT_MIN_ (-2147483647-1)
#define INT_MAX_ 2147483647
#define LONG_MIN_ (-9223372036854775807L-1)
#define LONG_MAX_ 9223372036854775807L

static int safe_add_int(int a, int b) { long r = (long)a + b; return r < INT_MIN_ || r > INT_MAX_ ? a : (int)r; }
static int safe_sub_int(int a, int b) { long r = (long)a - b; return r < INT_MIN_ || r > INT_MAX_ ? a : (int)r; }
static int safe_mul_int(int a, int b) { long r = (long)a * b; return r < INT_MIN_ || r > INT_MAX_ ? a : (int)r; }
static int safe_div_int(int a, int b) { return b == 0 || (a == INT_MIN_ && b == -1) ? a : a / b; }
static int safe_mod_int(int a, int b) { return b == 0 || (a == INT_MIN_ && b == -1) ? a : a % b; }
static int safe_neg_int(int a) { return a == INT_MIN_ ? a : -a; }

static long safe_add_long(long a, long b) { return (b > 0 && a > LONG_MAX_ - b) || (b < 0 && a < LONG_MIN_ - b) ? a : a + b; }
static long safe_sub_long(long a, long b) { return (b < 0 && a > LONG_MAX_ + b) || (b > 0 && a < LONG_MIN_ + b) ? a : a - b; }
static long safe_mul_long(long a, long b) {
  if (a > 2000000000L || a < -2000000000L || b > 2000000000L || b < -2000000000L)
    return a;
  return a * b;
}
static long safe_div_long(long a, long b) { return b == 0 || (a == LONG_MIN_ && b == -1) ? a : a / b; }
static long safe_mod_long(long a, long b) { return b == 0 || (a == LONG_MIN_ && b == -1) ? a : a % b; }
static long safe_neg_long(long a) { return a == LONG_MIN_ ? a : -a; }

// double -> long is undefined when out of range, so clamp.
static long d2l(double d) { return d > -1e15 && d < 1e15 ? (long)d : 0; }

// NaN's sign bit can differ between constant folding and run time.
static void pd(double d) { if (d != d) printf("nan\n"); else printf("%.17g\n", d); }
static void pu(unsigned long x) { printf("%lu\n", x); }
'''


#---------- The generator -----------------------------------------------------

class Gen:
    def __init__(self):
        self.out = []
        self.indent = 0
        self.scope = []        # visible Vars
        self.loop_vars = []    # read-only loop counters
        self.funcs = []
        self.structs = [Struct(f'S{i}') for i in range(R.randint(1, 3))]
        self.counter = 0
        self.in_loop = False

    def emit(self, line):
        self.out.append('  ' * self.indent + line)

    def fresh(self, prefix):
        self.counter += 1
        return f'{prefix}{self.counter}'

    def of_kind(self, kind):
        return [v for v in self.scope if v.kind == kind]

    #---------- Expressions

    def index(self, n, d):
        e, _ = self.int_expr(d - 1)
        return f'(unsigned)({e}) % {n}'

    def int_leaf(self, d):
        cands = ['lit']
        if self.of_kind('int'): cands += ['var'] * 4
        if self.of_kind('arr'): cands += ['arr'] * 2
        if self.of_kind('struct'): cands += ['mem'] * 2
        if self.of_kind('ptr'): cands += ['deref']
        if self.loop_vars: cands += ['loop'] * 2
        if self.of_kind('dbl') and d > 0: cands += ['d2l']
        c = R.choice(cands)

        if c == 'var':
            v = R.choice(self.of_kind('int'))
            return v.name, v.ty
        if c == 'arr':
            v = R.choice(self.of_kind('arr'))
            return f'{v.name}[{self.index(v.n, d)}]', v.ty
        if c == 'mem':
            s = R.choice(self.of_kind('struct'))
            name, kind, ty, extra = R.choice(s.ty.members)
            if kind == 'int':
                return f'{s.name}.{name}', ty
            if kind == 'bit':
                return f'{s.name}.{name}', 'int'  # narrow bit-fields promote to int
            if kind == 'arr':
                return f'{s.name}.{name}[{self.index(extra, d)}]', ty
            return f'd2l({s.name}.{name})', 'long'
        if c == 'deref':
            p = R.choice(self.of_kind('ptr'))
            return f'(*{p.name})', p.ty
        if c == 'loop':
            return R.choice(self.loop_vars), 'int'
        if c == 'd2l':
            return f'd2l({self.dbl_expr(d - 1)})', 'long'
        t = int_type()
        return int_lit(t), t

    def int_expr(self, d):
        if d <= 0 or R.random() < 0.25:
            return self.int_leaf(d)
        r = R.random()
        if r < 0.55:
            return self.binary(d)
        if r < 0.70:
            return self.unary(d)
        if r < 0.82:
            t = int_type()
            e, _ = self.int_expr(d - 1)
            return f'(({t})({e}))', t
        if r < 0.92:
            c, _ = self.int_expr(d - 1)
            a, ta = self.int_expr(d - 1)
            b, tb = self.int_expr(d - 1)
            return f'(({c}) ? ({a}) : ({b}))', common(ta, tb)
        op = R.choice(['<', '<=', '>', '>=', '==', '!='])
        return f'(({self.dbl_expr(d - 1)}) {op} ({self.dbl_expr(d - 1)}))', 'int'

    def binary(self, d):
        a, ta = self.int_expr(d - 1)
        b, tb = self.int_expr(d - 1)
        op = R.choice(['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>',
                       '==', '!=', '<', '<=', '>', '>=', '&&', '||'])

        if op in ('==', '!=', '<', '<=', '>', '>=', '&&', '||'):
            return f'(({a}) {op} ({b}))', 'int'

        if op in ('<<', '>>'):
            t = promote(ta)
            count = f'((unsigned)({b}) % {INT[t][0] * 8})'
            if op == '<<' and INT[t][1]:
                # Shifting a negative signed value left is undefined; shift
                # the same bits as unsigned and convert back.
                return f'(({t})(({UNSIGNED[t]})({a}) << {count}))', t
            return f'(({a}) {op} {count})', t

        t = common(ta, tb)
        if op in ('&', '|', '^'):
            return f'(({a}) {op} ({b}))', t

        name = {'+': 'add', '-': 'sub', '*': 'mul', '/': 'div', '%': 'mod'}[op]
        if INT[t][1]:
            return f'safe_{name}_{t}({a}, {b})', t
        if op in ('/', '%'):
            return f'(({b}) == 0 ? ({t})({a}) : ({a}) {op} ({b}))', t
        return f'(({a}) {op} ({b}))', t

    def unary(self, d):
        a, ta = self.int_expr(d - 1)
        t = promote(ta)
        op = R.choice(['-', '~', '!'])
        if op == '!':
            return f'(!({a}))', 'int'
        if op == '-' and INT[t][1]:
            return f'safe_neg_{t}({a})', t
        return f'({op}({a}))', t

    def dbl_expr(self, d):
        if d <= 0 or R.random() < 0.3:
            cands = ['lit', 'int']
            if self.of_kind('dbl'): cands += ['var'] * 3
            dbl_members = [(s, m) for s in self.of_kind('struct') for m in s.ty.members if m[1] == 'dbl']
            if dbl_members: cands += ['mem']
            c = R.choice(cands)
            if c == 'var':
                return R.choice(self.of_kind('dbl')).name
            if c == 'mem':
                s, m = R.choice(dbl_members)
                return f'{s.name}.{m[0]}'
            if c == 'int':
                return f'(double)({self.int_expr(d - 1)[0]})'
            return dbl_lit()

        a, b = self.dbl_expr(d - 1), self.dbl_expr(d - 1)
        r = R.random()
        if r < 0.75:
            op = R.choice(['+', '-', '*', '/'])
            if op == '/':
                return f'(({b}) != 0.0 ? ({a}) / ({b}) : ({a}))'
            return f'(({a}) {op} ({b}))'
        if r < 0.85:
            return f'(-({a}))'
        c, _ = self.int_expr(d - 1)
        return f'(({c}) ? ({a}) : ({b}))'

    #---------- Lvalues

    def lvalue(self, simple=False):
        """Returns (code, kind, type). kind is 'int', 'bit', 'dbl' or 'struct'.
        With simple=True, only names and struct members: no index or
        pointer that a function call on the right could change."""
        cands = []
        for v in self.scope:
            if not v.writable:
                continue
            if v.kind in ('int', 'dbl'):
                cands.append((v.name, v.kind, v.ty))
            elif v.kind == 'struct':
                cands.append((v.name, 'struct', v.ty))
                for name, kind, ty, extra in v.ty.members:
                    if kind == 'arr':
                        if not simple:
                            cands.append((f'{v.name}.{name}[{self.index(extra, 2)}]', 'int', ty))
                    else:
                        cands.append((f'{v.name}.{name}', kind, ty))
            elif v.kind == 'arr' and not simple:
                cands.append((f'{v.name}[{self.index(v.n, 2)}]', 'int', v.ty))
            elif v.kind == 'ptr' and not simple:
                cands.append((f'(*{v.name})', 'int', v.ty))
        return R.choice(cands) if cands else None

    #---------- Statements

    def assign_stmt(self):
        lv = self.lvalue()
        if not lv:
            return
        code, kind, ty = lv

        if kind == 'struct':
            others = [v for v in self.of_kind('struct') if v.ty is ty and v.name != code]
            if others:
                self.emit(f'{code} = {R.choice(others).name};')
            return
        if kind == 'dbl':
            self.emit(f'{code} = {self.dbl_expr(3)};')
            return

        r = R.random()
        e, _ = self.int_expr(R.randint(1, 4))
        if r < 0.1:
            self.emit(f'{code} {R.choice(["|=", "&=", "^="])} {e};')
        elif r < 0.25 and kind == 'int' and ty in ('unsigned int', 'unsigned long'):
            op = R.choice(['+=', '-=', '*=', '/=', '%=', '<<=', '>>=', '++', '--'])
            if op in ('++', '--'):
                self.emit(f'{code}{op};' if R.random() < 0.5 else f'{op}{code};')
            elif op in ('/=', '%='):
                self.emit(f'{code} {op} (({ty})({e}) | 1);')
            elif op in ('<<=', '>>='):
                self.emit(f'{code} {op} (unsigned)({e}) % {INT[ty][0] * 8};')
            else:
                self.emit(f'{code} {op} ({ty})({e});')
        else:
            self.emit(f'{code} = {e};')

    def print_stmt(self):
        if R.random() < 0.25:
            self.emit(f'pd({self.dbl_expr(3)});')
        else:
            self.emit(f'pu((unsigned long)({self.int_expr(4)[0]}));')

    def call_args(self, f):
        args = []
        for p in f.params:
            if p.kind == 'int':
                args.append(self.int_expr(3)[0])
            elif p.kind == 'dbl':
                args.append(self.dbl_expr(2))
            elif p.kind == 'ptr':
                targets = [v.name for v in self.globals if v.kind == 'int' and v.ty == p.ty]
                args.append(f'&{R.choice(targets)}')
            else:  # struct
                same = [v.name for v in self.of_kind('struct') if v.ty is p.ty]
                args.append(R.choice(same))
        return ', '.join(args)

    def call_stmt(self):
        callable_ = [f for f in self.funcs if all(
            p.kind != 'struct' or any(v.ty is p.ty for v in self.of_kind('struct')) for p in f.params)]
        if not callable_:
            return
        f = R.choice(callable_)
        call = f'{f.name}({self.call_args(f)})'

        if f.ret == 'void':
            self.emit(f'{call};')
        elif isinstance(f.ret, Struct):
            same = [v for v in self.of_kind('struct') if v.ty is f.ret and v.writable]
            if same:
                self.emit(f'{R.choice(same).name} = {call};')
            else:
                self.emit(f'{call};')
        elif f.ret == 'double':
            self.emit(f'pd({call});')
        else:
            lv = self.lvalue(simple=True)
            if lv and lv[1] in ('int', 'bit') and R.random() < 0.6:
                self.emit(f'{lv[0]} = {call};')
            else:
                self.emit(f'pu((unsigned long)({call}));')

    def block(self, d, nstmts):
        saved = len(self.scope)
        self.local_decls(R.randint(0, 3))
        for _ in range(nstmts):
            self.stmt(d)
        del self.scope[saved:]

    def local_decls(self, n):
        for _ in range(n):
            r = R.random()
            if r < 0.6:
                t = int_type()
                v = Var(self.fresh('l'), 'int', t)
                self.emit(f'{t} {v.name} = {self.int_expr(3)[0]};')
            elif r < 0.75:
                v = Var(self.fresh('l'), 'dbl', 'double')
                self.emit(f'double {v.name} = {self.dbl_expr(2)};')
            elif r < 0.9:
                t, n = int_type(), R.randint(1, 5)
                v = Var(self.fresh('l'), 'arr', t, n)
                vals = ', '.join(self.int_expr(2)[0] for _ in range(n))
                self.emit(f'{t} {v.name}[{n}] = {{{vals}}};')
            else:
                s = R.choice(self.structs)
                v = Var(self.fresh('l'), 'struct', s)
                self.emit(f'struct {s.name} {v.name} = {s.init()};')
            self.scope.append(v)

    def stmt(self, d):
        r = R.random()
        if d <= 0 or r < 0.45:
            self.assign_stmt()
        elif r < 0.55:
            self.print_stmt()
        elif r < 0.63 and not self.in_loop:
            self.call_stmt()
        elif r < 0.73:
            c, _ = self.int_expr(3)
            self.emit(f'if ({c}) {{')
            self.indent += 1
            self.block(d - 1, R.randint(1, 3))
            self.indent -= 1
            if R.random() < 0.5:
                self.emit('} else {')
                self.indent += 1
                self.block(d - 1, R.randint(1, 3))
                self.indent -= 1
            self.emit('}')
        elif r < 0.83 and len(self.loop_vars) < 2:
            self.loop(d)
        elif r < 0.90:
            e, _ = self.int_expr(3)
            self.emit(f'switch ((unsigned)({e}) % 4) {{')
            for label in ['case 0:', 'case 1:', 'case 2:', 'default:']:
                # Braces, since before C23 a declaration can't follow a label.
                self.emit(label + ' {')
                self.indent += 1
                self.block(d - 1, R.randint(0, 2))
                if R.random() < 0.7:
                    self.emit('break;')
                self.indent -= 1
                self.emit('}')
            self.emit('}')
        elif r < 0.95 and self.in_loop:
            c, _ = self.int_expr(2)
            self.emit(f'if ({c}) {R.choice(["break", "continue"])};')
        else:
            self.retarget_ptr()

    def loop(self, d):
        i = self.fresh('i')
        n = R.randint(1, 6)
        saved = self.in_loop
        self.in_loop = True
        kind = R.choice(['for', 'while', 'do'])
        if kind == 'for':
            self.emit(f'for (int {i} = 0; {i} < {n}; {i}++) {{')
        elif kind == 'while':
            self.emit(f'for (int {i} = {n}; {i} > 0;) {{')
        else:
            self.emit(f'{{ int {i} = 0; do {{')
        self.indent += 1
        if kind == 'while':
            self.emit(f'{i}--;')
        self.loop_vars.append(i)
        self.block(d - 1, R.randint(1, 4))
        self.loop_vars.pop()
        self.indent -= 1
        if kind == 'do':
            self.emit(f'}} while (++{i} < {n}); }}')
        else:
            self.emit('}')
        self.in_loop = saved

    def retarget_ptr(self):
        ptrs = [v for v in self.globals if v.kind == 'ptr']
        if not ptrs:
            return
        p = R.choice(ptrs)
        targets = [f'&{v.name}' for v in self.globals if v.kind == 'int' and v.ty == p.ty]
        targets += [f'&{v.name}[{self.index(v.n, 2)}]' for v in self.globals if v.kind == 'arr' and v.ty == p.ty]
        self.emit(f'{p.name} = {R.choice(targets)};')

    #---------- Top level

    def make_globals(self):
        self.globals = []
        for _ in range(R.randint(3, 8)):
            t = int_type()
            self.globals.append(Var(self.fresh('g'), 'int', t))
        for _ in range(R.randint(0, 2)):
            self.globals.append(Var(self.fresh('g'), 'dbl', 'double'))
        for _ in range(R.randint(0, 3)):
            self.globals.append(Var(self.fresh('g'), 'arr', int_type(), R.randint(1, 6)))
        for s in self.structs:
            for _ in range(R.randint(0, 2)):
                self.globals.append(Var(self.fresh('g'), 'struct', s))
        for _ in range(R.randint(0, 3)):
            target = R.choice([v for v in self.globals if v.kind == 'int'])
            p = Var(self.fresh('p'), 'ptr', target.ty)
            p.init = target.name
            self.globals.append(p)

        for v in self.globals:
            if v.kind == 'int':
                self.emit(f'{v.ty} {v.name} = {int_lit(v.ty)};')
            elif v.kind == 'dbl':
                self.emit(f'double {v.name} = {dbl_lit()};')
            elif v.kind == 'arr':
                self.emit(f'{v.ty} {v.name}[{v.n}] = {{{", ".join(int_lit(v.ty) for _ in range(v.n))}}};')
            elif v.kind == 'struct':
                self.emit(f'struct {v.ty.name} {v.name} = {v.ty.init()};')
            else:
                self.emit(f'{v.ty} *{v.name} = &{v.init};')
        self.scope = list(self.globals)

    def function(self):
        name = self.fresh('f')
        params = []
        for _ in range(R.randint(0, 8)):
            r = R.random()
            if r < 0.7:
                params.append(Var(self.fresh('a'), 'int', int_type()))
            elif r < 0.85:
                params.append(Var(self.fresh('a'), 'dbl', 'double'))
            elif r < 0.93:
                params.append(Var(self.fresh('a'), 'struct', R.choice(self.structs)))
            else:
                ints = [v for v in self.globals if v.kind == 'int']
                params.append(Var(self.fresh('a'), 'ptr', R.choice(ints).ty))
        r = R.random()
        ret = int_type() if r < 0.6 else 'double' if r < 0.75 else 'void' if r < 0.9 else R.choice(self.structs)

        def spell(t):
            return f'struct {t.name}' if isinstance(t, Struct) else t

        def param_decl(p):
            if p.kind == 'ptr':
                return f'{p.ty} *{p.name}'
            return f'{spell(p.ty)} {p.name}'

        self.emit(f'static {spell(ret)} {name}({", ".join(map(param_decl, params)) or "void"}) {{')
        self.indent += 1
        saved = len(self.scope)
        self.scope += params
        self.block(3, R.randint(3, 8))
        if isinstance(ret, Struct):
            same = [v.name for v in self.of_kind('struct') if v.ty is ret]
            self.emit(f'return {R.choice(same)};' if same else f'return (struct {ret.name}){ret.init()};')
        elif ret == 'double':
            self.emit(f'return {self.dbl_expr(3)};')
        elif ret != 'void':
            self.emit(f'return {self.int_expr(4)[0]};')
        del self.scope[saved:]
        self.indent -= 1
        self.emit('}')
        self.emit('')
        self.funcs.append(Func(name, ret, params))

    def dump_globals(self):
        for v in self.globals:
            if v.kind == 'int':
                self.emit(f'pu((unsigned long){v.name});')
            elif v.kind == 'dbl':
                self.emit(f'pd({v.name});')
            elif v.kind == 'arr':
                for i in range(v.n):
                    self.emit(f'pu((unsigned long){v.name}[{i}]);')
            elif v.kind == 'struct':
                for name, kind, ty, extra in v.ty.members:
                    if kind == 'arr':
                        for i in range(extra):
                            self.emit(f'pu((unsigned long){v.name}.{name}[{i}]);')
                    elif kind == 'dbl':
                        self.emit(f'pd({v.name}.{name});')
                    else:
                        self.emit(f'pu((unsigned long){v.name}.{name});')
            else:
                self.emit(f'pu((unsigned long)*{v.name});')

    def program(self):
        self.emit(PRELUDE)
        for s in self.structs:
            self.emit(s.decl())
        self.emit('')
        self.make_globals()
        self.emit('')
        for _ in range(R.randint(2, 7)):
            self.function()

        self.emit('int main(void) {')
        self.indent += 1
        for _ in range(R.randint(4, 12)):
            self.call_stmt()
        self.block(2, R.randint(2, 6))
        self.dump_globals()
        self.emit('return 0;')
        self.indent -= 1
        self.emit('}')
        return '\n'.join(self.out) + '\n'


sys.stdout.write(Gen().program())
