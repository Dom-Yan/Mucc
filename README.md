# Mucc

[![CI](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml/badge.svg)](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml)

Mucc (`mucc`) is a small, self-hosting C23 compiler for x86-64 Linux, written
in C. It has its own preprocessor, parser, code generator, assembler and
static linker. It aims to be minimal, easy to read and fast to compile with.
It is not trying to replace gcc or clang.

Website: <https://dom-yan.github.io/Mucc/>

## Project status: feature complete

mucc is feature complete. It compiles C11 and the parts of C23 that real code
uses, it builds large real-world programs (SQLite, Lua, zlib) that then pass
their own test suites, and it compiles, assembles and links itself with no
help from gcc. Everything it needs to turn C into a running Linux program is
in this repository and covered by tests. The C features it leaves out
(`_Complex`, `_BitInt`, GNU `asm` operands, K&R definitions) are rare in
practice, and adding them would make the code bigger without making mucc
more useful for what it is for.

All that is left is:

- **Speed:** making mucc itself compile faster and use less memory.
- **Performance:** making the code it generates run faster.
- **Bug fixes and patches:** anything that miscompiles or crashes.
- **New features, rarely:** only when the C language itself changes, or when
  a real program truly needs something mucc can't do.

**Next direction: self-sufficiency.** The goal is one `mucc` binary that
compiles and links C programs on any x86-64 Linux machine with nothing else
installed: no gcc, no binutils, no glibc. The steps, and how each one is
tested before it is checked off, are in [PLAN.md](PLAN.md).

**mucc is not trying to replace GCC or Clang.** Those are huge compilers that
target dozens of machines and optimize heavily. mucc has its own niche: a
simple, extremely lightweight and fast compiler for Linux on x86-64 machines.
The whole thing is about 15,000 lines you can read end to end, it builds in a
few seconds, and it compiles several times faster than gcc. Use it when you
want quick builds, a compiler you can understand completely, or a small
self-contained toolchain. Use GCC or Clang when you need maximum runtime
speed or another platform.

## Where it runs

**The compiler runs on** Linux on a 64-bit Intel or AMD (x86-64) processor,
with glibc:

- Linux distributions: Ubuntu, Debian, Fedora, Arch, openSUSE and others
  that use glibc (tested on Ubuntu 24.04 and 26.04)
- Windows 10 and 11, inside WSL 2
- Linux virtual machines, containers (Docker) and cloud servers, as long as
  they are x86-64 with glibc

**It does not run on:**

- macOS or Windows directly (use a Linux VM or WSL 2)
- ARM machines: Apple Silicon Macs, Raspberry Pi, ARM servers and phones
- 32-bit x86
- Linux distributions that use musl instead of glibc, such as Alpine

**The programs it builds** are 64-bit Linux executables (ELF, x86-64). They
use only baseline x86-64 instructions, so they run on any 64-bit Intel or
AMD processor, old or new.

- A normal (dynamically linked) program runs on x86-64 Linux machines with
  glibc the same age or newer than the machine that built it.
- A `-static` program carries its C library inside it, so it runs on almost
  any x86-64 Linux machine, including ones without glibc.
- On Windows, the programs run inside WSL 2, not as native `.exe` files.
- They do not run on macOS or ARM machines.

## Statistics

| | |
| --- | --- |
| Compiler source | 14,896 lines of C in 12 files (11,316 without blank and comment lines) |
| Largest file | `src/parser.c`, 4,148 lines |
| Headers it ships | 8 (`stddef.h`, `stdarg.h`, `stdatomic.h`, ...), 276 lines |
| Binary size | 948 KB (built by gcc with `-O2 -g`) |
| Test programs | 43, with 1,529 assertions |
| Other checks | 186 command-line, error-message, assembler and linker cases |
| Largest program it builds | CPython 3.10, about 450,000 lines of C; 387 of its 396 test suites pass (gcc: 391 on the same machine) |
| Other real programs | SQLite 3.34.0 (249,451 tests, 0 errors), Lua 5.4.7, zlib 1.3.1, libpng and TinyCC each pass their own tests |

Speed, measured on WSL 2 (Ubuntu, gcc 15.2), one file at a time:

| Task | Time |
| --- | --- |
| mucc compiling its own 12 source files | 0.21 s |
| gcc `-O0`, same files | 0.77 s (3.6x slower) |
| gcc `-O2`, same files | 2.82 s (13x slower) |
| `make test` | 12.4 s |
| `make test-all` (tests plus the self-hosting check) | 18.5 s |
| `make difftest N=100` | 43.7 s, 0 failures |

