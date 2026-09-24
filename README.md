# Mucc

Mucc (`mucc`) is a small, self-hosting C23 compiler for x86-64
Linux, written in C. It aims to be minimal, easy to read and fast to compile
with. It is not trying to replace gcc or clang.

mucc translates C to x86-64 machine code with its own assembler. It links
static executables (`-static`) with its own linker too, and uses the
system `ld` for dynamically linked ones.

x86-64 Linux is mucc's only target, on purpose: one target keeps the code
small enough to read end to end. It runs anywhere that is x86-64 Linux,
including WSL 2 on Windows. To build native Windows or macOS programs, use a
compiler made for that platform.

## At a glance

**What you need** (on Ubuntu/Debian, `build-essential` has all of it):

| For | Needed |
| --- | ------ |
| Running mucc | x86-64 Linux with **glibc** (not musl, so not Alpine) |
| Compiling | glibc's headers (`libc6-dev`) |
| Linking | glibc's startup files and gcc's runtime files (`crtbegin.o`, `libgcc.a`), from the `gcc` package. gcc itself isn't run. |
| Normal (dynamic) linking | `ld` (binutils). `-static` doesn't need it: mucc links those itself. |
| `asm()` statements it can't assemble | `as` (binutils), used as a fallback |
| Building mucc | a C11 compiler and `make`: gcc the first time, or mucc itself (`make CC=mucc`) |
| The tests | Python 3 (difftest, assembler comparison); gdb for debugging |

**Self-hosting.** mucc compiles, assembles and links itself. `make CC=mucc`
builds it with no gcc at all, and with `LDFLAGS=-static` without `ld` either.
A mucc built by mucc builds a byte-identical mucc (`make test-all` checks).

**Real projects** it builds, which then pass their own tests: SQLite 3.45
(285,000 lines), Lua 5.4.7 (its official test suite), zlib 1.3.1, and mucc.

**What it compiles:** C11 and most of C23 (`auto`, `constexpr`, `#embed`,
`nullptr`, `[[attributes]]`, ...), plus the GNU extensions statement
expressions, `typeof`, computed `goto` and case ranges. The full list is in
"What mucc can do" below.

**What it doesn't:**
- C++, or any target but x86-64 Linux with glibc.
- `__attribute__` on functions and variables (only `packed` and
  `aligned` on structs). Code that uses it only `#ifdef __GNUC__` is fine,
  since mucc doesn't claim to be gcc.
- GNU `asm` with operands (`asm("..." : "=r"(x))`), `_Complex`,
  `__int128`, `_BitInt`, K&R-style definitions.
- Optimization beyond register variables and constant folding: its code
  runs about as fast as `gcc -O0`'s, 3 to 5 times slower than `-O2`'s.
- Debug info for variables: gdb shows source lines and functions only.

## Setup

mucc needs an x86-64 Linux environment, because it emits Linux code and uses
Linux's `ld` (and `as`, only for `asm()` statements it can't assemble
itself). Windows users get one with WSL 2.

### Linux

```sh
sudo apt update
sudo apt install build-essential git gdb
```

| Package           | Why                                                             |
| ----------------- | --------------------------------------------------------------- |
| `build-essential` | `gcc` (builds mucc the first time), `make`, `as`/`ld`/`objdump`/`readelf` (binutils), and the C library headers and startup files mucc links against |
| `git`             | version control                                                 |
| `gdb`             | debugging mucc when it crashes or miscompiles                    |

Check it worked:

```sh
gcc --version && make --version && as --version && ld --version
```

This is tested on Ubuntu. Other distributions need the equivalent packages
(gcc, make, binutils, and the C library development files).

### Windows (WSL 2)

1. In PowerShell as Administrator: `wsl --install -d Ubuntu`. Restart if asked,
   then open "Ubuntu" from the Start menu and create a Linux username and password.
2. Inside Ubuntu, run the Linux commands above.
3. Put the project in the Linux filesystem (`~/mucc`), not under `/mnt/c`. It is
   much faster there, and file permissions and line endings behave.

## Build and install

```sh
git clone https://codeberg.org/HoraDomu/Mucc.git mucc
cd mucc
make
sudo make install
```

`make` produces `./mucc`. `make install` copies it to `/usr/local/bin` and
mucc's own headers to `/usr/local/lib/mucc/include`, so `mucc` works from any
directory. Use `make install PREFIX=$HOME/.local` to install without `sudo`
(then make sure `~/.local/bin` is on your `PATH`), and `make uninstall` to
remove it.

