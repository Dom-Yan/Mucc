# Plan: a self-sufficient mucc

## The goal

One file, `mucc`, that is the whole toolchain. Copy it onto any x86-64 Linux
machine and it compiles, assembles and links C programs with nothing else
installed: no gcc, no binutils, no glibc, no system headers. The programs it
builds run on any x86-64 Linux machine.

mucc keeps a second mode, `--libc=system`, that works the way mucc works
today (glibc, `ld`, system libraries), for programs that need libraries built
against glibc, such as OpenSSL or SDL.

### Done means

The goal is reached when all of these pass:

- [ ] In an empty container (`FROM scratch`) holding only the `mucc` binary
  and `hello.c`, `mucc -o hello hello.c && ./hello` works.
- [ ] That same binary builds SQLite, Lua, zlib and mucc itself, and each one
  passes its own tests, on Ubuntu, Fedora, Alpine and one old distribution.
- [ ] mucc rebuilds itself byte-identically in that empty container.
- [ ] `--libc=system` mode still passes everything mucc passes today.

## How to use this file

Every item has two boxes:

- **Added:** check it in the commit that makes the change.
- **Verified:** check it only after running the item's test, and after the
  checks below all pass. Until then the item is not done.

Every item, before it is verified:

1. has a test in `test/` (or a script that is run by CI) that fails without
   the change and passes with it,
2. passes `make test-all` and `make difftest N=300`,
3. passes CI on GitHub,
4. updates the README if users would notice it,
5. is one commit, or a few small ones, that can be read on their own.

Items are done in order within a phase. A later phase can start once the
items it depends on are verified.

## Facts this plan is built on

Checked on 2026-09-23 against the source:

- **mucc's generated code calls nothing in `libgcc`.** No helper functions
  showed up across all 43 test programs and mucc itself. Only the C library
  has to be replaced, not gcc's runtime.
- **mucc itself uses about 100 C library functions**, including `fork`,
  `execvp`, `wait`, `glob`, `mmap`, `open_memstream` and `strtold`, plus
  `<elf.h>`. A replacement C library must provide all of them.
- **The system paths are hard-coded in `src/main.c`**: `/usr/include`, glibc's
  `crt1.o`/`crti.o`/`crtn.o`, gcc's `crtbegin.o`/`crtend.o`, and `-lgcc`,
  `-lgcc_eh`, `-lgcc_s`.
- **mucc drops functions nothing refers to** (`src/parser.c`, `is_live`), so
  attributes like `used` and `constructor` must keep a function alive.
- **Only `packed` and `aligned` on structs are understood today**
  (`src/parser.c`, `attribute_list`). C23 `[[...]]` attributes are all
  dropped before parsing (`remove_attributes`), except `[[noreturn]]`, so
  `[[gnu::cleanup(f)]]` or `[[gnu::aligned(16)]]` are silently ignored.
- **`#embed` uses about 250 bytes of memory per embedded byte**, and the
  assembler has no `.incbin`. Embedding a multi-megabyte library needs one of
  those fixed first.
- **mucc has no archiver.** Building a `libc.a` needs `ar`, which comes from
  binutils.
- **`test/thirdparty/*.sh` clone into `/thirdparty`**, which `.gitignore`
  ignores, and use `git@github.com:` URLs, which need SSH keys.

## Phase 0: groundwork

- [x] **0.1 Move third-party downloads out of `thirdparty/`.** The scripts
  clone into `test/thirdparty/work/` (ignored by git) instead, so the root
  `thirdparty/` folder can hold vendored code that is committed.
  - [x] Added
  - [x] Verified: `test/thirdparty/common` cloned zlib into
    `test/thirdparty/work/zlib`, no root `thirdparty/` was created, and
    `git status` showed nothing but the edited files.

- [x] **0.2 Use `https://` URLs in the third-party scripts,** so they run
  anywhere, including CI.
  - [x] Added
  - [x] Verified: on a machine with no SSH key and SSH disabled, every
    script's repository answered `git ls-remote` over HTTPS. (The full
    clones happen in 0.3.)

