# Mucc

[![CI](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml/badge.svg)](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml)

Mucc (`mucc`) is a small, fast, self-hosting C compiler for x86-64 Linux.
It is one static file with its own preprocessor, assembler, linker, archiver,
headers and C library (musl) inside. Copy it onto any x86-64 Linux machine
and it builds C programs with nothing else installed: no gcc, no binutils,
no system headers.

Website: <https://dom-yan.github.io/Mucc/>

## Contents

- [Why use it](#why-use-it)
- [Get it](#get-it)
- [Quick start](#quick-start)
- [Using mucc](#using-mucc)
  - [Compiling and linking](#compiling-and-linking)
  - [The two C libraries](#the-two-c-libraries)
  - [Libraries and archives](#libraries-and-archives)
  - [Build systems: make, configure and friends](#build-systems-make-configure-and-friends)
  - [C standards](#c-standards)
  - [Warnings and errors](#warnings-and-errors)
  - [Debugging](#debugging)
  - [Assembly](#assembly)
  - [Using it in place of gcc](#using-it-in-place-of-gcc)
- [Options](#options)
- [What it supports](#what-it-supports)
- [Limitations](#limitations)
- [Performance](#performance)
- [Where it runs](#where-it-runs)
- [Build from source](#build-from-source)
- [How it works](#how-it-works)
- [Testing](#testing)
- [Statistics](#statistics)
- [Contributing](#contributing)
- [License](#license)

## Why use it

- **Nothing to install.** One 8.5 MB file is the whole toolchain. It works
  in an empty container, on old distributions and on machines with no
  compiler at all.
- **Programs that run anywhere.** By default it links programs statically
  against its own musl, so what it builds runs on any x86-64 Linux.
- **Fast builds.** It compiles about 2.7x faster than `gcc -O0` and 12x
  faster than `gcc -O2`.
- **Real C.** C17 by default, C23 with `-std=c23`, and the GNU extensions
  real code uses. SQLite, Lua, Git, CPython, Redis, Tcl, jq, zlib and
  others build with it and pass their own test suites.
- **Self-hosting.** Each release is built by the release before it, with
  gcc and binutils made to fail, and a mucc built by mucc builds a
  byte-identical mucc.
- **Small enough to read.** About 28,000 lines of C, in clearly marked
  sections, that one person can understand end to end.

Use gcc or clang instead when you need the fastest generated code, C++, or
a target other than x86-64 Linux.

## Get it

On any x86-64 Linux (Ubuntu, Debian, Fedora, Arch, Alpine, WSL 2, ...):

```sh
mkdir -p ~/.local/bin
curl -Lo ~/.local/bin/mucc https://github.com/Dom-Yan/Mucc/releases/latest/download/mucc-x86_64-linux
chmod +x ~/.local/bin/mucc
mucc --version
```

If `mucc` is not found, open a new terminal or add `~/.local/bin` to your
`PATH`. To update, run the same `curl` again. To uninstall, delete the file.

Each release also has a `.sha256` file to check the download with
`sha256sum -c`.

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

`hello` is a static executable: copy it to any x86-64 Linux machine and it
runs there.

## Using mucc

mucc takes the same command-line flags as gcc, so most commands you would
type for gcc work as they are.

### Compiling and linking

```sh
mucc -o prog main.c util.c         # compile and link in one step
mucc -c main.c                     # compile only: main.o
mucc -c -o build/util.o util.c     # compile to a chosen file
mucc -o prog main.o build/util.o   # link objects
mucc -S main.c                     # assembly: main.s
mucc -E main.c                     # preprocess only, to stdout
mucc -fsyntax-only main.c          # check, write nothing
```

Common flags: `-I dir` adds an include directory, `-D NAME[=VALUE]` and
`-U NAME` define and undefine macros, `-L dir` and `-l name` add library
directories and libraries, `-include file` includes a file first, and
`-x c` (or `-x assembler`) says what the next inputs are. `-` reads
standard input. Dependency files for make work as with gcc: `-M`, `-MM`,
`-MD`, `-MMD`, `-MF`, `-MT`, `-MQ`, `-MP`.

`-O0` to `-O3` are accepted and ignored: mucc always compiles the same way
(see [Performance](#performance)).

### The two C libraries

mucc can build against either of two C libraries:

| | `--libc=mucc` (the default for the released binary) | `--libc=system` |
| --- | --- | --- |
| C library | musl, inside mucc | the system's glibc |
| Linking | static | dynamic, as gcc does (`-static` for static) |
| Needs on the machine | nothing | glibc's development files (`build-essential` on Ubuntu); `ld` for shared libraries |
| Programs run on | any x86-64 Linux | machines with a compatible glibc |
| Shared libraries, `dlopen`, `.so` files | no | yes |
| Speed of the C library | slower `malloc` and stdio | faster |

Use the default for portable tools and anything that should run without
installing things. Use `--libc=system` for programs that need the system's
shared libraries (OpenSSL, SDL, GTK, ...), plugins loaded with `dlopen`,
or the fastest C library.

```sh
mucc -o tool tool.c                              # portable, static, musl
mucc --libc=system -o app app.c -lssl -lcrypto   # with system libraries
```

`-lm`, `-lpthread`, `-ldl`, `-lrt` and the other libraries that musl puts
inside libc all work with the default, as they do with glibc.

### Libraries and archives

`mucc -ar` makes static libraries, as GNU `ar` does, and `mucc -ranlib`
rewrites an archive's index:

```sh
mucc -c a.c b.c
mucc -ar rcs libfoo.a a.o b.o
mucc -o prog main.c -L. -lfoo
```

Archives are deterministic: dates, owners and modes are fixed, so the same
inputs give the same bytes. mucc also links archives and objects made by
gcc and GNU `ar`.

Shared libraries (`-shared`, `-fPIC`) need `--libc=system` and the
system's `ld`.

### Build systems: make, configure and friends

Point the build at mucc as its C compiler:

```sh
make CC=mucc
./configure CC=mucc AR="mucc -ar" RANLIB="mucc -ranlib"
```

Autoconf `configure` scripts, plain Makefiles and Linux-style builds work:
SQLite, Lua, Git, CPython, Redis, Tcl, jq, zlib, libpng, BusyBox and others
are built this way in mucc's own tests. Like clang, mucc says it is gcc 4.2
(`__GNUC__` is 4), so headers and scripts that look for gcc take their gcc
paths but don't expect a newer gcc's features.

### C standards

C17 is the default. `-std=` picks another: `c89`, `c99`, `c11`, `c17` and
`c23` (and their `gnu` spellings). `__STDC_VERSION__` is set to match.

```sh
mucc -std=c23 -o prog prog.c
```

### Warnings and errors

Errors and warnings point at the line and column, with the source line
shown, as gcc does. Warnings work as with gcc:

- `-Wall` adds `printf` and `scanf` format checks, `if (x = 0)`,
  statements with no effect, comparisons with string literals, unused
  variables and static functions, and enum values a `switch` misses.
- Some warn by default: returning a local's address, division by zero,
  bad shift counts, unknown attributes, excess initializer elements.
- `-Werror`, `-Werror=<name>`, `-Wno-<name>` and `-w` work, and so does
  `#pragma GCC diagnostic`.

### Debugging

`-g` writes DWARF debug info, so gdb shows variables, parameters, globals,
structs, `const` and `volatile` types, and backtraces, as with
`gcc -g -O0`:

```sh
mucc -g -o prog prog.c
gdb ./prog
```

Line numbers are always recorded, so backtraces have them even without
`-g`.

### Assembly

mucc assembles `.s` files with its own assembler, and `.S` files after
running them through the preprocessor. Inline `asm` and extended `asm`
(with operands, clobbers and `asm goto`) work in C.

The built-in assembler knows the instructions mucc and typical
hand-written code use, including system instructions like `cpuid`,
`rdtsc`, `rdmsr`, `lgdt` and `iretq`, and GNU as's common directives:
`.byte` to `.quad`, `.float`, `.double`, `.ascii`, `.string`, `.skip`,
`.fill`, `.align`, `.p2align`, `.set`, `.equ`, `.rept`, `.macro`,
`.section`, `.pushsection`, `.comm`, `.incbin` and more. Anything it
doesn't know, such as Intel syntax, is handed to the system `as` if there
is one (`-fno-as-fallback` turns that off, `-fno-integrated-as` always uses
`as`).

### Using it in place of gcc

To make mucc the compiler for everything that runs `cc`, link it under
that name earlier in your `PATH`:

```sh
ln -s ~/.local/bin/mucc ~/.local/bin/cc
```

Builds that need things mucc doesn't do (C++, sanitizers, LTO,
`-march`-specific code; see [Limitations](#limitations)) still need gcc or
clang.

## Options

The flags mucc acts on. Other gcc flags that only tune optimization,
diagnostics or hardening (`-O2`, `-fstack-protector`, `-flto`, `-pipe`,
...) are accepted and ignored. An unknown flag is an error.

| Flag | What it does |
| --- | --- |
| `-o file` | Output file |
| `-c`, `-S`, `-E` | Stop after compiling, after generating assembly, or after preprocessing |
| `-fsyntax-only` | Check the code, write nothing |
| `-I`, `-isystem`, `-iquote`, `-idirafter`, `-nostdinc` | Include directories |
| `-D`, `-U`, `-include`, `-imacros` | Macros |
| `-dM -E` | Print every macro defined at the end |
| `-std=` | C standard: `c89`, `c99`, `c11`, `c17`, `c23` |
| `-g`, `-g0` | Debug info on or off |
| `-Wall`, `-W<name>`, `-Wno-<name>`, `-Werror`, `-w` | Warnings |
| `-L`, `-l`, `-static`, `-shared`, `-r`, `-s`, `-pthread`, `-rdynamic` | Linking |
| `-nostdlib`, `-nostartfiles`, `-nodefaultlibs` | Leave out the C library or startup files |
| `-Wl,a,b`, `-Xlinker a` | Pass options to the linker |
| `-Wp,...`, `-Xassembler a` | Pass options to the preprocessor or `as` |
| `--libc=mucc`, `--libc=system` | Which C library (see above) |
| `-fpic`, `-fPIC`, `-fno-pic` | Position-independent code |
| `-funsigned-char`, `-fsigned-char` | Whether plain `char` is unsigned |
| `-fcommon`, `-fno-common`, `-fvisibility=` | Symbols |
| `-msse3` to `-msse4.2`, `-maes`, `-mpclmul`, `-mpopcnt` | Define the macros for these extensions |
| `-fno-integrated-as`, `-fno-as-fallback`, `-fuse-ld=` | Use the system `as` or `ld`, or never `as` |
| `-x c`, `-x assembler`, `-x assembler-with-cpp`, `-x none` | Type of the next inputs |
| `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, `-MQ`, `-MP`, `-MG` | Dependency files |
| `-v`, `-###`, `--version`, `-dumpversion`, `-dumpmachine`, `-print-search-dirs` | Information |
| `-ar`, `-ranlib` | Run the archiver (as the first argument) |

## What it supports

- **C17 and C23:** `bool`, `nullptr`, `constexpr`, `auto`, `#embed`,
  `typeof`, `[[attributes]]`, `_Static_assert`, checked arithmetic
  (`<stdckdint.h>`), `<stdbit.h>` and more.
- **The whole of C:** the full preprocessor, VLAs, `_Generic`, `_Atomic`
  and `<stdatomic.h>`, thread-local variables, structs and unions by
  value, bit-fields, varargs, complex numbers (`_Complex` and
  `<complex.h>`), designated initializers, compound literals, and K&R
  function definitions.
- **GNU extensions:** `__int128`, statement expressions, computed `goto`,
  case ranges, `__attribute__` (including `mode`, `vector_size`,
  `transparent_union`, `cleanup`, `constructor`, `weak`, `alias`,
  `section`, `visibility`, `aligned` and `packed`), asm labels, extended
  `asm` and `asm goto`, and gcc's builtins for bit counting, byte swaps,
  branch hints, overflow checks, atomics (`__sync_*`, `__atomic_*`),
  floating point and the C library.
- **SIMD:** gcc's vector types of 4, 8 or 16 bytes with their operators,
  passed in XMM registers as the psABI says, and the x86 intrinsics
  headers (`<immintrin.h>` and the rest) up to SSE4.2, AES and PCLMUL.
- **Debugging and warnings:** see [Debugging](#debugging) and
  [Warnings and errors](#warnings-and-errors).
- **Its own toolchain:** an assembler, a linker for static executables,
  relocatable objects (`-r`) and map files (`-Wl,-Map,FILE`), and an
  archiver.
- **Linux's own headers** (`<linux/*.h>`, `<asm/*.h>`, ...) come with the
  bundled musl, for programs that use the kernel directly: usbfs, input
  devices, netlink, ioctls.
- **gcc's object format and calling convention,** so objects and archives
  from gcc and mucc link together either way.

## Limitations

What mucc does not do, and what to use instead.

**Platform**

- It runs on, and builds programs for, x86-64 Linux only. No macOS,
  Windows (except under WSL 2), ARM or 32-bit x86.

**Language**

- C only: no C++ or Objective-C.
- Not supported: `_BitInt`, nested functions, `__label__`, variable-length
  arrays inside structs, integer `_Complex`, and vectors of sizes other
  than 4, 8 and 16 bytes.
- Implicit function declarations are errors, as C99 and later require.
- Some gcc attributes that change what a program does are errors rather
  than silently ignored: `ifunc`, `naked`, `target`, `target_clones`,
  `symver`, `weakref`, `retain`, `copy`, `noinit`, `persistent`,
  `scalar_storage_order` and `hardbool`. Unknown attributes only warn.
- No AVX or later instructions: code that checks `__AVX__` takes its SSE
  path.
- mucc accepts some invalid programs that gcc and clang reject. It rejects
  the common mistakes, but it isn't a strict checker.

**Code generation**

- No optimization beyond register allocation and constant folding.
  Generated code runs about as fast as `gcc -O0`. No LTO, profile-guided
  optimization or `-march`-specific code.
- No sanitizers (`-fsanitize=`), coverage (`--coverage`) or profiling
  (`-pg`).
- No unwind tables (`.eh_frame`): fine for C, but `.cfi_*` directives in
  assembly are dropped.

**The bundled C library (musl)**

- Programs are static only: no shared libraries, no `dlopen`. Use
  `--libc=system` for those.
- musl's `malloc` and stdio are slower than glibc's, which makes some
  programs 1.3 to 1.5 times slower than with `--libc=system`.
- musl's locales are basic (`setlocale` accepts names but changes little),
  its `iconv` differs from glibc's in small ways, and threads get 128 KB
  stacks unless a program asks for more.

**Toolchain**

- Shared libraries (`-shared`) are linked by the system's `ld`.
- Intel-syntax assembly is handed to the system `as`.
- Objects from gcc that call libgcc's helpers (complex multiplication,
  128-bit division) need gcc's `libgcc.a` to link.

## Performance

| | Time |
| --- | --- |
| mucc 1.3.0 compiling its own source | 0.46 s |
| gcc 15.2 `-O0`, the same | 1.22 s |
| gcc 15.2 `-O2`, the same | 5.35 s |

The generated code runs about as fast as `gcc -O0` with glibc
(`--libc=system`), and about 1.3 to 1.5 times slower with the bundled musl,
whose `malloc` and stdio are slower. For the fastest programs, use gcc or
clang with `-O2`.

## Where it runs

- Any 64-bit Intel or AMD Linux, with glibc, musl or no C library at all:
  current distributions, old ones like CentOS 7, containers, and WSL 2.
- The released binary is static and needs nothing else, not even `/proc`
  or `/tmp` (it falls back to the current directory for temporary files).
- The programs it builds are x86-64 Linux executables.

## Build from source

### With only mucc (any x86-64 Linux)

Needs `mucc` from [Get it](#get-it), plus `git` and `make`. No gcc.

```sh
git clone https://github.com/Dom-Yan/Mucc.git && cd Mucc
make CC=mucc        # ./mucc
make libc           # musl, built by mucc
make build/mucc     # the single binary, as released
build/mucc --version
```

This takes about 15 seconds. Copy `build/mucc` anywhere, for example over
`~/.local/bin/mucc`.

Releases are built this way, by the release before them, with gcc and
binutils made to fail so nothing else can take part:
`test/bootstrap.sh mucc out/mucc -s` does the same. Whichever compiler
starts the chain, the result is the same bytes (`make test-bootstrap`).

### With gcc

Install the build tools first:

```sh
sudo apt install build-essential git        # Ubuntu, Debian, WSL
sudo dnf install gcc make git glibc-devel   # Fedora
sudo pacman -S base-devel git               # Arch
```

Then:

```sh
git clone https://github.com/Dom-Yan/Mucc.git && cd Mucc
make && make libc && make build/mucc
```

`make install` puts mucc in `/usr/local` (`make install PREFIX=$HOME/.local`
for no `sudo`), and `make uninstall` removes it.

### Notes

- A `./mucc` built from source uses the system's glibc by default. Use
  `build/mucc`, or `./mucc --libc=mucc` after `make libc`, to compile
  without it.
- On WSL 2, clone into your Linux home (`~`), not `/mnt/c`. It is much
  faster there.
- `cc: not found` or `cc: Not a directory` means gcc is not installed. Use
  the steps under [With only mucc](#with-only-mucc-any-x86-64-linux) or
  install gcc.

## How it works

The compiler is a pipeline, one stage per file in `src/`:

1. `token.c` turns source text into tokens.
2. `preprocess.c` expands macros and includes.
3. `parser.c` and `type.c` build a typed syntax tree.
4. `cgen.c` writes x86-64 assembly, with register allocation for
   variables and constant folding.
5. `asm.c` assembles it into an ELF object file.
6. `link.c` links objects and archives into a static executable.

`main.c` is the driver that runs them, `ar.c` is `mucc -ar`, and `mucc.h`
declares what they share, with a guide at its top to where common changes
go. Each file is split into sections by `//---------- Name ---` lines:
`grep -n '^//-------' src/*.c` lists them.

The released binary carries its headers (`include/`), musl's headers and
libraries, and Linux's headers inside it, and reads them from memory.

## Testing

```sh
make test        # language, driver, error, assembler, linker and debugger tests
make test-all    # also self-hosting, musl, the single binary, and
                 # programs written from scratch for Linux, each built by
                 # the single binary alone in an empty root
make difftest    # random programs compared against gcc
```

The tests need gcc and glibc's headers (`build-essential`), since they also
check `--libc=system`. Real programs are tested by the scripts in
`test/thirdparty/`, one per program, which download it, build it with
mucc and run its own test suite.

## Statistics

| | |
| --- | --- |
| Source | 27,667 lines of C in 13 files (`wc -l src/*`) |
| Released binary | about 8.5 MB, with musl and Linux's headers inside |
| Compiling its own source | 0.46 s (gcc `-O0`: 1.22 s, gcc `-O2`: 5.35 s) |
| Tests | 57 programs with over 2,700 assertions, 8 Linux programs with 166 checks, plus hundreds of command-line, error, assembler, linker and debugger checks |
| Real programs | With the bundled musl: SQLite (249,453 tests, 0 errors), Lua, zlib, Redis, Tcl, jq, the kilo text editor. With `--libc=system`: CPython 3.10 (402 of 408 test suites pass), Git (21,115 tests pass), libpng, TinyCC, QuickJS (its 9 test files pass) |
| Self-hosting | a mucc built by mucc builds a byte-identical mucc |

Times are for the released binary, compiling `src/*.c` to objects one file
at a time, best of 7, on WSL 2 on Ubuntu with gcc 15.2.


## Disclaimer 
AI was used in this project but for automating tests. And Comments explaining some logic. 

## Contributing

- Run `make test-all` before every commit, and add a test for each fix.
- `src/` may only use C that the latest release compiles, since each
  release is built by the one before it.
- Changes should make mucc faster, smaller, clearer or more correct.
- AI tools are fine if you have read every line you submit. AI agents should
  read [AGENTS.md](AGENTS.md).

## License

MIT. See [LICENSE](LICENSE). musl, in `thirdparty/musl/`, is MIT too.

Linux's headers, in `thirdparty/linux-headers/`, which the released
binary carries, are the kernel's: GPL-2.0 WITH Linux-syscall-note. That
note says programs that use the kernel through them aren't derived works
of it, so they don't change the license of mucc or of programs it builds.