mucc finds its headers relative to its own binary: `include/` next to it when
run from the source tree, or `../lib/mucc/include` once installed.

## Use

```sh
mucc -o hello hello.c
./hello
```

Add `-static` to get a binary that does not depend on the system's C library
at run time, so it runs on almost any x86-64 Linux machine. mucc links
those itself, about 2.5 times faster than `ld`; `-fuse-ld=bfd` (any value)
makes it use `ld` instead, and so do linker flags it doesn't know, such as
`-Wl,--gc-sections`.

mucc accepts the usual flags: `-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-U`, `-static`,
`-shared`, `-l`, `-L`, `-M*`, `-w`, `-fno-integrated-as`, `-fuse-ld=` and
more (see `src/main.c`).

The executables mucc produces are Linux ELF binaries. They run on Linux and
inside WSL.

## Test

```sh
make test       # language-feature tests, command-line tests, error-message tests
make test-all   # also rebuilds mucc with itself and checks the result
make difftest   # compiles 300 random programs with mucc and gcc, compares output
```

`make difftest` (needs Python 3) is the best way to find wrong-code bugs.
`test/gen_random.py` writes random C programs that have no undefined
behavior, so if mucc and gcc print different things, one of them is wrong.
Run more with `make difftest N=2000`. A program that fails is saved in
`difftest-failures/`, and `test/reduce.py difftest-failures/SEED.c` shrinks
it to the lines that matter. `test/difftest.sh --check-ub` checks the
generator itself by running its programs under gcc's sanitizers.

`make test-all` proves self-hosting in three stages:

1. `mucc` (stage 1) is built by `gcc`.
2. `stage2/mucc` is built by `mucc`, then run against the whole test suite.
3. `stage3/*.o` is compiled by `stage2/mucc` and must match `stage2/*.o`
   byte for byte.

When you change mucc:

- Plain `make` only runs stage 1. Run `make test-all` to compile your change
  with mucc itself, and do it before every commit.
- mucc's own source can only use C features mucc supports (no `_Complex`, K&R
  definitions or `asm` with operands), or stage 2 will not build.
- Stage 1 always uses gcc, so a bad change can be fixed in the source and
  rebuilt.
- Add a test in `test/` for every feature or fix.

## How mucc works

`./mucc -o hello hello.c` sends the file through the stages below (the
files are in `src/`). `[mucc]`
marks this project's own code and `[system]` marks tools mucc borrows. The
example input is `int main(void) { return 42; }`. The flags `-E`, `-S` and
`-c` stop the pipeline early and keep that stage's output.

```
 hello.c     int main(void) { return 42; }
    |
    v
+----------------------------------------------------------------------+
| DRIVER [mucc]  main.c                                                |
|   Reads the flags, then runs itself again as `mucc -cc1` for each C  |
|   file, which does stages 1 to 5. Then it links, stage 6.            |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 1. TOKENIZE [mucc]  token.c                                          |
|   characters -> tokens                                               |
|   int  main  (  void  )  {  return  42  ;  }                         |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 2. PREPROCESS [mucc]  preprocess.c                     -E stops here |
|   expands #include, #define, #if (nothing to expand in this file)    |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 3. PARSE [mucc]  parser.c, type.c                                    |
|   tokens -> typed syntax tree (AST); type errors are caught here     |
|   function main -> return -> constant 42 (type int)                  |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 4. CODEGEN [mucc]  cgen.c                              -S stops here |
|   AST -> x86-64 assembly text (AT&T syntax)                          |
|   main:  push %rbp ... mov $42, %rax ... ret                         |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 5. ASSEMBLE [mucc]  asm.c                              -c stops here |
|   assembly text -> machine code, stored in an object file (hello.o)  |
|   "mov $42, %rax" becomes the bytes  48 c7 c0 2a 00 00 00            |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 6. LINK [mucc]  link.c with -static, else [system]  ld               |
|   hello.o + C startup files (crt1.o, ...) + the C library (libc)     |
|   -> one runnable file; the startup code calls main                  |
+----------------------------------------------------------------------+
    |
    v
 hello     Linux ELF executable (a.out if you do not pass -o)
    |
    v
 $ ./hello ; echo $?      ->  42
```

Stages 1 to 4 are the compiler proper; stage 5 turns their assembly text
into machine code in the same process. Optimizing (see the Roadmap) means
changing stages 3 and 4.