- [x] **0.3 Record a baseline.** Run every third-party script and write the
  result in the table below. This decides which GNU attributes and features
  matter, from real code instead of guesses.
  - [x] Added: `test/thirdparty/lua.sh` and `zlib.sh`, which the README's
    claims had no script for.
  - [x] Verified: the table is filled in, with the first error for each
    failure.

Baseline: mucc at commit `f6817e9`, run on 2026-09-24 in an Ubuntu 24.04
container (the system CI uses) with each project's build dependencies
installed, as a normal user.

| Project | Builds | Own tests pass | First failure |
| --- | --- | --- | --- |
| SQLite 3.34.0 (230,944 lines) | Yes | Yes: 249,451 tests, 0 errors; 45,957 fuzz cases, 0 errors | None. (`configure` runs `mucc -dumpmachine`, which was rejected; harmless here, fixed in 0.6.) |
| Lua 5.4.7 | No | Yes, after 0.4: the full suite ends "final OK" | `unknown argument: -march=native` (fixed in 0.4) |
| zlib 1.3.1 | Static library only | Yes, after 0.4: static and shared tests pass | `unknown file extension: adler32.lo` (fixed in 0.4) |
| libpng | Yes | Yes | None |
| Git | No; yes after 0.5 to 0.8 and 1.9 | Yes, after 1.9: 21,334 tests pass, 0 fail, 232 known breakages, the same as gcc (2026-09-24, in WSL without the optional OpenSSL, curl, expat, gettext and Tcl/Tk) | `<regex.h>`: `undeclared identifier '__nmatch'`, a parameter used in a later parameter's size (fixed in 0.5) |
| CPython 3.10 dev (about 450,000 lines of C) | Yes, except the `_decimal` module | 387 of 396 suites. gcc passes 391 on the same machine; the 5 it also fails are environment problems (OpenSSL 3 against old code). | `_decimal`: `asm` with operands (2.3). `test_epoll`, `test_selectors`: glibc strips `packed` for mucc, so `struct epoll_event` has the wrong layout (1.6). `test_distutils`, `test_peg_generator`: not yet diagnosed (1.12). |
| TinyCC | Yes | Yes | None |

- [x] **0.4 Driver fixes the baseline found.** Accept and ignore `-march=`
  and `-mtune=` (Lua's makefile passes `-march=native`; mucc only emits
  baseline x86-64, which every x86-64 CPU runs). Pass files with unknown
  extensions to the linker as objects, as gcc does (zlib's libtool-style
  `.lo` files), while sources mucc can't compile (`.S`, C++, headers) get a
  clear error.
  - [x] Added
  - [x] Verified: new `test/driver.sh` cases fail with the old mucc and pass
    with the new one (including `.lo` with `-static`); `make test-all` and
    `make difftest N=300` pass.

- [x] **0.5 Parameters in scope for later parameters (C99).** The baseline
  found that `void f(int n, int a[n])` failed with "undeclared identifier",
  which broke every program using glibc's `<regex.h>` (git, for one).
  Parameters now have their own scope; a definition's size expressions
  refer to the real parameters, and `int m[r][c]` gets its row size on
  entry. Also: `int a[n]` parameters become pointers like fixed arrays do,
  `[*]` is accepted in prototypes, and `const`/`volatile` inside `[]`.
  - [x] Added
  - [x] Verified: new cases in `test/vla.c` (indexing, `sizeof`, pointer
    arithmetic on `int m[r][c]` parameters) and a `regexec` call in
    `test/stdhdr.c`, all failing to compile with the old mucc; `make
    test-all` and `make difftest N=300` pass.

- [x] **0.6 `-dumpmachine`.** Prints `x86_64-linux-gnu`, as gcc does.
  SQLite's `configure` (through `dpkg-architecture`) asks for it.
  - [x] Added
  - [x] Verified: new `test/driver.sh` case; the old mucc rejected the flag;
    `make test-all` and `make difftest N=300` pass.

