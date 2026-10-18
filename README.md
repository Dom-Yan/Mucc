# The Mucc C Compiler

[![CI](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml/badge.svg)](https://github.com/Dom-Yan/Mucc/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Dom-Yan/Mucc)](https://github.com/Dom-Yan/Mucc/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

Welcome to the Mucc project!

This repository contains the source code for Mucc, a small, fast,
self-hosting C compiler toolchain for x86-64 and ARM64 Linux.

Mucc is one static file of a few megabytes. It contains a C compiler, a
preprocessor, an assembler, a linker and an archiver, along with its own
headers, the musl C library, and Linux's headers. Copy it onto any 64-bit
Linux machine and it builds working C programs with nothing else
installed: no gcc, no binutils, no system headers. The programs it builds
are static, so they run on any Linux for their CPU.

Mucc compiles C17 by default, C23 with `-std=c23`, and the GNU extensions
real code uses. It takes gcc's flags, so build scripts work unchanged, and
it compiles several times faster than `gcc -O0`. Each release is built by
the one before it, with no other compiler or tools, and rebuilds itself
byte for byte.

## Getting Mucc

Download the binary for your CPU:

```sh
curl -Lo mucc https://github.com/Dom-Yan/Mucc/releases/latest/download/mucc-$(uname -m)-linux
chmod +x mucc
./mucc -o hello hello.c && ./hello
```

Consult the [Getting Started with Mucc](https://dom-yan.github.io/Mucc/getting-started.html)
page for installing it, making it your default compiler, and building it
from source.

## Documentation

The [Mucc documentation](https://dom-yan.github.io/Mucc/) covers
everything: the [User Guide](https://dom-yan.github.io/Mucc/user-guide.html),
the [command-line reference](https://dom-yan.github.io/Mucc/options.html),
[C language support](https://dom-yan.github.io/Mucc/language.html),
[performance](https://dom-yan.github.io/Mucc/performance.html),
[how Mucc works](https://dom-yan.github.io/Mucc/internals.html), and the
[changelog](https://dom-yan.github.io/Mucc/changelog.html).

## Contributing

Bug reports and pull requests are welcome on the
[issue tracker](https://github.com/Dom-Yan/Mucc/issues). For how to run
the tests and send a change, take a look at the
[Contributing to Mucc](https://dom-yan.github.io/Mucc/contributing.html)
guide.

## License

Mucc is under the [MIT License](LICENSE). It started from Rui Ueyama's
[chibicc](https://github.com/rui314/chibicc). musl, in
`thirdparty/musl/`, is MIT too. Linux's headers, in
`thirdparty/linux-headers/`, are GPL-2.0 WITH Linux-syscall-note, which
leaves the license of Mucc and of the programs it builds unchanged.
