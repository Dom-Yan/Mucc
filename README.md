# Mucc

Mucc (`mucc`) is a small, self-hosting C11 compiler for x86-64
Linux, written in C. It aims to be minimal, easy to read and fast to compile
with. It is not trying to replace gcc or clang.

mucc translates C to x86-64 assembly, then uses the system `as` and `ld` to
produce an ELF executable.

x86-64 Linux is mucc's only target, on purpose: one target keeps the code
small enough to read end to end. It runs anywhere that is x86-64 Linux,
including WSL 2 on Windows. To build native Windows or macOS programs, use a
compiler made for that platform.

## Setup

mucc needs an x86-64 Linux environment, because it emits Linux code and uses
Linux's `as` and `ld`. Windows users get one with WSL 2.

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
at run time, so it runs on almost any x86-64 Linux machine.

mucc accepts the usual flags: `-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-U`, `-static`,
`-shared`, `-l`, `-L`, `-M*` and more (see `src/main.c`).

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
| DRIVER [mucc]  main.c                                                 |
|   Reads the flags, then runs itself again as `mucc -cc1` for each C   |
|   file. It also calls the system tools in stages 5 and 6.            |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 1. TOKENIZE [mucc]  token.c                                           |
|   characters -> tokens                                               |
|   int  main  (  void  )  {  return  42  ;  }                         |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 2. PREPROCESS [mucc]  preprocess.c                      -E stops here |
|   expands #include, #define, #if (nothing to expand in this file)    |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 3. PARSE [mucc]  parser.c, type.c                                     |
|   tokens -> typed syntax tree (AST); type errors are caught here     |
|   function main -> return -> constant 42 (type int)                  |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 4. CODEGEN [mucc]  cgen.c                               -S stops here |
|   AST -> x86-64 assembly text (AT&T syntax)                          |
|   main:  push %rbp ... mov $42, %rax ... ret                         |
+----------------------------------------------------------------------+
    |   hello.s  (temporary assembly file; mucc's own work ends here)
    v
+----------------------------------------------------------------------+
| 5. ASSEMBLE [system]  as                               -c stops here |
|   assembly text -> machine code, stored in an object file (hello.o)  |
|   "mov $42, %rax" becomes the bytes  48 c7 c0 2a 00 00 00            |
+----------------------------------------------------------------------+
    |
    v
+----------------------------------------------------------------------+
| 6. LINK [system]  ld                                                 |
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

Stages 1 to 4 are the compiler proper. Its job ends at assembly text; mucc
never writes machine code itself. Optimizing (see the Roadmap) means changing
stages 3 and 4. Writing your own assembler and linker would replace stages 5
and 6.

## What mucc emits

mucc is a compiler front end plus a code generator. Its output is x86-64
assembly text in AT&T syntax (`./mucc -S file.c` prints it). Without `-S`, it
hands that text to the system `as` and `ld`.

The code generator is a simple stack machine. Every local variable lives in
the stack frame, each expression leaves its result in `%rax`, and
intermediate values go through `push`/`pop`. Operands that are constants
or local variables skip the stack and are loaded straight into a register.
For `int add(int a, int b) { return a + b * 2; }` it emits (trimmed
slightly):

```asm
add:
  push %rbp
  mov %rsp, %rbp
  sub $16, %rsp
  mov %edi, -12(%rbp)        # a
  mov %esi, -16(%rbp)        # b
  movsxd -16(%rbp), %rax     # load b
  mov $2, %rdi
  imul %edi, %eax            # b * 2
  push %rax                  # save b * 2
  movsxd -12(%rbp), %rax     # load a
  pop %rdi
  add %edi, %eax             # a + (b * 2)
  jmp .L.return.add
```

The code is correct but slow, and there is no optimizer: `-O` is accepted
and ignored, as are `-W*`, `-g` and `-std=`. It follows the System V AMD64
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

From C23 (see `test/c23.c`):

- `bool`, `true`, `false` and `nullptr` (with `nullptr_t` in `<stddef.h>`)
- `static_assert` with or without a message, `alignas`, `alignof`,
  `thread_local`, `typeof_unqual`
- `[[attributes]]`, accepted and ignored
- Digit separators: `1'000'000`
- Labels before declarations and at the end of a block
- `#elifdef`, `#elifndef`, `__has_include`
- Empty initializers: `int a[3] = {};`

Inline assembly works only in its plain form, `asm("...")` (or `__asm__`)
with a string and no operands.

Not supported yet: `_Complex`, K&R-style function definitions, `asm` with
operands, the C23 features `auto` type inference, `constexpr`, `#embed` and
`_BitInt`, warnings (it reports the first error and stops), and any target
other than x86-64 Linux with glibc. `__STDC_VERSION__` stays C11 (`201112L`),
since mucc doesn't have all of C23.

SQLite 3.45 (about 285,000 lines) compiles with mucc and runs correctly. Git,
libpng and the other builds scripted in `test/thirdparty/` have not been
re-checked.

## Roadmap


**Fill the gaps**
- Keep going after the first error, and add basic warnings (unused variable,
  missing return).
- K&R function definitions, `_Complex`, `asm` with operands.
- The rest of C23: `auto` type inference, `constexpr`, `#embed`.
- Re-verify the third-party builds (Git, libpng, ...).
- Grow `test/gen_random.py` to cover more of C (unions, long double,
  function pointers, goto).

**Make the output better**
- Add an intermediate representation between the AST and assembly, with
  constant folding and dead-code elimination.
- Replace the push/pop stack machine with register allocation.
- Emit real DWARF debug info.

**Stand on its own**
- Write our own assembler and ELF linker, so `as` and `ld` are no longer needed.

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
| `src/mucc.h`         | Declarations shared by every file           |
| `src/hashmap.c`     | Hash table (keywords, macros, scopes)       |
| `src/strings.c`     | Growable string arrays, `format()`          |
| `src/unicode.c`     | UTF-8 encoding and identifier rules         |
| `include/`          | Headers mucc ships for the programs it compiles (`stddef.h`, ...) |
| `test/`             | Tests (`driver.sh` checks command-line flags; `thirdparty/` builds real projects) |

Each source file starts with a header saying which stage it is and is
split into sections marked like this:

```c
//---------- Macro expansion (#, ## and substitution) ------------------------
```

To jump around, search for `//----------`, or list a file's sections
with `grep -n -- '^//----------' src/parser.c`.

## License

MIT. See [LICENSE](LICENSE).