- [x] **0.7 Two regressions from 0.5, and two older bugs they uncovered.**
  Re-running git after 0.5 showed "cannot convert `struct archiver *` to
  `struct archiver *`":
  - (Regression) A struct's tag was only registered after its members, so
    `struct S { int (*f)(struct S *); };` declared a second, separate
    `struct S` inside the parameter list. Before 0.5 this happened to work.
    The tag is now in scope from the `{`, as C requires.
  - (Regression) Unused parameters got "unused variable" warnings.
  - (Older) Every `-I` directory counted as a system header directory, so
    type errors and warnings in those headers were hidden, and `-MMD` left
    them out of `.d` files. This is also why the test suite, built with
    `-Itest`, didn't catch the first regression.
  - (Older) `-MD`/`-MMD` with `-c -o obj/x.o` wrote `x.d` in the current
    directory instead of `obj/x.d` as gcc does.
  - [x] Added
  - [x] Verified: new `test/errors.sh` and `test/driver.sh` cases each fail
    with the 0.5 commit (`542e4c0`) and pass now; `make test-all` and `make
    difftest N=300` pass; git builds and passes its tests (see the
    baseline table).

- [x] **0.8 glibc's large-file renames in arguments.** For compilers other
  than gcc, glibc renames functions with macros (`#define getrlimit
  getrlimit64`), so `getrlimit(RLIMIT_NOFILE, &r)` passes a `struct rlimit *`
  where `struct rlimit64 *` is declared. The two have the same layout. A
  `struct X` and a `struct X64` of the same size now count as compatible
  pointees. Found by git (`packfile.c`).
  - [x] Added
  - [x] Verified: a `test/errors.sh` case with `getrlimit` fails on the
    previous commit (`b0809b7`) and passes now, and a different-sized `struct s` and
    `struct s64` are still an error; `make test-all` and `make difftest
    N=300` pass; git builds and passes its tests (see the baseline table).

## Where we left off (2026-09-24, day)

`aligned`, `packed` and `weak` (part of 1.3) and 1.6 are committed
together: 1.3's layout test needs 1.6 (without it, glibc strips the test's
attributes), and 1.6 needs 1.3 (glibc's own headers use `aligned` on
members). Then the rest of 1.3, 1.4, 1.5 and 1.9 (C17 by default).
Git now builds with mucc and passes its tests, the same as gcc, which
verified 0.7, 0.8 and 1.9. Phase 0 is done.

