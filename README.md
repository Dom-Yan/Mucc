# Mucc

[![CI](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml/badge.svg)](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Dom-Yan/Mucc)](https://github.com/Dom-Yan/Mucc/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Mucc is a small, fast, self-hosting C compiler for x86-64 and ARM64 Linux.
It is a single static file with its own preprocessor, assembler, linker,
archiver, headers and C library (musl) built in. Copy it onto any 64-bit
Linux machine and it builds C programs with nothing else installed: no
gcc, no binutils, no system headers.

[Website](https://dom-yan.github.io/Mucc/) ·
[Download](https://github.com/Dom-Yan/Mucc/releases/latest) ·
[Changelog](https://dom-yan.github.io/Mucc/changelog.html) ·
[Report a bug](https://github.com/Dom-Yan/Mucc/issues)

## At a glance

| | |
| --- | --- |
| **Compile speed** | 2.7x faster than `gcc -O0` and 1.6x faster than `clang -O0` ([details](#performance)) |
| **Generated code** | Runs about 15% slower than `gcc -O0` code and about half as fast as `gcc -O2` code |
| **Size** | One 8.8 MB static binary, with musl and Linux's headers inside ([what's in it](#whats-inside-the-binary)) |
| **Source** | About 33,000 lines of C in 15 files, small enough to read end to end |
| **Language** | C17 by default, C23 with `-std=c23`, plus the GNU extensions real code uses |
| **Targets** | x86-64 and ARM64 (AArch64) Linux, natively or cross-compiling either way |
| **Real programs** | SQLite, Lua, Redis, Tcl, jq, zlib, Git, libpng, QuickJS and TinyCC build with it and pass their own test suites; CPython passes 402 of its 408 |
| **Self-hosting** | Each release is built by the one before it, with no gcc or binutils, and rebuilds itself byte for byte |

## Contents

- [Features](#features)
- [Limitations](#limitations)
- [Install](#install)
- [Quick start](#quick-start)
- [Make mucc your default compiler](#make-mucc-your-default-compiler)
- [Using mucc](#using-mucc)
- [Performance](#performance)
- [Build from source](#build-from-source)
- [How it works](#how-it-works)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)

## Features

- **Nothing else to install.** The compiler, assembler, linker, archiver,
  headers and C library are one file. It works in an empty container, on
  old distributions like CentOS 7, and on machines with no compiler at all.
- **Programs that run anywhere.** By default it links statically against
  its own musl, so what it builds runs on any Linux for that CPU. With
  `--libc=system` it uses the system's glibc and shared libraries instead.
- **Drop-in for gcc.** It takes gcc's flags (`-c`, `-o`, `-I`, `-D`, `-l`,
  `-std=`, `-M*`, `-Wall`, ...), so Makefiles and `configure` scripts work
  unchanged.
- **Real C.** VLAs, `_Generic`, `_Atomic`, threads, complex numbers, bit
  fields, `#embed`, `constexpr`, `typeof`, `__int128`, statement
  expressions, inline and extended `asm`, gcc's builtins and attributes,
  and SSE intrinsics up to SSE4.2.
- **Debuggable.** `-g` writes DWARF that gdb understands, and diagnostics
  point at the line and column like gcc's. `-Wall`, `-Werror`,
  `-Wno-<name>` and `#pragma GCC diagnostic` work.
- **Cross-compiles.** `--target=aarch64-linux` or `--target=x86_64-linux`
  builds for the other CPU.

## Limitations

Use gcc or clang instead if you need any of these.

- **Platforms:** only x86-64 and ARM64 Linux (on Windows, WSL 2). No
  macOS, native Windows, or 32-bit targets.
- **Optimization:** none beyond register allocation and constant folding.
  Generated code is in the range of `gcc -O0`, not `-O2`. No LTO, PGO or
  `-march` tuning.
- **Language:** C only, no C++. No `_BitInt`, nested functions,
  `__label__`, VLAs inside structs or integer `_Complex`. No AVX or NEON
  intrinsics (code that checks `__AVX__` or `__ARM_NEON` takes its portable
  path).
- **Bundled musl:** static programs only, so no shared libraries or
  `dlopen` (use `--libc=system`). Its `malloc` and stdio are slower than
  glibc's, and its locale support is basic.
- **Tooling:** no sanitizers, coverage or profiling. Shared libraries
  (`-shared`) and Intel-syntax assembly use the system's `ld` and `as`.
- **Diagnostics:** it rejects the common mistakes but is not a strict
  checker; some invalid programs gcc rejects are accepted. See the
  [open issues](https://github.com/Dom-Yan/Mucc/issues).

<details>
<summary>The full list</summary>

- Cross-compiling from x86-64 to ARM64, `long double` constants are only
  as precise as x86-64's own `long double` (64 bits of mantissa, not
  ARM64's 113). At run time, and in a mucc that runs on ARM64, they are
  exact.
- Implicit function declarations are errors, as C99 and later require.
- gcc attributes that change what a program does but that mucc doesn't
  implement are errors rather than silently ignored: `ifunc`, `naked`,
  `target`, `target_clones`, `symver`, `weakref`, `retain`, `copy`,
  `noinit`, `persistent`, `scalar_storage_order` and `hardbool`. Unknown
  attributes only warn.
- Vectors must be 4, 8 or 16 bytes.
- No unwind tables (`.eh_frame`): fine for C, but `.cfi_*` directives in
  assembly are dropped.
- Objects from gcc that call libgcc's helpers (complex multiplication,
  128-bit division) need gcc's `libgcc.a` to link. (On ARM64, the bundled
  musl comes with those.)
- With the bundled musl, threads get 128 KB stacks unless a program asks
  for more, and `iconv` differs from glibc's in small ways.
- `-O0` to `-O3` and other flags that only tune optimization, diagnostics
  or hardening are accepted and ignored. An unknown flag is an error.

</details>

## Install

On any x86-64 or ARM64 Linux (Ubuntu, Debian, Fedora, Arch, Alpine,
Raspberry Pi OS, WSL 2, ...):

```sh
mkdir -p ~/.local/bin
curl -Lo ~/.local/bin/mucc https://github.com/Dom-Yan/Mucc/releases/latest/download/mucc-$(uname -m | sed s/arm64/aarch64/)-linux
chmod +x ~/.local/bin/mucc
mucc --version
```

That downloads `mucc-x86_64-linux` or `mucc-aarch64-linux` to match your
CPU. Each release also has a `.sha256` file to check it with
`sha256sum -c`.

- **`mucc: command not found`?** Open a new terminal, or add
  `export PATH="$HOME/.local/bin:$PATH"` to your `~/.bashrc`.
- **Update:** run the same `curl` again.
- **Uninstall:** `rm ~/.local/bin/mucc`.

## Quick start

```c
// hello.c
#include <stdio.h>

int main(void) {
  printf("hello, world\n");
  return 0;
}
```

```sh
mucc -o hello hello.c
./hello
```

`hello` is a static executable: copy it to any Linux machine with the same
kind of CPU and it runs there.

## Make mucc your default compiler

Pick the scope you want. Each one is undone by reversing its step.

**One build.** Most build systems take the compiler as `CC`:

```sh
make CC=mucc
./configure CC=mucc AR="mucc -ar" RANLIB="mucc -ranlib"
```

**Your shell.** Add this to `~/.bashrc` (or your shell's equivalent), and
every build that reads `$CC` uses mucc:

```sh
export CC=mucc AR="mucc -ar" RANLIB="mucc -ranlib"
```

**Your user, as `cc`.** Builds that run `cc` directly use whatever `cc` is
first in your `PATH`. Put mucc there under that name (no `sudo` needed):

```sh
ln -s ~/.local/bin/mucc ~/.local/bin/cc
hash -r
cc --version          # mucc 1.4.0
```

Undo with `rm ~/.local/bin/cc`.

**The whole system** (Debian, Ubuntu and derivatives). Install mucc where
every user can run it and register it as an alternative for `cc`:

```sh
sudo install -m 755 ~/.local/bin/mucc /usr/local/bin/mucc
sudo update-alternatives --install /usr/bin/cc cc /usr/local/bin/mucc 100
cc --version          # mucc 1.4.0
```

Switch back with `sudo update-alternatives --config cc`, or remove it with
`sudo update-alternatives --remove cc /usr/local/bin/mucc`.

Anything that needs what mucc doesn't do (C++, sanitizers, LTO; see
[Limitations](#limitations)) still needs gcc or clang, so keep them
installed alongside it.

## Using mucc

mucc takes gcc's command-line flags, so most commands you'd type for gcc
work unchanged.

```sh
mucc -o prog main.c util.c         # compile and link
mucc -c main.c                     # compile only: main.o
mucc -o prog main.o util.o         # link objects
mucc -S main.c                     # assembly: main.s
mucc -E main.c                     # preprocess to stdout
mucc -fsyntax-only main.c          # check, write nothing
mucc -std=c23 -o prog prog.c       # C23 (c89, c99, c11, c17 and gnu* too)
mucc -g -o prog prog.c && gdb ./prog
```

### The two C libraries

| | `--libc=mucc` (the default) | `--libc=system` |
| --- | --- | --- |
| C library | musl, built into mucc | the system's glibc |
| Linking | static | dynamic, as gcc does (`-static` for static) |
| Needs on the machine | nothing | glibc's development files (`build-essential`) |
| Programs run on | any Linux for that CPU | machines with a compatible glibc |
| Shared libraries, `dlopen` | no | yes |

Use the default for portable tools. Use `--libc=system` for programs that
link the system's shared libraries (OpenSSL, SDL, GTK, ...), load plugins
with `dlopen`, or need glibc's faster `malloc` and stdio:

```sh
mucc -o tool tool.c                              # portable, static, musl
mucc --libc=system -o app app.c -lssl -lcrypto   # with system libraries
```

`-lm`, `-lpthread`, `-ldl` and `-lrt` work with either.

### Cross-compiling

```sh
mucc --target=aarch64-linux -o hello-arm hello.c   # on an x86-64 machine
```

Any GNU triple for either CPU works, and so does clang's `-target`. As
with gcc on ARM64, plain `char` is unsigned there and `long double` is
128-bit. The released binary only carries musl for its own CPU: to
cross-compile against musl, build from source and run `make libc-aarch64`
(or `make libc-x86_64`). With `--libc=system`, mucc uses Debian and
Ubuntu's cross glibc (`sudo apt install gcc-aarch64-linux-gnu`).

### Static libraries

```sh
mucc -c a.c b.c
mucc -ar rcs libfoo.a a.o b.o      # like GNU ar; mucc -ranlib too
mucc -o prog main.c -L. -lfoo
```

Archives are deterministic, and objects and archives from gcc and mucc
link together either way.

### Assembly

`.s` files go through mucc's own assembler, and `.S` files through the
preprocessor first. It handles AT&T syntax for x86-64, ARM64's base, FP
and common NEON instructions, and GNU as's common directives (`.macro`,
`.rept`, `.p2align`, `.incbin`, ...). Anything it doesn't know is handed
to the system `as` if there is one.

<details>
<summary>All the options mucc acts on</summary>

Other gcc flags that only tune optimization, diagnostics or hardening
(`-O2`, `-fstack-protector`, `-flto`, `-pipe`, ...) are accepted and
ignored. An unknown flag is an error.

| Flag | What it does |
| --- | --- |
| `-o file` | Output file |
| `-c`, `-S`, `-E` | Stop after compiling, after generating assembly, or after preprocessing |
| `-fsyntax-only` | Check the code, write nothing |
| `-I`, `-isystem`, `-iquote`, `-idirafter`, `-nostdinc` | Include directories |
| `-D`, `-U`, `-include`, `-imacros` | Macros |
| `-dM -E` | Print every macro defined at the end |
| `-std=` | C standard: `c89`, `c99`, `c11`, `c17`, `c23` (and `gnu*`) |
| `-g`, `-g0` | Debug info on or off |
| `-Wall`, `-W<name>`, `-Wno-<name>`, `-Werror`, `-w` | Warnings |
| `-L`, `-l`, `-static`, `-shared`, `-r`, `-s`, `-pthread`, `-rdynamic` | Linking |
| `-nostdlib`, `-nostartfiles`, `-nodefaultlibs` | Leave out the C library or startup files |
| `-Wl,a,b`, `-Xlinker a` | Pass options to the linker |
| `-Wp,...` | Pass options to the preprocessor |
| `-Wa,a,b`, `-Xassembler a` | Pass options to `as`. The built-in assembler takes none: it ignores `--noexecstack`, `-g` and `--64` quietly and warns about others |
| `--libc=mucc`, `--libc=system` | Which C library |
| `--target=`, `-target` | Build for x86-64 or ARM64 |
| `-fpic`, `-fPIC`, `-fno-pic` | Position-independent code |
| `-funsigned-char`, `-fsigned-char` | Whether plain `char` is unsigned |
| `-fcommon`, `-fno-common`, `-fvisibility=` | Symbols |
| `-msse3` to `-msse4.2`, `-maes`, `-mpclmul`, `-mpopcnt` | Define gcc's macros for these extensions (`__SSE4_2__` and those before it, ...), so code takes its paths for them. The code mucc makes doesn't change |
| `-march=`, `-mtune=`, `-mcpu=` | Accepted, and change nothing: mucc only emits instructions every x86-64 (or ARMv8-A) CPU has, `-march=native` included |
| `-fno-builtin`, `-ffreestanding` | `fabs`, `fabsf` and `fabsl` are calls, not builtins |
| `-fno-integrated-as`, `-fno-as-fallback`, `-fuse-ld=` | Use the system `as` or `ld`, or never `as` |
| `-x c`, `-x assembler`, `-x assembler-with-cpp`, `-x none` | Type of the next inputs |
| `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, `-MQ`, `-MP`, `-MG` | Dependency files |
| `-v`, `-###`, `--version`, `-dumpversion`, `-dumpmachine`, `-print-search-dirs` | Information |
| `-ar`, `-ranlib` | Run the archiver (as the first argument) |

</details>

## Performance

The mucc measured here is the release binary (`build/mucc`), which is
built by mucc itself, as the one you download is. (A mucc built by
`gcc -O2` compiles about 2.7 times as fast, as the last table shows.)

How long each compiler takes to compile mucc's own source (`src/*.c`,
32,419 lines, 56,573 after preprocessing) to objects:

| Compiler | Time | Lines per second | mucc is |
| --- | ---: | ---: | ---: |
| **mucc 1.4.0** | **0.54 s** | **105,000** | |
| clang 21 `-O0` | 0.84 s | 67,000 | 1.6x faster |
| gcc 15 `-O0` | 1.46 s | 39,000 | 2.7x faster |
| clang 21 `-O2` | 5.27 s | 11,000 | 9.8x faster |
| gcc 15 `-O2` | 7.72 s | 7,300 | 14x faster |

And SQLite's amalgamation, `sqlite3.c` 3.38.2 (239,379 lines in one
file), the usual test of compile speed. tcc, the Tiny C Compiler, is much
faster still: it generates machine code directly in one pass. (It can't
compile mucc's own source, which uses `__int128`.)

| Compiler | Time | Lines per second | mucc is |
| --- | ---: | ---: | ---: |
| tcc 0.9.27 | 0.05 s | 4,200,000 | 16x slower |
| **mucc 1.4.0** | **0.79 s** | **302,000** | |
| clang 21 `-O0` | 0.93 s | 255,000 | 1.2x faster |
| gcc 15 `-O0` | 2.35 s | 102,000 | 3.0x faster |
| clang 21 `-O2` | 12.78 s | 19,000 | 16x faster |
| gcc 15 `-O2` | 16.51 s | 14,000 | 21x faster |

How fast the code each one makes runs, using mucc itself as the program:
mucc built four ways, each compiling the same source.

| mucc built by | Time | Slower than gcc `-O2` by |
| --- | ---: | ---: |
| gcc `-O2` | 0.24 s | |
| gcc `-O0` | 0.37 s | 1.6x |
| mucc, with glibc (`--libc=system`) | 0.43 s | 1.8x |
| mucc, with its musl (the default) | 0.64 s | 2.7x |

Measured on an Intel Core i7-1185G7 laptop under WSL 2 with gcc 15.2,
clang 21.1 and tcc 0.9.27: real time, best of 7 with the compilers taking
turns, one file at a time. Your numbers will differ; run `make bench`
(`test/bench.sh`) to measure them on your machine. It times `sqlite3.c`
too if you download the amalgamation from
[sqlite.org](https://sqlite.org/download.html) and give its path:
`make bench SQLITE3_C=path/to/sqlite3.c`.

In short: mucc compiles faster than gcc and clang, and the code it makes
is a little slower than theirs at `-O0`. The bundled musl's `malloc` and
stdio make programs about 1.4x slower than with glibc. For the fastest
programs, build with gcc or clang at `-O2`.

## Build from source

You need `git` and `make`, plus a C compiler: either mucc itself or gcc.

### With only mucc (no gcc)

Install mucc as in [Install](#install), then:

```sh
git clone https://github.com/Dom-Yan/Mucc.git && cd Mucc
make CC=mucc        # ./mucc
make libc           # musl, built by mucc
make build/mucc     # the single binary, as released
build/mucc --version
```

This takes about 15 seconds. This is how releases are built: each one by
the release before it, with gcc and binutils made to fail so nothing else
takes part (`test/bootstrap.sh`).

### With gcc

```sh
sudo apt install build-essential git        # Ubuntu, Debian, WSL
sudo dnf install gcc make git glibc-devel   # Fedora
sudo pacman -S base-devel git               # Arch

git clone https://github.com/Dom-Yan/Mucc.git && cd Mucc
make && make libc && make build/mucc
```

### Installing what you built

Either copy the single binary over the downloaded one:

```sh
cp build/mucc ~/.local/bin/mucc
```

or install `./mucc` with its headers and musl:

```sh
make install                        # into /usr/local (needs sudo)
make install PREFIX=$HOME/.local    # into ~/.local, no sudo
make uninstall                      # removes it again
```

Then see [Make mucc your default compiler](#make-mucc-your-default-compiler).

### Notes

- `./mucc` built from source uses the system's glibc by default. Use
  `build/mucc`, or `./mucc --libc=mucc` after `make libc`, for the bundled
  musl.
- On WSL 2, clone into your Linux home (`~`), not `/mnt/c`. It is much
  faster.
- `cc: not found` means there is no C compiler: use `make CC=mucc` or
  install gcc.

## How it works

The compiler is a pipeline, one stage per file in `src/`:

| Stage | File | Does |
| --- | --- | --- |
| 1 | `token.c` | source text to tokens |
| 2 | `preprocess.c` | macros, `#include`, `#if` |
| 3 | `parser.c`, `type.c` | tokens to a typed syntax tree |
| 4 | `cgen.c`, `cgen_arm64.c` | syntax tree to x86-64 or ARM64 assembly |
| 5 | `asm.c` | assembly to an ELF object |
| 6 | `link.c` | objects and archives to a static executable |

`main.c` is the driver, `ar.c` is `mucc -ar`, and `diag.c` prints errors
and warnings for every stage. `mucc.h` declares what they share and has a
guide at its top to where common changes go. Each file is split into
sections by `//---------- Name ---` lines (`grep -n '^//-------' src/*.c`
lists them).

The released binary carries its headers (`include/`), musl and Linux's
headers inside it and reads them from memory. On ARM64,
`runtime/arm64.c` provides the `long double` and 128-bit routines that
gcc's libgcc would.

### What's inside the binary

Of the 8.8 MB of `build/mucc` on x86-64:

| Part | Size |
| --- | ---: |
| Linux's headers (`<linux/*.h>`, `<asm/*.h>`, ...) | 3.7 MB |
| musl's `libc.a` and startup files | 3.2 MB |
| The compiler itself, code and data (with the musl functions it uses) | 1.3 MB |
| musl's headers | 0.5 MB |
| mucc's own headers (`include/`) | 0.1 MB |

## Testing

```sh
make test        # language, driver, error, assembler, linker and gdb tests
make test-all    # plus self-hosting, musl, the single binary, and cross-compiling
make difftest    # random programs, compared against gcc
make bench       # the numbers in Performance
```

The tests need gcc and glibc's headers (`build-essential`), since they also
check `--libc=system`. With `gdb`, `qemu-user-binfmt` and
`binutils-aarch64-linux-gnu` installed they also cover debugging and ARM64.
Real programs are tested by `test/thirdparty/*.sh`, which download each
one, build it with mucc and run its own test suite.

The suite has 57 test programs with 2,793 assertions, 8 programs written
from scratch for Linux, over 300 checks of errors and warnings, and the
assembler compared byte for byte with GNU as on 114 programs.

## Contributing

Bug reports and pull requests are welcome. The
[open issues](https://github.com/Dom-Yan/Mucc/issues) are a good place to
start.

- Report a bug with the smallest C file that shows it, the command you
  ran, and what gcc does with it.
- Run `make test-all` before sending a change, and add a test for each
  fix (`test/*.c` for the language, `test/errors.sh` for diagnostics).
- `src/` may only use C that the latest release compiles, since each
  release is built by the one before it.
- Changes should make mucc faster, smaller, clearer or more correct.
- AI tools are fine if you have read and understood every line you
  submit. AI agents should read [AGENTS.md](AGENTS.md).

A note on AI: it was used in this project to help automate tests and to
write some of the comments that explain the logic.

## License

MIT, see [LICENSE](LICENSE). Mucc started from Rui Ueyama's
[chibicc](https://github.com/rui314/chibicc). musl, in `thirdparty/musl/`,
is MIT too.

Linux's headers, in `thirdparty/linux-headers/`, are GPL-2.0 WITH
Linux-syscall-note. That note says programs that use the kernel through
them aren't derived works of it, so they don't change the license of mucc
or of the programs it builds.
