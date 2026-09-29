# Mucc

[![CI](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml/badge.svg)](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml)

Mucc (`mucc`) is a small, self-hosting C compiler (C17, and C23 with
`-std=c23`) for x86-64 Linux, written in C. It has its own preprocessor,
parser, code generator, assembler and static linker, and it comes as one
static binary with its headers and C library (musl) inside it. Copy that
one file onto any x86-64 Linux machine and it compiles, assembles and
links C programs with nothing else installed: no gcc, no binutils, no
glibc, no system headers. It aims to be minimal, easy to read and fast to
compile with. It is not trying to replace gcc or clang.

Website: <https://dom-yan.github.io/Mucc/>

## Get it

Download the binary from the
[latest release](https://github.com/Dom-Yan/Mucc/releases/latest):

```sh
curl -LO https://github.com/Dom-Yan/Mucc/releases/latest/download/mucc-x86_64-linux
curl -LO https://github.com/Dom-Yan/Mucc/releases/latest/download/mucc-x86_64-linux.sha256
sha256sum -c mucc-x86_64-linux.sha256
chmod +x mucc-x86_64-linux
sudo mv mucc-x86_64-linux /usr/local/bin/mucc
```

Without `sudo`, put it in `~/.local/bin` instead (`mkdir -p ~/.local/bin &&
mv mucc-x86_64-linux ~/.local/bin/mucc`) and make sure that directory is on
your `PATH`. That one file is all of mucc: nothing else needs installing,
not even a C library or headers. To update, download it again the same
way; to uninstall, delete it.

Then:

```sh
mucc -o hello hello.c
./hello
```

To build it from source instead, see [Build from source](#build-from-source).

## Project status: 1.0, feature complete

mucc is feature complete. It compiles C11 and the parts of C23 that real code
uses, it builds large real-world programs (CPython, Git, SQLite, Lua) that then pass
their own test suites, and it compiles, assembles and links itself with no
help from gcc. Everything it needs to turn C into a running Linux program is
in this repository and covered by tests. The C features it leaves out
(`_Complex`, `_BitInt`, K&R definitions) are rare in
practice, and adding them would make the code bigger without making mucc
more useful for what it is for.

All that is left is:

- **Speed:** making mucc itself compile faster and use less memory.
- **Performance:** making the code it generates run faster.
- **Bug fixes and patches:** anything that miscompiles or crashes.
- **New features, rarely:** only when the C language itself changes, or when
  a real program truly needs something mucc can't do.

**mucc is not trying to replace GCC or Clang.** Those are huge compilers that
target dozens of machines and optimize heavily. mucc has its own niche: a
simple, extremely lightweight and fast compiler for Linux on x86-64 machines.
The whole thing is about 18,000 lines you can read end to end, it builds in a
few seconds, and it compiles several times faster than gcc. Use it when you
want quick builds, a compiler you can understand completely, or a small
self-contained toolchain. Use GCC or Clang when you need maximum runtime
speed or another platform.

## Where it runs

**The released binary runs on** any Linux on a 64-bit Intel or AMD (x86-64)
processor. It is a static program, so it doesn't matter which C library the
system has, or whether it has one at all:

- Linux distributions with glibc (Ubuntu, Debian, Fedora, Arch, ...) or
  musl (Alpine), new or old (CentOS 7, from 2014)
- Windows 10 and 11, inside WSL 2
- Virtual machines, cloud servers and containers, even an empty one
  (`FROM scratch`)

**It does not run on:**

- macOS or Windows directly (use a Linux VM or WSL 2)
- ARM machines: Apple Silicon Macs, Raspberry Pi, ARM servers and phones
- 32-bit x86

**The programs it builds** are 64-bit Linux executables (ELF, x86-64). They
use only baseline x86-64 instructions, so they run on any 64-bit Intel or
AMD processor, old or new.

- By default they are linked statically against the musl inside mucc, so
  they run on any x86-64 Linux machine too.
- With `--libc=system` they use the system's glibc and libraries, and are
  linked dynamically unless `-static` is given (see
  [Two C libraries](#two-c-libraries)).
- On Windows, the programs run inside WSL 2, not as native `.exe` files.
- They do not run on macOS or ARM machines.

## Statistics

| | |
| --- | --- |
| Compiler source | 18,244 lines of C in 13 files (13,886 without blank and comment lines) |
| Largest file | `src/parser.c`, 5,630 lines |
| Headers it ships | 9 (`stddef.h`, `stdarg.h`, `stdatomic.h`, ...), 255 lines |
| Released binary | about 7 MB, with musl's headers and libraries inside it |
| Test programs | 49, with 1,739 assertions |
| Other checks | 261 command-line, error-message, assembler and linker cases |
| Largest program it builds | CPython 3.10, about 450,000 lines of C, with every module; 402 of its 408 test suites that run pass. gcc fails 5 of the 6 too on the same machine (OpenSSL 3 and network tests); the other, `test_peg_generator`, needs `-fvisibility=hidden` |
| Other real programs | Git (21,115 tests pass, 0 fail, as with gcc), SQLite 3.34.0 (249,453 tests, 0 errors), Lua 5.4.7, zlib 1.3.1, libpng and TinyCC each pass their own tests. The released binary alone, with no gcc on the machine, builds zlib, Lua, Tcl 8.6.14 and SQLite, which pass their tests, on Ubuntu, Fedora, Alpine and CentOS 7 |
| C library tests | musl built by mucc passes the same `libc-test` cases as musl built by gcc |

Speed, measured on WSL 2 (Ubuntu, gcc 15.2), one file at a time:

| Task | Time |
| --- | --- |
| mucc compiling its own 12 source files | 0.20 s |
| gcc `-O0`, same files | 0.91 s (4.5x slower) |
| gcc `-O2`, same files | 3.54 s (17x slower) |
| `make test` | 20.9 s |
| `make test-all` (also self-hosting, musl and the single binary) | 50.9 s |
| `make difftest N=100` | 49.7 s, 0 failures |

Code mucc generates runs about as fast as `gcc -O0` code, 3 to 5 times slower
than `gcc -O2`. Its built-in static linker is about 2.5 times faster than `ld`.

## Two C libraries

`--libc=` picks the C library to compile and link against.

- `--libc=mucc`, the default for the released binary, is musl 1.2.6, built
  by mucc itself and carried inside the binary. Programs get musl's headers
  and are always linked statically by mucc's own linker, with nothing from
  glibc, gcc or binutils. Headers from inside the binary are named
  `<mucc>/...` in messages, and `-M` leaves them out. musl's complex
  numbers and its x86-64 assembly math are left out; its portable C math
  is used instead.
- `--libc=system` is the system's glibc, with gcc's startup files, for
  programs that need libraries built against glibc (OpenSSL, SDL and the
  like) or shared libraries (`.so`, `-shared`). It needs the packages
  under [Requirements](#requirements). A system library like `-lssl`
  without it is an error that says so.

A `mucc` built from source has no C library inside it, and its default is
`--libc=system`. `make libc` builds musl with it (for `--libc=mucc`), and
`make build/mucc` makes the single binary.

## Build from source

### Requirements

The released binary needs nothing. Building mucc from source, and using
`--libc=system`, needs x86-64 Linux with glibc (WSL 2 on Windows counts).
On Ubuntu or Debian, `build-essential` has everything.

| For | Needed |
| --- | --- |
| Compiling | glibc's headers (`libc6-dev`) |
| Linking | glibc's startup files and gcc's runtime files (`crtbegin.o`, `libgcc.a`) from the `gcc` package. gcc itself isn't run. |
| Dynamic linking | `ld` (binutils). `-static` doesn't need it, since mucc links those itself. |
| `asm()` statements it can't assemble | `as` (binutils), used as a fallback |
| Building mucc | a C11 compiler and `make`: gcc the first time, or mucc itself (`make CC=mucc`) |
| Running the tests | Python 3; Docker for `test/distros.sh`; gdb for debugging |

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
make                 # ./mucc
make libc            # musl, built by mucc, for --libc=mucc
make build/mucc      # the single binary, as released
sudo make install
```

`make` produces `./mucc`. `make install` copies it to `/usr/local/bin`, its
headers to `/usr/local/lib/mucc/include` and, if `make libc` built it, musl
to `/usr/local/lib/mucc/musl`. To install without `sudo`, use `make install
PREFIX=$HOME/.local` and make sure `~/.local/bin` is on your `PATH`. `make
uninstall` removes it. `build/mucc` needs no installing: it can be copied
anywhere on its own.

## Use

```sh
mucc -o hello hello.c
./hello
```

It accepts the usual flags: `-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-U`,
`-static`, `-shared`, `-fPIC`, `-l`, `-L`, `-M*`, `-w`,
`-fno-integrated-as`, `-fuse-ld=` and more (see `src/main.c`). `-std=`
picks the C standard: C17 by default, as with gcc 14 and clang, and
`-std=c23` for C23 (`-std=gnu17` and the like work too; `-ansi` is C89).
`-O`, `-g`, `-march=`, `-mtune=` and `-W*` (except `-w`) are accepted and
ignored.

With `--libc=system`, add `-static` for a binary that doesn't depend on the
system's C library at run time. mucc links those itself; `-fuse-ld=bfd`
(any value) makes it use `ld` instead, and so do linker flags it doesn't
know, such as `-Wl,--gc-sections`. When mucc's built-in assembler doesn't
know an instruction (in a `.s` file or an `asm` statement), mucc runs the
system's `as` instead; `-fno-as-fallback` makes that an error.

As with gcc, a file with an extension mucc doesn't know (such as libtool's
`.lo`) is passed to the linker as an object file. `-E` writes line markers
(`# 12 "foo.h" 1`) as gcc does, so its output, compiled again, reports
errors and debug info against the original files; `-P` leaves them out.
`mucc -ar rcs libfoo.a a.o b.o` is an archiver for static libraries
(operations `r`, `q`, `d` and `t`; its archives are the same, byte for
byte, as GNU `ar`'s), and `mucc -ranlib libfoo.a` rewrites an archive's
symbol index.

## How it works, start to finish

1. `main.c` (driver): reads the flags, then for each `.c` file runs itself
   again as `mucc -cc1`.
2. `token.c` → `preprocess.c`: turns the source into tokens and expands
   macros and `#include`s. Headers come from inside the binary, named
   `<mucc>/...`.
3. `parser.c` + `type.c`: build a typed syntax tree and report errors.
4. `cgen.c`: turns the tree into x86-64 assembly text, using a simple stack
   machine.
5. `asm.c`: turns the assembly into an ELF object file, byte-identical to
   what GNU `as` produces. Code mucc generates always goes through it; only
   hand-written assembly it doesn't know (a `.s` file or an `asm`
   statement) falls back to the system's `as`, if there is one.
6. `link.c`: links the objects with musl's `libc.a`, also read from inside
   the binary, into a static executable. `ar.c` makes `.a` libraries. With
   `--libc=system`, dynamic programs are linked by the system's `ld`.

## Features

**Self-hosting and self-sufficient.** mucc compiles, assembles and links
itself, and its own C library. A mucc built by mucc builds a byte-identical
mucc. In an empty container holding only the released binary and mucc's
source, it rebuilds itself, and that mucc rebuilds itself byte for byte the
same.

**Its own toolchain.** The assembler (`src/asm.c`) produces object files
byte-for-byte identical to GNU `as` from the same input; `.incbin` puts a
file's bytes in an object without holding more than one copy. The linker
(`src/link.c`) makes static executables, including thread-local data and
glibc's IFUNC functions. The archiver (`src/ar.c`) makes static libraries.

**C23** with `-std=c23` (`__STDC_VERSION__` is `202311L`). The default is
C17, where these still work as GNU extensions, except that `bool`, `true`,
`false`, `nullptr`, `constexpr`, `alignas`, `alignof`, `static_assert` and
`thread_local` are ordinary names, as older code expects, and `int f()`
takes any arguments instead of meaning `int f(void)`:

- `bool`, `true`, `false`, `nullptr` and `nullptr_t`
- `auto x = expr;` and `constexpr` variables
- `#embed` with `limit`, `prefix`, `suffix` and `if_empty`, and `__has_embed`
- `static_assert`, `alignas`, `alignof`, `thread_local`, `typeof_unqual`
- `enum E : type`, with a fixed underlying type
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
  `deprecated`, `always_inline`, ...) are ignored. These work as with gcc:
  `noreturn`, `unused`, `packed` and `aligned(N)` (with gcc's layouts),
  `used`, `weak`, `alias`, `section` (with `__start_`/`__stop_` symbols),
  `visibility`, `constructor` and `destructor` (with priorities),
  `cleanup` and `gnu_inline`. Attributes that would change the program but
  aren't implemented are errors, never silently ignored.
  `__has_attribute` and `__has_builtin` tell which are there.
- `asm` statements, plain or with operands (GNU extended asm): the
  constraints `r`, `q`, `a`, `b`, `c`, `d`, `S`, `D`, `m`, `i`, `n` and
  matching digits, with `=`, `+` and `&`, named operands, the `%b`, `%h`,
  `%w`, `%k`, `%q`, `%c` and `%n` modifiers, `%=`, and clobbers. `register
  long x asm("r10")` puts `x` in that register for an asm statement, as
  musl's system calls do. Not supported: `asm goto`, alternative
  constraints (`"r,m"`), SSE and x87 register operands, and asm labels on
  functions and global variables.

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

- C++, or any target but x86-64 Linux
- These GNU attributes: `vector_size`, `mode`, `ifunc`, `naked`, `target`
  and a few more
- `asm goto`, `_Complex`, `__int128`, `_BitInt`,
  `<stdckdint.h>` and `__builtin_add_overflow`, decimal floats,
  K&R-style definitions
- Dynamic linking in mucc's own linker (`--libc=system` uses `ld` for it)
- Optimization beyond register variables and constant folding
- Debug info for variables and types
- `const` enforcement (except for `constexpr`)

## Logistics

### Layout

| File | Role |
| --- | --- |
| `src/main.c` | Driver: command-line flags, runs each stage, embedded files |
| `src/token.c` | Source text to tokens, error messages |
| `src/preprocess.c` | Macros, `#include`, `#if` |
| `src/parser.c` | Recursive-descent parser, builds the typed syntax tree |
| `src/type.c` | Type system and type checking |
| `src/cgen.c` | Syntax tree to x86-64 assembly |
| `src/asm.c` | Assembly to an ELF object file |
| `src/link.c` | Static linker |
| `src/ar.c` | Archiver for static libraries (`mucc -ar`) |
| `src/mucc.h` | Declarations shared by every file |
| `src/hashmap.c`, `src/strings.c`, `src/unicode.c` | Hash table, string helpers, UTF-8 |
| `include/` | Headers mucc ships for the programs it compiles |
| `thirdparty/musl/` | musl 1.2.6, the C library inside the released binary |
| `test/` | Tests; `test/thirdparty/` has build scripts for real projects |

Each source file is split into sections marked `//----------`. List them with
`grep -n -- '^//----------' src/parser.c`.

### Testing

```sh
make test               # language, command-line, error-message, assembler and linker tests
make test-all           # also rebuilds mucc with itself, and tests musl and the single binary
make test-all LIBC=mucc # the same, with everything built against the bundled musl
make difftest           # compiles 300 random programs with mucc and gcc, compares output
```

`make test-all` proves self-hosting in three stages: gcc builds `mucc`, `mucc`
builds `stage2/mucc` which runs the whole test suite, and `stage2/mucc`
compiles `stage3/*.o`, which must match `stage2/*.o` byte for byte.

`make difftest` is the best way to find wrong-code bugs. Its random programs
have no undefined behavior, so if mucc and gcc print different things, one of
them is wrong. Failures are saved in `difftest-failures/`, and
`test/reduce.py difftest-failures/SEED.c` shrinks one to the lines that matter.

Two scripts need Docker: `test/distros.sh` runs the single binary on
Ubuntu, Fedora, Alpine, CentOS 7 and in an empty container, where it also
rebuilds itself; `test/thirdparty/distros.sh` has it build zlib, Lua, Tcl,
SQLite and mucc on those distributions, with no gcc there, and run their
tests (about an hour each).

### Continuous integration and releases

Every push and pull request, and a weekly run, test everything on GitHub
Actions (`.github/workflows/ci.yml`): `make test-all` and `make difftest
N=100`; `make test-all LIBC=mucc` and `test/distros.sh`; and
`test/thirdparty/distros.sh` on the four distributions in parallel. Pushing a version tag (`git tag v1.0.0 && git
push origin v1.0.0`) builds the single binary, checks it with
`test/single.sh` and `test/distros.sh`, and publishes it with its SHA-256
checksum as a GitHub Release (`.github/workflows/release.yml`). The website
in `docs/` is published to GitHub Pages by `.github/workflows/pages.yml`.

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

MIT. See [LICENSE](LICENSE). musl, in `thirdparty/musl/`, is MIT too (see
its `COPYRIGHT`).