Left in Phase 1: 1.6 (verify with CPython's `test_epoll` and
`test_selectors`), 1.7 (added; verified once CI passes), 1.11 (re-run the baseline),
1.12 (CPython's `test_distutils` and `test_peg_generator`) and 1.13
(locals aligned above 16). 1.8 and 1.10 were dropped as not needed.

glibc headers, re-checked: of the 123 in `/usr/include/*.h` that gcc
compiles alone, mucc compiles all but `<complex.h>` and `<tgmath.h>`
(`_Complex`, not planned) and `<link.h>` (`mode`, a clear error).

The first test run found two bugs, both fixed:

- `#include_next` continued from wherever the last uncached header search
  had stopped, so `include/sys/cdefs.h` found itself again and recursed
  until mucc ran out of memory. That is what crashed WSL
  (`Wsl/Service/E_UNEXPECTED`) last night, not WSL itself. Fixed in its own
  commit (`9f1585a`).
- A default include directory also given with `-I` (the Makefile's
  `-Iinclude`) was searched twice, so the wrapper was read twice, and the
  extra path in the debug info depended on where the mucc binary was:
  stage 2 and stage 3 differed. Such a directory is now searched once.

Found while working on these, to keep in mind:

- Run the tests from a copy on WSL's Linux filesystem, not `/mnt/c`: it
  is much faster, and a runaway there can't stall on the Windows drive.
  Use `ulimit -v` so a runaway fails instead of taking the WSL VM down.
- The glibc stripping (1.6) was worse than the epoll bug: it also removed
  `__attribute__` from the user's own code after any `#include <stdio.h>`,
  so every attribute in a real program was silently dropped. Before 1.6,
  only `[[gnu::...]]` attributes had any effect.
- See 1.13: locals aligned above 16 are misaligned (an older bug).

## Phase 1: GNU attributes and the small C23 features

The biggest step toward compiling C found in the wild. The rule for every
attribute: ignore it only when ignoring it can't change what the program
does; otherwise implement it; otherwise stop with a clear error. mucc never
silently produces wrong code.

- [x] **1.1 A general attribute parser.** Read `__attribute__((a, b(x, y)))`
  wherever gcc accepts it: before and after declarations, on parameters,
  struct members, typedefs, labels and empty statements. Both spellings
  (`packed` and `__packed__`). C23 `[[gnu::name]]` goes through the same
  rules instead of being dropped; standard `[[...]]` attributes that are
  only hints (`nodiscard`, `maybe_unused`, `deprecated`, `fallthrough`)
  stay ignored. An unknown `__attribute__` or `[[gnu::...]]` is an error
  naming it; other unknown `[[...]]` attributes get a warning and are
  ignored, as C23 requires.
  - [x] Added: `attributes()` in `src/parser.c`, called from declaration
    specifiers, declarators (before and after pointers, after the name,
    after the whole declarator), bit-fields, enums and enumerators, labels
    and statements. `[[gnu::x(args)]]` is rewritten to
    `__attribute__((x(args)))`, and `[[maybe_unused]]` to
    `__attribute__((unused))`, before parsing.
  - [x] Verified: tests in `test/attribute.c` for each position and both
    syntaxes, and `test/errors.sh` cases for unknown and unsupported
    attributes (errors) and unknown `[[...]]` ones (warnings); the previous
    commit (`4b9a6a6`) fails them; `make test-all` and `make difftest
    N=300` pass; zlib and Lua still pass their tests.

- [x] **1.2 Attributes that are safe to ignore.** `unused`, `maybe_unused`,
  `format`, `format_arg`, `nonnull`, `returns_nonnull`, `sentinel`,
  `warn_unused_result`, `deprecated`, `unavailable`, `pure`, `const`,
  `malloc`, `alloc_size`, `alloc_align`, `nothrow`, `leaf`, `noinline`,
  `noclone`, `always_inline`, `hot`, `cold`, `artificial`, `flatten`,
  `may_alias`, `fallthrough`, `no_sanitize*`, `returns_twice`. Each is
  checked to be truly harmless for mucc before it goes on the list.
  - [x] Added: 66 attributes in `ignored_attributes` (`src/parser.c`),
    the ones above plus hints like `noipa`, `optimize`, `access`,
    `counted_by`, `tls_model`, `assume` and `musttail`. `unused` also turns
    off the unused-variable warning, and `noreturn` works like
    `_Noreturn`. `returns_twice` was checked against how mucc keeps
    variables in registers: after `longjmp`, only variables changed since
    `setjmp` can differ, which C already leaves unspecified.
  - [x] Verified: `attr_all()` in `test/attribute.c` carries all 66 and
    still returns the right value (a script checked that none is missing).

- [x] **1.3 Attributes that change behavior, implemented:**
  - `noreturn`, the same as `_Noreturn`
  - `aligned(N)` on variables, members and typedefs, not just structs
  - `packed` on members and enums
  - `used`: keep the function or variable even if nothing refers to it
  - `weak`, and `alias("name")`
  - `section("name")`
  - `visibility("default" | "hidden" | "protected" | "internal")`
  - `constructor` and `destructor`, with optional priority (`.init_array`,
    `.fini_array`), kept alive like `used`
  - `cleanup(fn)`: call `fn(&var)` when the variable goes out of scope,
    including through `break`, `continue`, `return` and `goto`
  - `gnu_inline`, with gcc's `extern inline` meaning
  - [x] Added: `aligned(N)` (on variables, members and typedefs; an
    error on a local above 16, see 1.13), `packed` on members and enums,
    and `weak` on functions and global variables.
    `test/attribute-layout.sh` checks 24 lines of layouts and addresses
    against gcc; `test/driver.sh` and `test/errors.sh` cover `weak` and
    misuse. Also `used`, and
    `constructor`/`destructor` with priorities: mucc's linker sorts
    `.init_array.N` and `.fini_array.N` first, as `ld` does.
    `test/constructor.c` checks the order gcc runs them in, with `ld` and
    with `-static`. And `cleanup(fn)`, at the end of a block and through
    `break`, `continue`, `return` and `goto` (`test/cleanup.c`, checked
    against gcc). Jumping into a cleanup variable's scope (`goto`, `case`)
    is an error, as are `goto *` in one and one at the end of a statement
    expression. Then `alias`, `section`, `visibility` and `gnu_inline`
    (`test/symbol-attrs.c`, and the symbol table in `test/driver.sh`).
    mucc's assembler learned `.set`, `.hidden`, `.protected` and
    `.internal`, and writes a line table sequence per code section, as
    GNU as does. mucc's linker keeps a section named like a C identifier
    as its own and defines `__start_name` and `__stop_name`, as `ld`
    does; glibc's `__libc_atexit` and similar sections now get these too.
  - [x] Verified: a runtime test per attribute, checked against gcc's
    behavior, and each one works with the built-in linker (`-static`) and
    with `ld`. CI passed (`a31b3a8`).

- [x] **1.4 Clear errors for the rest.** `vector_size`, `mode`, `ifunc`,
  `naked`, `target` and anything not in 1.2 or 1.3 stop with "attribute X is
  not supported".
  - [x] Added: the gcc attributes mucc doesn't implement
    (`unsupported_attributes` in `src/parser.c`) say "not supported";
    names gcc doesn't have either say "unknown attribute".
  - [x] Verified: `test/errors.sh` cases: one per unsupported attribute,
    in the `__x__` spelling.

- [x] **1.5 `__has_attribute(x)` and `__has_builtin(x)`.** 1 for what mucc
  supports, 0 otherwise, so code that checks first picks its fallback.
  - [x] Added: in `#if`, also when a macro produces them, as glibc's
    `__glibc_has_attribute` does. glibc's headers now take their
    attribute paths too; the header check (below) and zlib and Lua still
    pass.
  - [x] Verified: a test for supported, ignored and unsupported names
    (`test/attribute.c`).

- [ ] **1.6 Keep glibc's attributes.** glibc's `<sys/cdefs.h>` defines
  `__attribute__(x)` as nothing for any compiler that isn't gcc, clang or
  tcc, so today mucc never sees glibc's `packed` and `aligned`. Found by
  the baseline: under mucc, `struct epoll_event` is 16 bytes with its data
  at offset 8; the kernel and gcc use 12 and 4, so every epoll program
  built by mucc reads the wrong data. Once 1.1 to 1.4 are verified, undo
  that `#define` (for example, a `sys/cdefs.h` in mucc's `include/` that
  includes glibc's and then `#undef __attribute__`).
  - [x] Added: `include/sys/cdefs.h`, installed by `make install`. A
    default include directory also given with `-I` is now searched once,
    so the wrapper is read once.
  - [ ] Verified: a new test compares `sizeof`, `_Alignof` and member
    offsets of glibc's structs between gcc and mucc (starting with
    `struct epoll_event`: 12 and 4), and CPython's `test_epoll` and
    `test_selectors` pass.

- [ ] **1.7 C23 `enum E : type`.** A fixed underlying type, which sets the
  size, signedness and range checks of the enum.
  - [x] Added: also `enum E : type;` with no list, and the constants take
    the enum's type (so `sizeof` of one can be 1 or 8). `struct { enum E :
    3; }` is still a bit-field. Available in every mode, as with gcc.
  - [ ] Verified: tests in `test/c23.c` for size, sign, out-of-range errors
    (and `test/errors.sh`); the same results as gcc 15 for all of them.

- **1.8 Dropped** (see "Not planned"): `<stdckdint.h>` and the
  `__builtin_*_overflow` builtins.

- [x] **1.9 C23 `int f()` means `int f(void)`.** Keep the old meaning under
  `-std=c89` through `-std=c17`, since old code depends on it. `-std=`,
  ignored today, starts to count for this rule. The same goes for C23's new
  keywords: git names a struct `thread_local`, which is a keyword in C23
  (mucc's default) but not in C17 (the default of gcc 13, which git is
  tested with). Decide whether mucc's default should stay C23, and make
  `-std=c17` and earlier treat those words as ordinary names.
  - [x] Added: decided to make C17 the default, as gcc 14 and clang do,
    since the goal is compiling existing code; `-std=c23` (or `c2x`,
    `gnu23`) gives C23. `-std=` and `-ansi` set `__STDC_VERSION__`
    (none for C89). Before C23, `true`, `false`, `nullptr`, `constexpr`,
    `bool`, `alignas`, `alignof`, `static_assert`, `thread_local` and
    `typeof_unqual` are ordinary names (`<stdbool.h>` defines `bool`,
    `true` and `false`), and `int f()` accepts any arguments. `typeof`,
    `[[...]]` attributes, `auto` type inference and the rest of C23 stay
    available in every mode, as GNU extensions. A test program can ask
    for options with a `// flags:` line (`test/c23.c` uses `-std=c23`).
  - [x] Verified: tests for both modes (`test/c17.c`, `test/c23.c`,
    `test/driver.sh`, `test/errors.sh`), and the third-party baseline is
    no worse: the glibc headers, zlib and Lua pass as before, and git now
    builds and passes its tests. (gcc 15, which defaults to C23, fails on
    git the way mucc used to; it needs `-std=gnu17`.)