Code mucc generates runs about as fast as `gcc -O0` code, 3 to 5 times slower
than `gcc -O2`. Its built-in static linker is about 2.5 times faster than `ld`.

## Setup

### Requirements

mucc runs on x86-64 Linux with glibc (not musl, so not Alpine), and WSL 2 on
Windows counts. On Ubuntu or Debian, `build-essential` has everything.

| For | Needed |
| --- | --- |
| Compiling | glibc's headers (`libc6-dev`) |
| Linking | glibc's startup files and gcc's runtime files (`crtbegin.o`, `libgcc.a`) from the `gcc` package. gcc itself isn't run. |
| Dynamic linking | `ld` (binutils). `-static` doesn't need it, since mucc links those itself. |
| `asm()` statements it can't assemble | `as` (binutils), used as a fallback |
| Building mucc | a C11 compiler and `make`: gcc the first time, or mucc itself (`make CC=mucc`) |
| Running the tests | Python 3; gdb for debugging |

### Linux

```sh
sudo apt update
sudo apt install build-essential git gdb
```

Check it worked:

```sh
gcc --version && make --version && as --version && ld --version
```

This is tested on Ubuntu. Other distributions need the same packages under
their own names (gcc, make, binutils and the C library development files).

### Windows (WSL 2)

1. In PowerShell as Administrator, run `wsl --install -d Ubuntu`. Restart if
   asked, then open "Ubuntu" from the Start menu and create a Linux user.
2. Inside Ubuntu, run the Linux commands above.
3. Keep the project in the Linux filesystem (`~/mucc`), not under `/mnt/c`.
   It builds much faster there, and file permissions and line endings behave.

### Build and install

```sh
git clone https://github.com/Dom-Yan/Mucc.git mucc
cd mucc
make
sudo make install
```

`make` produces `./mucc`. `make install` copies it to `/usr/local/bin` and its
headers to `/usr/local/lib/mucc/include`. To install without `sudo`, use
`make install PREFIX=$HOME/.local` and make sure `~/.local/bin` is on your
`PATH`. `make uninstall` removes it.

### Use

```sh
mucc -o hello hello.c
./hello
```

Add `-static` for a binary that doesn't depend on the system's C library at
run time. mucc links those itself; `-fuse-ld=bfd` (any value) makes it use
`ld` instead, and so do linker flags it doesn't know, such as
`-Wl,--gc-sections`.

It accepts the usual flags: `-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-U`,
`-static`, `-shared`, `-fPIC`, `-l`, `-L`, `-M*`, `-w`,
`-fno-integrated-as`, `-fuse-ld=` and more (see `src/main.c`). `-O`, `-g`,
`-std=`, `-march=`, `-mtune=` and `-W*` (except `-w`) are accepted and
ignored. As with gcc, a file with an extension mucc doesn't know (such as
libtool's `.lo`) is passed to the linker as an object file.

## Features

**Self-hosting.** mucc compiles, assembles and links itself. `make CC=mucc`
builds it with no gcc at all, and with `LDFLAGS=-static` without `ld` either.
A mucc built by mucc builds a byte-identical mucc.

**Its own toolchain.** The assembler (`src/asm.c`) produces object files
byte-for-byte identical to GNU `as` from the same input. The linker
(`src/link.c`) makes static executables, including thread-local data and
glibc's IFUNC functions.

**C23 by default** (`__STDC_VERSION__` is `202311L`):

- `bool`, `true`, `false`, `nullptr` and `nullptr_t`
- `auto x = expr;` and `constexpr` variables
- `#embed` with `limit`, `prefix`, `suffix` and `if_empty`, and `__has_embed`
- `static_assert`, `alignas`, `alignof`, `thread_local`, `typeof_unqual`
- `[[attributes]]`, `unreachable()`, digit separators (`1'000'000`)
- `#elifdef`, `#elifndef`, `#warning`, `__has_include`, empty initializers `{}`

**C11 and the rest:**

- Full preprocessor: macros, `#include`, `#include_next`, `#pragma once`, `-M`/`-MD`
- Integers, `float`, `double`, `long double`, bit-fields, enums, unions
- Structs passed and returned by value, varargs, function pointers
- Variable-length arrays, including parameters like `int m[rows][cols]`
  sized by earlier parameters, `alloca`, compound literals, designated
  initializers
