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
| Git | No | Not reached | `<regex.h>`: `undeclared identifier '__nmatch'`, a parameter used in a later parameter's size (fixed in 0.5) |
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

- [ ] **0.7 Two regressions from 0.5, and two older bugs they uncovered.**
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
  - [ ] Verified: new `test/errors.sh` and `test/driver.sh` cases each fail
    with the 0.5 commit (`542e4c0`) and pass now; `make test-all` and `make
    difftest N=300` pass; git builds and passes its tests.

## Phase 1: GNU attributes and the small C23 features

The biggest step toward compiling C found in the wild. The rule for every
attribute: ignore it only when ignoring it can't change what the program
does; otherwise implement it; otherwise stop with a clear error. mucc never
silently produces wrong code.

- [ ] **1.1 A general attribute parser.** Read `__attribute__((a, b(x, y)))`
  wherever gcc accepts it: before and after declarations, on parameters,
  struct members, typedefs, labels and empty statements. Both spellings
  (`packed` and `__packed__`). C23 `[[gnu::name]]` goes through the same
  rules instead of being dropped; standard `[[...]]` attributes that are
  only hints (`nodiscard`, `maybe_unused`, `deprecated`, `fallthrough`)
  stay ignored. An unknown `__attribute__` or `[[gnu::...]]` is an error
  naming it; other unknown `[[...]]` attributes get a warning and are
  ignored, as C23 requires.
  - [ ] Added
  - [ ] Verified: tests in `test/attribute.c` for each position and both
    syntaxes, and an unknown attribute gives a clear error in
    `test/errors.sh`.

- [ ] **1.2 Attributes that are safe to ignore.** `unused`, `maybe_unused`,
  `format`, `format_arg`, `nonnull`, `returns_nonnull`, `sentinel`,
  `warn_unused_result`, `deprecated`, `unavailable`, `pure`, `const`,
  `malloc`, `alloc_size`, `alloc_align`, `nothrow`, `leaf`, `noinline`,
  `noclone`, `always_inline`, `hot`, `cold`, `artificial`, `flatten`,
  `may_alias`, `fallthrough`, `no_sanitize*`, `returns_twice`. Each is
  checked to be truly harmless for mucc before it goes on the list.
  - [ ] Added
  - [ ] Verified: a test uses every one and still behaves correctly.

- [ ] **1.3 Attributes that change behavior, implemented:**
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
  - [ ] Added
  - [ ] Verified: a runtime test per attribute, checked against gcc's
    behavior, and each one works with the built-in linker (`-static`) and
    with `ld`.

- [ ] **1.4 Clear errors for the rest.** `vector_size`, `mode`, `ifunc`,
  `naked`, `target` and anything not in 1.2 or 1.3 stop with "attribute X is
  not supported".
  - [ ] Added
  - [ ] Verified: `test/errors.sh` cases.

- [ ] **1.5 `__has_attribute(x)` and `__has_builtin(x)`.** 1 for what mucc
  supports, 0 otherwise, so code that checks first picks its fallback.
  - [ ] Added
  - [ ] Verified: a test for supported, ignored and unsupported names.

- [ ] **1.6 Keep glibc's attributes.** glibc's `<sys/cdefs.h>` defines
  `__attribute__(x)` as nothing for any compiler that isn't gcc, clang or
  tcc, so today mucc never sees glibc's `packed` and `aligned`. Found by
  the baseline: under mucc, `struct epoll_event` is 16 bytes with its data
  at offset 8; the kernel and gcc use 12 and 4, so every epoll program
  built by mucc reads the wrong data. Once 1.1 to 1.4 are verified, undo
  that `#define` (for example, a `sys/cdefs.h` in mucc's `include/` that
  includes glibc's and then `#undef __attribute__`).
  - [ ] Added
  - [ ] Verified: a new test compares `sizeof`, `_Alignof` and member
    offsets of glibc's structs between gcc and mucc (starting with
    `struct epoll_event`: 12 and 4), and CPython's `test_epoll` and
    `test_selectors` pass.

- [ ] **1.7 C23 `enum E : type`.** A fixed underlying type, which sets the
  size, signedness and range checks of the enum.
  - [ ] Added
  - [ ] Verified: tests in `test/c23.c` for size, sign, out-of-range errors.

- [ ] **1.8 `<stdckdint.h>`.** `ckd_add`, `ckd_sub` and `ckd_mul`, and gcc's
  `__builtin_add_overflow`, `__builtin_sub_overflow` and
  `__builtin_mul_overflow`, which share the same code.
  - [ ] Added
  - [ ] Verified: tests at the limits of every integer type, with results
    compared against gcc.

- [ ] **1.9 C23 `int f()` means `int f(void)`.** Keep the old meaning under
  `-std=c89` through `-std=c17`, since old code depends on it. `-std=`,
  ignored today, starts to count for this rule.
  - [ ] Added
  - [ ] Verified: tests for both modes, and the third-party baseline is no
    worse.

- [ ] **1.10 (Stretch) Errors for writing to `const`.** Assigning to a `const`
  object or through a pointer to `const` is an error, as the standard
  requires.
  - [ ] Added
  - [ ] Verified: `test/errors.sh` cases, and all tests and third-party
    builds still compile.

- [ ] **1.11 Re-run the baseline.** Update the Phase 0 table and the README's
  lists of what mucc does and doesn't do.
  - [ ] Added
  - [ ] Verified: table and README match the results.

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

- `_Complex` and `_BitInt`: large, and rare in real code.
- Dynamic linking in mucc's own linker: `--libc=system` uses `ld` for that.
- Building mucc from source without `make`: users get the binary; building
  from source may still use `make`.
- Other processors (ARM, 32-bit x86), other systems (macOS, Windows) and C++.