- **1.10 Dropped** (see "Not planned"): errors for writing to `const`.

- [ ] **1.11 Re-run the baseline.** Update the Phase 0 table and the README's
  lists of what mucc does and doesn't do.
  - [ ] Added
  - [ ] Verified: table and README match the results.

- [ ] **1.13 Locals aligned above 16 bytes.** Locals are placed relative to
  `%rbp`, which is only 16-byte aligned, so `_Alignas(32)` on a local, or
  a local whose struct type is `aligned(32)`, can be misaligned without
  any error. (Found while doing `aligned` in 1.3, which makes
  `__attribute__((aligned(32)))` on a local an error for now.) Fix by
  realigning the frame, or putting such locals in an aligned area, then
  lift that error.
  - [ ] Added
  - [ ] Verified: a test checks the address of 32- and 64-byte aligned
    locals of each kind, compared with gcc.

- [ ] **1.12 Diagnose CPython's `test_distutils` and `test_peg_generator`.**
  Both pass with gcc and fail with mucc, and both compile C during the
  test. Find the cause, and fix it or add it to this plan.
  - [ ] Added
  - [ ] Verified: both pass, or the cause is written here with its own item.

## Phase 2: prepare for a bundled C library

- [ ] **2.1 Keep "no libgcc" true.** A test that compiles every test program
  and mucc itself and fails if any object needs a symbol from `libgcc`.
  - [ ] Added
  - [ ] Verified: the test runs in `make test` and passes; adding a call to a
    `libgcc` helper makes it fail.

