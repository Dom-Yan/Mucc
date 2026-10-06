# Plan

Where mucc is going, what was done, and what comes next. Read this first
when picking the work up again.

## The goal

mucc is a daily-driver C compiler for x86-64 Linux, and only that:

- One static ELF that needs nothing else installed: no gcc, binutils or
  system headers. Copy it into WSL on a new laptop and start programming.
- Compiles basically all valid C, and can sometimes stand in for gcc.
- Its niche is debug and test builds with very fast compile times. Use
  gcc or clang for optimized or cross-platform builds.
- Fully self-hosted, small, not bloated, light on memory.
- Every release is built by mucc. `release.yml` does this already: the
  objects and musl in `build/mucc` are compiled, assembled and linked by
  mucc. gcc builds only the throwaway stage-1 compiler.
- The code stays documented, in sections (see the guide at the top of
  `src/mucc.h`).

The linter idea from 2026-10-04 is dropped. The `linter` and
`llvm-backend` branches are parked; keep them for their history.

## Done on 2026-10-05 (on main, not pushed)

- `08af0a3` README and website: line and test counts were stale.
- `acbdc14` README and website: compile times of the released binary
  (0.45 s for mucc's own source, 2.4x faster than gcc -O0, 10x than -O2).
  The old 0.20 s came from a gcc-built mucc. Method: `src/*.c` to
  objects one file at a time, best of 7, with `build/mucc`.
- `13e6728` asm goto, with outputs and cleanups.
- `ef27a4f` __int128. 200,000 random operations and conversions match
  gcc exactly.
- `52c2124` _Complex. Multiplying and dividing call helpers ported from
  libgcc2.c (`__mucc_mulsc3` and so on, parsed from C text at the end of
  `parse()` when used). 600,000 random operations match gcc exactly, but
  for NaN signs, which gcc -O0 and -O2 don't agree on either. musl's
  complex functions are now built into the bundled libc. The fuzzer is
  in the `complex` branch's history (`wip/cfuzz.c`).

Every commit passed `make test-all`. All commits are authored only by
Dom-Yan, with no co-author lines.

Then, the same day:

- The deep test's ten bugs, all fixed with tests: 32-bit constant
  folding, multi-character constants, `#if` in intmax_t, `__VA_OPT__`
  substitution, `_Bool` bit-field initializers, `&&label` with VLAs,
  `max_align_t`, driver flags (`-isystem`, `-v`, `-dumpversion`,
  `-print-*-name=`, ignored optimization and hardening flags),
  `#pragma GCC diagnostic`, -Wall-only warnings, scanf `%ms`, the `"x"`
  asm constraint, `L ## #x`, `__FILE_NAME__`, `#x` escaping, address
  differences in initializers.
- Memory: sqlite3.c peaks at 430 MB, from 540 MB (gcc -O0: 297 MB).
  Tokens are 96 bytes (were 128), AST nodes 280 (were 336), and a macro
  expansion copies each token once (was three times). Same object code.
- A second deep test (a subagent, about 2 hours: hand probes, mutation
  fuzzing of test/*.c, cross-compiler ABI checks, gawk, sed, make, Lua,
  redis, jq) found more. Fixed, with tests:
  - Wrong code: user functions named `err`/`verr`/... were noreturn;
    struct returns over 16 bytes left a dead address in %rax; enums
    wider than int were cut to 32 bits; psABI classes of `__int128`
    members, padding-only upper halves and packed structs; sizeof with
    an initialized flexible array member; `_Alignas` on static locals.
  - gcc's target macros (`__BYTE_ORDER__`, `__CHAR_BIT__`, limits,
    `__FLT_*`, ...); `__STDC_NO_COMPLEX__` dropped.
  - `defined`/`__has_c_attribute`/`__has_include` from a macro in `#if`
    (every gnulib project failed); `#include FOO` crash; a bare
    `#define`/`#undef`/`#ifdef` took the next line's first word.
  - Stack overflow on wide code (a 1 GB stack for the compiler process,
    as gcc has); 100,000 globals took 54 s (now 0.47 s).
  - `-MD -c -o obj/x.o` target; `-g` line at function entry (gdb's
    `break main`) and macro code on its line of use; local `.comm`
    placement in the assembler.
- difftest: 3,000 programs, 0 real failures. The generator no longer
  makes `0.0 - x`, which gcc folds into `-x`.

## Done later on 2026-10-05

- `58eebeb` `__GNUC__` is 4.2, as clang has it (the maintainer's
  decision). Asm labels on functions and globals, attributes `mode`,
  `transparent_union`, `common`/`nocommon`, and `include/limits.h`.
  Every standard and POSIX header compiles alone without a warning
  under glibc and musl, -std=c99 to c23, with the usual feature macros
  (5,375 of 5,400; the rest is glibc's `<tgmath.h>`, see below). redis
  7.2.5 builds and runs; Lua, make and sqlite (libtool) pass their
  tests; sed and gawk as before (gawk's same 8 failures).
- `54e1d4a` `?:` with pointer operands took the first one's type: musl's
  `<tgmath.h>` computed `sqrt(double)` in float. C's rules now, and
  `*p` on a `void *` is a void expression, as in gcc.

- `cb8668e` Initializers make a node only for what is set, keep strings
  as bytes, and data is emitted as `.zero` and 16-byte `.byte` lines:
  `int a[10000000] = {[9999999] = 1}` 8.3 s and 1.6 GB -> 0.28 s and
  83 MB, a 2 MB string 766 MB -> 35 MB, sqlite3.c 0.75 s -> 0.57 s.
- `f6dbcae` `#embed`'s bytes are one token, copied into the data: 2 MB
  1.7 GB -> 33 MB.
- `7c41ca3` `-E` printed adjacent string literals as only the first.
- `0b4cbbd` Conflicting redeclarations (functions, globals, typedefs,
  struct/union/enum definitions) are errors. sed's `make check` passes
  now, gnulib's tests too (the ioctl wrapper). A prototype after `int
  f()` becomes f's type.
- `a488a24` `include/tgmath.h` is musl's, for glibc too. Every C and
  POSIX header now compiles alone without a warning under both libcs
  (10,800 of 10,800).
- `e4ee992` `__auto_type`, `int a[k = 3]`, `"xyz"[1]` as a constant, and
  `int x = {1, 2}`.
- `8bb43a7` `__int128`: switch, overflow builtins (a helper from C text),
  and constants of any value (`eval128()`).
- `76eaa46` Cleanup variables at the end of a statement expression.
- `b3d8113` Driver: `@file`, `-MM`, `-MG`, `-dM`, `-imacros`, `-iquote`,
  `-nostdinc`, `-fsyntax-only`, `-x c-header`, `-print-search-dirs`,
  `-l foo`; `-specs=`, `--param`, `-mcmodel=small`, `-Xassembler`
  accepted. creal, cimag and conj are builtins (no -lm).

Found on the way:
- stb includes `<emmintrin.h>` on every x86-64 build, `__GNUC__` or
  not: it needs SSE intrinsics (`-DSTBI_NO_SIMD -DSTBIR_NO_SIMD` until
  then; with them, stb and miniz match gcc's output). The old "stb
  fails" result was the harness, which left out miniz's other files.
- `cos(sin(tan(f)))` with musl's `<tgmath.h>` expands to 44 MB of
  tokens. gcc takes 1.2 GB; mucc took over 4 GB and, with no limit,
  ran WSL out of memory.

## Next: the deep test's other findings

In order:

1. **Accepted, rejected by gcc**: duplicate labels, sizeof an
   incomplete struct or a bit-field, bad bit-field widths, `int x; int
   x;` in a block, `&` of a register variable, `static extern`, a
   flexible array member not last, a block-scope `extern int x;` and a
   later file-scope `long x;`, `int f(void x)`, arrays of
   functions, duplicate macro parameters, `#define f(` with parameters
   on the next line.
2. **-g, minor**: a VLA shows as a pointer, `va_list` has no fields, a
   loop variable shows after its scope.
3. **Memory, more**: implicit casts are full nodes and copy their type
   (49 MB and 27 MB of sqlite3.c's); a function's tokens and AST could
   be freed once it is emitted. Macro expansion: musl's `<tgmath.h>`
   nested three deep takes over 4 GB (gcc: 1.2 GB).
4. **SSE intrinsics** (`<emmintrin.h>`, `<xmmintrin.h>`, attribute
   `vector_size`): stb and many other libraries use them on x86-64
   without a fallback. The biggest gap left for real code bases.
5. **Bigger and rare**: arrays of 2 GiB or more (sizes are `int` all
   through: types, offsets, codegen); `__int128` bit-fields (layout,
   loads and stores in 128 bits, static initializers).

## Later

- Optionally bootstrap releases from the previous release (`make
  CC=mucc` in `release.yml`), so gcc isn't in the release path at all.
- Push main (it is many commits ahead of origin).
- With glibc's headers, `CMPLX` is undefined under mucc: glibc defines
  it only for gcc 4.7+ and clang. `__builtin_complex` works. musl's
  headers are fine.

## Working setup

Build and test in WSL, not on `/mnt/c`. Mirror the Windows tree into a
Linux clone (`~/mucc-dev`, with `git config core.fileMode false`), copy
`src include test Makefile` over, then run `make -j12 test-all` there.
`~/mucc-verify`, `~/mucc-old`, `~/mucc-hunt`, `~/probes` and `~/rw` in
WSL are leftover scratch copies and can be deleted.