The linker (`src/link.c`) makes static executables: it pulls the members
it needs out of `libc.a` and the other archives, lays out the code, data
and thread-local sections, makes the GOT entries and the stubs for
glibc's IFUNC functions (like `memcpy`, picked for the CPU at startup),
applies the relocations and writes the ELF file with a symbol table for
debuggers. `test/link.sh` links every test program this way, and mucc
itself.

## What mucc emits

mucc is a compiler front end plus a code generator. Its output is x86-64
assembly text in AT&T syntax (`./mucc -S file.c` prints it), which its
assembler (`src/asm.c`) turns into an ELF object file. The assembler knows
exactly the instructions and directives the code generator uses, and its
objects are byte-for-byte the same as GNU `as` makes from the same text
(`test/asm.sh` checks this on every test program). For an `asm()`
statement or a `.s` file with anything else, mucc runs the system `as`
instead; `-fno-integrated-as` always does.

The code generator is a simple stack machine: each expression leaves its
result in `%rax`, and intermediate values go through `push`/`pop`. Three
things keep that from being slow:

- **Register variables.** Each function's most used integer and pointer
  locals (uses in loops count more) live in the callee-saved registers
  `%rbx` and `%r12`-`%r15`, unless their address is taken. Others live in
  the stack frame.
- **Simple operands and folding.** Constants, variables and constant
  expressions (`1000 * 1000`) are loaded straight into a register, without
  the stack, and constant expressions are computed at compile time.
- **Branches.** Conditions like `i < n && p` jump on the CPU flags
  directly.

For
`long sum(int *a, int n) { long s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }`
the loop is (comments added):

```asm
.L.begin.1:
  movsxd %ebx, %rax          # i
  movsxd %r14d, %rdi         # n
  cmp %edi, %eax
  jge .L..2                  # leave if i >= n
  movsxd %ebx, %rax
  mov $4, %rdi
  imul %rdi, %rax            # i * sizeof(int)
  mov %r13, %rdi             # a
  add %rdi, %rax
  movsxd (%rax), %rax        # a[i]
  mov %r12, %rdi             # s
  add %rdi, %rax
  mov %rax, %r12             # s += a[i]
  movsxd %ebx, %rax
  mov $1, %rdi
  add %edi, %eax
  mov %eax, %ebx             # i++
  jmp .L.begin.1
```

The result runs about as fast as `gcc -O0`'s code (roughly 3 to 5 times
slower than `-O2`). There is no optimizer beyond this: `-O` is accepted
and ignored, as are `-W*` (except `-w`), `-g` and `-std=`. It follows the System V AMD64
ABI, so its objects link with gcc/clang-built code and glibc. It emits
`.file`/`.loc` line directives, so debuggers see source lines, but no
variable or type debug info.

## What mucc can do

Covered by the tests in `test/`:

- Full preprocessor: macros, `#include`, `#include_next`, `#pragma once`, `-M`/`-MD`
- Integers, `float`, `double`, `long double`, bit-fields, enums, unions
- Structs passed and returned by value, varargs, function pointers
- Variable-length arrays, `alloca`, compound literals, designated initializers
- `_Generic`, `_Alignof`/`_Alignas`, `_Static_assert`, `typeof`, statement expressions
- Thread-local and atomic variables, common symbols
- `L`, `u`, `U`, `u8` string literals
- `__attribute__((packed))` and `__attribute__((aligned(N)))` on structs
- `-c`, `-S`, `-E`, `-static`, `-shared`, `-fPIC`, linking `.a`/`.so` files
- Compiling itself (see Test)

Errors and warnings (see `test/errors.sh`):

- Every error in a file is reported, each with its line and a caret, up to
  20. After an error, mucc skips to the end of that statement or
  declaration and carries on.
- Warnings for unused local variables and for non-void functions that can
  reach their end without a `return`. `-w` turns warnings off. They're
  never given for system headers. Functions marked `_Noreturn` and C
  library ones like `exit()` and `abort()` count as not returning.

C23 is the default: `__STDC_VERSION__` is `202311L`. From C23 (see
`test/c23.c`):

- `bool`, `true`, `false` and `nullptr` (with `nullptr_t` in `<stddef.h>`)
- `auto x = expr;` takes its type from the initializer
- `constexpr` variables. Scalar ones are true constants: usable in array
  sizes, `case` labels, `static_assert` and other `constexpr`s. mucc checks
  that the value fits the type exactly and that nothing modifies them.
  Arrays and structs are accepted as ordinary initialized objects.