- [ ] **2.2 One place for system paths.** Move the hard-coded paths in
  `src/main.c` into one table describing a C library: where its headers,
  startup files and libraries are, and what else to link. Today's behavior
  becomes `--libc=system`.
  - [ ] Added
  - [ ] Verified: no behavior change; `make test-all` passes and the output
    of `test/driver.sh` is identical.

- [ ] **2.3 GNU `asm` with operands.** Constraints `r`, `a`, `b`, `c`, `d`,
  `S`, `D`, `m`, `i`, `n`, matching digits, the modifiers `=`, `+` and `&`,
  the clobbers `memory`, `cc` and named registers, and `volatile`. musl's
  system calls need it, and so does a lot of real code. `asm goto` stays out.
  - [ ] Added
  - [ ] Verified: `test/asm-operands.c` covers each constraint, including a
    raw `write` system call, with results compared against gcc.

- [ ] **2.4 An archiver.** `mucc -ar rcs lib.a a.o b.o` writes a standard
  `ar` archive that mucc's linker, `ld` and `nm` all read.
  - [ ] Added
  - [ ] Verified: an archive made by mucc links the same as one made by GNU
    `ar`, and `ar t` lists it.

- [ ] **2.5 A cheap way to embed large files.** Either make `#embed` use
  about as much memory as the data itself, or add `.incbin` to the
  assembler. Needed to put a C library inside the binary.
  - [ ] Added
  - [ ] Verified: embedding a 10 MB file uses under 50 MB of memory, and the
    bytes come out exactly right.

## Phase 3: bundle musl

musl is a small, MIT-licensed C library built for static linking. It goes in
`thirdparty/musl/`, unchanged except for patches listed in
`thirdparty/README.md`.

