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

Every commit passed `make test-all`. All commits are authored only by
Dom-Yan, with no co-author lines.

## In progress: _Complex (branch `complex`, commit `0bea5ab`)

Not finished, and `make test-all` has not been run on it.

How it works: a complex type is a `TY_STRUCT` with `is_complex` set and
two unnamed members (see `complex_type()` in type.c), so it is stored,
copied and passed as a struct. That matches the psABI, except that a
long double `_Complex` is returned in `%st0`/`%st1` (done in cgen.c).
`add_type()` types complex arithmetic, which is otherwise left alone
while a function is parsed, so `+=` and `++` work unchanged.
`lower_complex()` (parser.c, "Complex numbers") then rewrites it in
place into arithmetic on temporaries, at the end of `function()`. Global
initializers use `eval_complex()`. Conditions, `!` and `(bool)z` test
both parts in cgen's `cmp_zero()`.

Done on the branch: the types (`_Complex`, `__complex__`), imaginary
constants (`2.0i`, `1.0fi`), `__real__`/`__imag__`, `__builtin_complex`
(musl's `CMPLX`), conversions, + - * / == !=, `~` as conjugate, the
long double return, and DWARF.

What's left:

1. **Match gcc for infinities, NaNs and signed zeros.** The fuzzer
   (`wip/cfuzz.c` with `wip/cfuzz_abi.c` built by gcc, compared with
   `wip/fields.py`) differs from gcc in about 60% of lines, in two
   patterns:
   - Multiplying or dividing two complex numbers gives NaN where gcc
     gives an infinity or zero. gcc calls libgcc's `__mulXc3` and
     `__divXc3`, which recover them as C's Annex G says.
   - Dividing `float` complex numbers: current libgcc divides them in
     `double` precision, so some zeros come out with a different sign.

   The planned fix: port libgcc2.c's `__mul{s,d,x}c3` and
   `__div{s,d,x}c3` to C text inside mucc. At parse start, declare them
   as static prototypes named `__mucc_mulsc3` and so on. Have
   `lower_arith()` call them for complex times complex and anything
   divided by a complex number (with 0 as a real dividend's imaginary
   part, as gcc does). At the end of `parse()`, if any was used, tokenize
   and parse their definitions (like `new_builtin_token`, with the line
   of the first use) and mark them live. They must not call libgcc (see
   `test/libgcc.sh`). The double and long double division uses GCC 12+'s
   scaled Smith algorithm with RBIG, RMIN, RMIN2, RMINSCAL and RMAX2;
   float division computes in double.
2. Run the fuzzer until it matches exactly, then add `test/complex.c`
   (with `<complex.h>`: `I`, `creal`, `cimag`, `CMPLX`, calls to musl's
   `cabs`/`cexp`) and error tests for `_Complex int` and `z < w`.
3. `make test-all`. In the commit, remove `wip/` and update the README
   and website ("Not supported" now lists only C++, `_BitInt`, K&R
   definitions and optimization). Then merge into main.

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

## Working setup

Build and test in WSL, not on `/mnt/c`. Mirror the Windows tree into a
Linux clone (`~/mucc-dev`, with `git config core.fileMode false`), copy
`src include test Makefile` over, then run `make -j12 test-all` there.
`~/mucc-verify`, `~/mucc-old`, `~/mucc-hunt`, `~/probes` and `~/rw` in
WSL are leftover scratch copies and can be deleted.