- `#embed "file"` with `limit`, `prefix`, `suffix` and `if_empty`, and
  `__has_embed`
- `static_assert` with or without a message, `alignas`, `alignof`,
  `thread_local`, `typeof_unqual`
- `[[attributes]]`, accepted and ignored, except `[[noreturn]]`, which the
  missing-return warning uses; `__has_c_attribute`
- `unreachable()` in `<stddef.h>` (it traps if reached)
- Unnamed parameters in definitions, `f(...)` with one-argument `va_start`
- `u8'a'` character literals and digit separators: `1'000'000`
- Labels before declarations and at the end of a block
- `#elifdef`, `#elifndef`, `#warning`, `__has_include`
- Empty initializers: `int a[3] = {};`

Inline assembly works only in its plain form, `asm("...")` (or `__asm__`)
with a string and no operands.

Not supported yet: `_Complex`, K&R-style function definitions, `asm` with
operands, the C23 features `_BitInt`, `enum E : type`, `<stdckdint.h>` and
decimal floats, and any target other than x86-64 Linux with glibc. mucc
doesn't enforce `const` (except for `constexpr`), and still treats
`int f();` as a function with unspecified parameters, as C17 did, rather
than as `int f(void)`.

SQLite 3.45 (about 285,000 lines), Lua 5.4.7 and zlib 1.3.1 compile with
mucc and pass their own tests. Git, libpng and the other builds scripted
in `test/thirdparty/` have not been re-checked.

## Roadmap

mucc's features are complete for what it's meant to be. From here the work
is polishing, trimming and optimizing what's there, not adding features:
each change should make mucc faster, smaller, clearer or more correct.

**Correctness and polish**
- Clearer errors for things mucc doesn't support, instead of confusing
  ones: `__attribute__` on a function says "variable name omitted" now.
- Clearer messages for a stray `}` or an unknown type inside a struct.
- Re-check the third-party builds in `test/thirdparty/`, and fix what
  they find.
- Grow `test/gen_random.py` to cover more of C (unions, long double,
  function pointers, goto), to find wrong-code bugs.

**Faster code**
- Keep expression temporaries in registers instead of push/pop, and use
  addressing modes like `(%r13,%rbx,4)` for `a[i]`.
- `double` variables in registers.

**Smaller and faster compiles**
- Smaller tokens and AST nodes: compiling SQLite uses about 490 MB.
  This also makes big `#embed`s lighter (about 250 bytes per byte now).
- Don't emit `__func__` strings for functions that never use them.

Deliberately left out: dynamic linking in mucc's own linker (`ld` does it),
`_Complex`, GNU `asm` operands, `_BitInt` and C++. Add one only if a real
program you need can't do without it.

## Layout

All compiler code is in `src/`, listed here in pipeline order:

| File                | Role                                        |
| ------------------- | ------------------------------------------- |
| `src/main.c`        | Driver: command-line flags, runs `as` / `ld` |
| `src/token.c`       | Stage 1: source text to tokens, error messages |
| `src/preprocess.c`  | Stage 2: macros, `#include`, `#if`          |
| `src/parser.c`      | Stage 3: recursive-descent parser, builds typed AST |
| `src/type.c`        | Stage 3: type system and type checking      |
| `src/cgen.c`        | Stage 4: AST to x86-64 assembly             |
| `src/asm.c`         | Stage 5: assembly to an ELF object file     |
| `src/link.c`        | Stage 6: static linker                      |
| `src/mucc.h`         | Declarations shared by every file           |
| `src/hashmap.c`     | Hash table (keywords, macros, scopes)       |
| `src/strings.c`     | Growable string arrays, `format()`          |
| `src/unicode.c`     | UTF-8 encoding and identifier rules         |
| `include/`          | Headers mucc ships for the programs it compiles (`stddef.h`, ...) |
| `test/`             | Tests (`driver.sh` checks command-line flags, `errors.sh` diagnostics, `asm.sh` the assembler against GNU `as`, `link.sh` the static linker; `thirdparty/` builds real projects) |

Each source file starts with a header saying which stage it is and is
split into sections marked like this:

```c
//---------- Macro expansion (#, ## and substitution) ------------------------
```

To jump around, search for `//----------`, or list a file's sections
with `grep -n -- '^//----------' src/parser.c`.

## License

MIT. See [LICENSE](LICENSE).