- [ ] **3.1 Vendor musl.** Add a pinned musl release to `thirdparty/musl/`
  with its license. `thirdparty/README.md` records the version, where it came
  from, its checksum, and any local patches.
  - [ ] Added
  - [ ] Verified: the files match the release tarball's published checksum.

- [ ] **3.2 Everything musl's x86-64 code needs is in mucc's assembler,** so
  the system `as` is never used. Add a flag that makes falling back to `as`
  an error, to prove it.
  - [ ] Added
  - [ ] Verified: musl builds with the flag on and no `as` on the `PATH`.

- [ ] **3.3 Build musl with mucc.** `make libc` compiles musl with mucc and
  archives it with `mucc -ar` into `libc.a`, with musl's `crt1.o`, `crti.o`
  and `crtn.o`.
  - [ ] Added
  - [ ] Verified: builds with no gcc, `as`, `ld` or `ar` used (checked by
    running the build with them removed from the `PATH`).

- [ ] **3.4 musl's own tests.** Run the `libc-test` suite against the musl
  that mucc built, from a script in `test/thirdparty/`.
  - [ ] Added
  - [ ] Verified: its pass count matches a gcc-built musl's, and any
    difference is explained in `thirdparty/README.md`.

- [ ] **3.5 `--libc=mucc`.** Uses musl's headers (after mucc's own
  `include/`), musl's startup files and `libc.a`, and always links
  statically with mucc's own linker.
  - [ ] Added
  - [ ] Verified: the whole test suite passes with `make test LIBC=mucc`.

- [ ] **3.6 mucc builds itself on musl.** Stage 2 and stage 3 built against
  musl, byte-identical as today.
  - [ ] Added
  - [ ] Verified: `make test-all LIBC=mucc` passes.

- [ ] **3.7 CI runs both modes.** A second CI job for `LIBC=mucc`.
  - [ ] Added
  - [ ] Verified: both jobs pass on GitHub.

## Phase 4: one binary

- [ ] **4.1 Embed the C library.** mucc's own headers, musl's headers,
  `libc.a` and the startup files go inside the `mucc` binary (using 2.5).
  The preprocessor and linker read them from memory, with no files written
  to disk.
  - [ ] Added
  - [ ] Verified: with no `include/` or library files next to it, mucc still
    compiles and links a program.

- [ ] **4.2 mucc is itself a static musl program,** so it runs on any
  x86-64 Linux, including Alpine and systems without glibc.
  - [ ] Added
  - [ ] Verified: the binary runs on Ubuntu, Fedora, Alpine and one old
    distribution, and `file mucc` says statically linked.

- [ ] **4.3 Bundled libc is the default.** `mucc hello.c` uses the bundled
  musl; `--libc=system` uses glibc and system libraries as today.
  - [ ] Added
  - [ ] Verified: tests for both modes, and a clear error when a system-only
    library like `-lssl` is used without `--libc=system`.

- [ ] **4.4 Releases.** Pushing a version tag makes CI build the single
  binary and attach it to a GitHub Release, with its size and checksum.
  - [ ] Added
  - [ ] Verified: the released binary, downloaded onto a clean machine,
    passes the "Done means" checks at the top of this file.

- [ ] **4.5 Documentation.** README, website and statistics describe the
  single binary, how to get it, and both modes.
  - [ ] Added
  - [ ] Verified: every claim in the docs was checked against the release.

## Not planned

These stay out unless a real program needs them:

- `_Complex` and `_BitInt`: large, and rare in real code. musl's
  `src/complex/` is left out of the bundled libc (Phase 3).
- `<stdckdint.h>` and `__builtin_add_overflow` and the like (was 1.8):
  none of the baseline projects need them. Code that checks with
  `__has_builtin` or `__GNUC__` (which mucc doesn't define) uses its own
  fallback.
- Errors for writing to `const` (was 1.10, a stretch goal): a check, not
  something real code needs, and it would touch much of the type checker.
- Dynamic linking in mucc's own linker: `--libc=system` uses `ld` for that.
- Building mucc from source without `make`: users get the binary; building
  from source may still use `make`.
- Other processors (ARM, 32-bit x86), other systems (macOS, Windows) and C++.