- `_Generic`, `_Alignof`/`_Alignas`, `_Static_assert`
- Thread-local and atomic variables, common symbols
- `L`, `u`, `U`, `u8` string literals
- GNU statement expressions, `typeof`, computed `goto` and case ranges
- GNU attributes, as `__attribute__((...))` or C23 `[[gnu::...]]`, in every
  position gcc accepts. The 66 that are only hints (`format`, `nonnull`,
  `deprecated`, `always_inline`, ...) are ignored; `noreturn` and `unused`
  are honored, and `packed` and `aligned(N)` work on structs and unions.
  Attributes that would change the program but aren't implemented yet are
  errors, never silently ignored.
- Plain `asm("...")` statements

**Code generation.** A simple stack machine with three things that keep it
from being slow: each function's most used integer and pointer locals live in
callee-saved registers, constant expressions are folded at compile time, and
conditions jump on CPU flags directly. It follows the System V AMD64 ABI, so
its objects link with gcc and clang code. It emits line information, so gdb
shows source lines and functions.

**Errors and warnings.** Every error in a file is reported (up to 20), each
with its line and a caret. It warns about unused local variables and non-void
functions that can reach their end without a `return`.

**Not supported:**

- C++, or any target but x86-64 Linux with glibc
- These GNU attributes (so far): `aligned` and `packed` outside structs,
  `used`, `weak`, `alias`, `section`, `visibility`, `constructor`,
  `destructor`, `cleanup`, `vector_size`, `mode` and a few more
- GNU `asm` with operands, `_Complex`, `__int128`, `_BitInt`, `enum E : type`,
  `<stdckdint.h>`, decimal floats, K&R-style definitions
- Optimization beyond register variables and constant folding
- Debug info for variables and types
- `const` enforcement (except for `constexpr`)

**Known issue:** glibc's headers drop `__attribute__` for compilers other
than gcc and clang, so structs that glibc marks `packed` get the wrong
layout under mucc. The one found so far is `struct epoll_event` (16 bytes
instead of 12), which breaks programs that use epoll. The fix is item 1.6 in
[PLAN.md](PLAN.md).

## Logistics

### Layout

| File | Role |
| --- | --- |
| `src/main.c` | Driver: command-line flags, runs each stage |
| `src/token.c` | Source text to tokens, error messages |
| `src/preprocess.c` | Macros, `#include`, `#if` |
| `src/parser.c` | Recursive-descent parser, builds the typed syntax tree |
| `src/type.c` | Type system and type checking |
| `src/cgen.c` | Syntax tree to x86-64 assembly |
| `src/asm.c` | Assembly to an ELF object file |
| `src/link.c` | Static linker |
| `src/mucc.h` | Declarations shared by every file |
| `src/hashmap.c`, `src/strings.c`, `src/unicode.c` | Hash table, string helpers, UTF-8 |
| `include/` | Headers mucc ships for the programs it compiles |
| `test/` | Tests; `test/thirdparty/` has build scripts for real projects |

Each source file is split into sections marked `//----------`. List them with
`grep -n -- '^//----------' src/parser.c`.

### Testing

```sh
make test       # language, command-line, error-message, assembler and linker tests
make test-all   # also rebuilds mucc with itself and checks the result
make difftest   # compiles 300 random programs with mucc and gcc, compares output
```

`make test-all` proves self-hosting in three stages: gcc builds `mucc`, `mucc`
builds `stage2/mucc` which runs the whole test suite, and `stage2/mucc`
compiles `stage3/*.o`, which must match `stage2/*.o` byte for byte.

`make difftest` is the best way to find wrong-code bugs. Its random programs
have no undefined behavior, so if mucc and gcc print different things, one of
them is wrong. Failures are saved in `difftest-failures/`, and
`test/reduce.py difftest-failures/SEED.c` shrinks one to the lines that matter.

### Continuous integration

Every push and pull request runs `make test-all` and `make difftest N=100` on
GitHub Actions (`.github/workflows/ci.yml`). The website in `docs/` is
published to GitHub Pages by `.github/workflows/pages.yml`.

### Contributing

- Run `make test-all` before every commit.
- mucc's own source may only use C that mucc supports, or stage 2 won't build.
- Add a test in `test/` for every feature or fix.
- The goal from here is polish, not new features: each change should make
  mucc faster, smaller, clearer or more correct.
- Using AI tools is fine, as long as you have read and understood every line
  you submit. Pull requests whose author hasn't read them will be closed.
  AI agents should read [AGENTS.md](AGENTS.md).

## License

MIT. See [LICENSE](LICENSE).
