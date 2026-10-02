# Mucc

[![CI](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml/badge.svg)](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml)

Mucc (`mucc`) is a small, fast, self-hosting C compiler for x86-64 Linux.
It is one static file with its own preprocessor, assembler, linker, headers
and C library (musl) inside. Copy it onto any x86-64 Linux machine and it
builds C programs with nothing else installed: no gcc, no binutils, no
system headers.

Website: <https://dom-yan.github.io/Mucc/>

## Why use it

- **Fast builds.** It compiles about 4.5x faster than `gcc -O0` and 17x
  faster than `gcc -O2`.
- **Nothing to install.** One 11 MB file is the whole toolchain. It works in
  an empty container, on old distributions and on machines with no compiler.
- **Small enough to read.** About 18,000 lines of C that you can understand
  end to end.
- **Real C.** C17 by default, C23 with `-std=c23`. It builds CPython, Git,
  SQLite and Lua, which then pass their own test suites.

Use gcc or clang instead when you need the fastest possible generated code
or a target other than x86-64 Linux. Code mucc generates runs about as fast
as `gcc -O0`.

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

## Use

```sh
mucc -o hello hello.c
./hello
```

It takes the usual gcc flags (`-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-l`,
`-L`, `-static`, `-std=`, ...). `-O`, `-g` and `-W` flags are accepted and
ignored. `mucc -ar` makes static libraries.

Programs are linked statically against the musl inside mucc, so they run on
any x86-64 Linux. To use the system's glibc and shared libraries (OpenSSL,
SDL, `.so` files) instead, add `--libc=system`. That needs the system's C
development packages (`build-essential` on Ubuntu).

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

## Where it runs

- Runs on any 64-bit Intel or AMD Linux, with glibc, musl or no C library
  at all, including WSL 2, containers and CentOS 7.
- Does not run on macOS, Windows outside WSL, ARM or 32-bit x86.
- The programs it builds are x86-64 Linux executables.

## Statistics

| | |
| --- | --- |
| Source | 19,991 lines of C in 13 files |
| Released binary | about 11 MB, with musl and Linux's headers inside |
| Compiling its own source | 0.20 s (gcc `-O0`: 0.91 s, gcc `-O2`: 3.54 s) |
| Tests | 51 programs with 1,934 assertions, 8 Linux programs with 166 checks, plus 262 command-line, error, assembler and linker checks |
| Real programs | CPython 3.10 (402 of 408 test suites pass), Git (21,115 tests pass), SQLite (249,453 tests, 0 errors), Lua, zlib, libpng, TinyCC, QuickJS (its 9 test files pass), the kilo text editor |
| Self-hosting | a mucc built by mucc builds a byte-identical mucc |

Times are from WSL 2 on Ubuntu with gcc 15.2.

## What it supports

- C17 and C23: `bool`, `nullptr`, `constexpr`, `auto`, `#embed`,
  `typeof`, `[[attributes]]`, checked arithmetic (`<stdckdint.h>`) and more
- The full preprocessor, VLAs, `_Generic`, atomics, thread-local variables,
  structs by value, varargs
- GNU extensions: statement expressions, computed `goto`, case ranges,
  `__attribute__`, extended `asm`, and gcc's builtins for bit counting
  (`__builtin_clz`, `__builtin_popcount`, ...), byte swaps, branch hints,
  overflow checks (`__builtin_add_overflow`, ...) and atomics (`__sync_*`,
  `__atomic_*`)
- Code that can't run isn't compiled, as with gcc: `if (0)`, `0 && x`,
  constant `?:` and statements after a `return`. Code like
  `if (ENABLE_FEATURE) f();` links without f.
- Inline assembly for kernels and drivers: port I/O (`inb`, `outl`, ...),
  `cpuid`, `rdtsc`, `rdmsr`, `lgdt`, `cli`, `iretq` and other system
  instructions, all assembled without binutils
- Assembly files: `.s`, and `.S`, which go through the preprocessor first
- Its own linker: static executables, relocatable objects (`-r`, as
  Linux's kbuild uses for `built-in.o`) and map files (`-Wl,-Map,FILE`)
- Linux's own headers (`<linux/*.h>`, `<asm/*.h>`, ...) come with the
  bundled musl, for programs that use the kernel directly: USB through
  usbfs, input devices, netlink, ioctls
- Not supported: C++, `_Complex`, `__int128`, `_BitInt`, `asm goto`, K&R
  function definitions, optimization beyond register allocation and
  constant folding

## Testing

```sh
make test        # language, driver, error, assembler and linker tests
make test-all    # also self-hosting, musl, the single binary, and
                 # programs written from scratch for Linux, each built by
                 # the single binary alone in an empty root
make difftest    # random programs compared against gcc
```

The tests need gcc and glibc's headers (`build-essential`), since they also
check `--libc=system`.

## Contributing

- Run `make test-all` before every commit, and add a test for each fix.
- `src/` may only use C that mucc supports, since mucc compiles itself.
- Changes should make mucc faster, smaller, clearer or more correct.
- AI tools are fine if you have read every line you submit. AI agents should
  read [AGENTS.md](AGENTS.md).

## License

MIT. See [LICENSE](LICENSE). musl, in `thirdparty/musl/`, is MIT too.

Linux's headers, in `thirdparty/linux-headers/`, which the released
binary carries, are the kernel's: GPL-2.0 WITH Linux-syscall-note. That
note says programs that use the kernel through them aren't derived works
of it, so they don't change the license of mucc or of programs it builds.
