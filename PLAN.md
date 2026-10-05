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

## Next: bugs found by the deep test (2026-10-05)

A test run with difftest (4,100 programs), hand probes and real projects
(bzip2, cJSON, lz4, miniz, stb, SQLite) found these, each checked against
gcc 15.2. Fix them in this order, with a test for each:

1. **Wrong code: unsigned int constant folding doesn't wrap to 32 bits.**
   `eval2()` (parser.c, "Constant expression evaluation") computes ADD,
   SUB, MUL, NEG, BITNOT and SHL in 64 bits without truncating to a
   4-byte result type. SHR shifts 4-byte unsigned values arithmetically.
   - `UINT32_MAX + 1u` gives 0x100000000.
   - `~0u >> 4` gives all ones.
   - `enum { F = ~0u >> 28 }` gives -1.
   - `static double d = -1u` gives 1.8e19.
   - `if (~4294967295U) A(); else B();` runs neither branch.

   This also explains difftest seeds 50263 and 300316.
2. **Wrong code: multi-character constants.** `'RIFF'` gives 0x52, not
   0x52494646. Fix `read_char_literal` (token.c).
3. **Wrong code: `#if` arithmetic is in int, not intmax_t.**
   `#if (2147483647 + 1) > 0` is false. `eval_pp_expr` (preprocess.c)
   uses the parser's `const_expr`, where literals are int.
4. **Wrong code: `_Bool` bit-fields in static initializers.**
   `static struct { _Bool b:1; } g = {2};` gives 0. `write_bitfield`
   needs a conversion to bool.
5. **ABI: `max_align_t` must have 16-byte alignment** (include/stddef.h).
6. **`__VA_OPT__(..., __VA_ARGS__)` doesn't substitute inside.** `subst()`
   in preprocess.c copies the tokens verbatim.
7. **`&&label` in a function with a VLA is rejected** with "jump into the
   scope of a variable-length array".
8. **Driver flags rejected as unknown.** Each should be handled or
   ignored (main.c `ignored_options[]`):
   - `-isystem`
   - `-ffile-prefix-map=`, `-fdebug-prefix-map=` (Debian and Ubuntu
     default CFLAGS)
   - `-pie`, `-Xlinker`
   - `-funroll-loops`, `-finline-functions`, `-fno-inline`,
     `-ftree-vectorize`
   - `-dumpversion`, `-print-file-name=`, `-print-prog-name=`,
     `-save-temps`, `-fmax-errors=`, `-dD`
9. **Diagnostics:**
   - `#pragma GCC diagnostic` is unsupported. lz4hc.c gets a false
     `-Wunused-function`, which is fatal with `-Werror`.
   - `-Wunused-variable` is on without `-Wall`.
   - `%ms` in scanf formats is miscounted.
10. **Minor:**
    - The `"x"` asm constraint is unsupported.
    - `L ## #x` is rejected.
    - `__FILE_NAME__` is missing.
    - `#x` of a stray backslash is escaped.
    - `(long)&a[1] - (long)&a[0]` in a global initializer is rejected.
    - stb needs `-DSTBI_NO_SIMD`, since mucc defines neither `__GNUC__`
      nor `__SSE2__`.

Not a mucc bug: difftest seeds 51240, 100040 and 300111 come from gcc
folding `0.0 - (double)u` into `-(double)u`. Make the generator avoid
that pattern.

Seen in passing: compiling sqlite3.c takes 0.73 s against gcc -O0's
2.66 s, but peaks at 532 MB of memory against gcc's 297 MB. Worth
looking at, given the goal of being light on memory.

## Later

- Optionally bootstrap releases from the previous release (`make
  CC=mucc` in `release.yml`), so gcc isn't in the release path at all.
- Push main (it is several commits ahead of origin) once the above is
  in.
- The README and website test counts are stale since `__int128`
  (test/int128.c and test/complex.c are new).
- With glibc's headers, `CMPLX` is undefined under mucc: glibc defines
  it only for gcc 4.7+ and clang. `__builtin_complex` works. musl's
  headers are fine.

## Working setup

Build and test in WSL, not on `/mnt/c`. Mirror the Windows tree into a
Linux clone (`~/mucc-dev`, with `git config core.fileMode false`), copy
`src include test Makefile` over, then run `make -j12 test-all` there.
`~/mucc-verify`, `~/mucc-old`, `~/mucc-hunt`, `~/probes` and `~/rw` in
WSL are leftover scratch copies and can be deleted.
