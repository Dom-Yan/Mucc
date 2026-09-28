# -O2 makes the installed mucc about 40% faster than an unoptimized build.
CFLAGS=-std=c11 -O2 -g -fno-common -Wall -Wno-switch

# All compiler source is in src/. OBJNAMES is the bare object names
# (main.o, ...), shared by the stage 1, 2 and 3 builds.
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
OBJNAMES=$(notdir $(OBJS))

TEST_SRCS=$(wildcard test/*.c)

# `make test-all LIBC=mucc` builds the tests, and stages 2 and 3, against
# the bundled musl (see `make libc`), linked by mucc itself, instead of
# glibc. Its files are named apart (test/*.musl.exe, stage2-musl/, ...),
# so the two builds don't mix.
LIBC=system
MUSL_BUILD=build/musl
ifeq ($(LIBC),mucc)
X=.musl
S2=stage2-musl
S3=stage3-musl
COMMON=test/common.musl.o
MUSL_DEP=$(MUSL_BUILD)/lib/libc.a
# $(call link_test,mucc,obj) links a test program, and link_mucc links
# a stage's mucc. It finds musl in $(S2)/build, as mucc finds it next to
# itself.
link_test=$(1) --libc=mucc -o $@ $(2) $(COMMON)
link_mucc=ln -sfn ../build $(S2)/build && ./mucc --libc=mucc -o $@ $^
else
X=
S2=stage2
S3=stage3
COMMON=
MUSL_DEP=
link_test=$(CC) -pthread -o $@ $(2) -xc test/common
link_mucc=$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
endif
TESTS=$(TEST_SRCS:.c=$(X).exe)

# A test program can ask for options with a `// flags: ...` line, like
# test/c23.c's `-std=c23`. test/link.sh and test/asm.sh read it too.
test_flags=$(shell sed -n 's|^// flags: ||p' test/$(1).c)

# `make install` puts mucc in $(PREFIX)/bin and its headers in
# $(PREFIX)/lib/mucc/include, where mucc looks for them, and musl, if
# `make libc` built it, in $(PREFIX)/lib/mucc/musl.
PREFIX=/usr/local

# Stage 1 (mucc compiled by $(CC), normally gcc)
#
# This is the bootstrap step and always uses the system C compiler, so a
# broken mucc can be fixed in the source and rebuilt. `make` runs only this
# stage.

mucc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): src/mucc.h

test/%$(X).exe: mucc test/%.c $(COMMON)
	./mucc --libc=$(LIBC) -Iinclude -Itest $(call test_flags,$*) -c -o test/$*$(X).o test/$*.c
	$(call link_test,./mucc,test/$*$(X).o)

test/common.musl.o: mucc test/common $(MUSL_BUILD)/lib/libc.a
	./mucc --libc=mucc -c -o $@ -xc test/common

# The scripts get the mucc to test as one argument, `$(1) --libc=...`.
test_scripts=\
	test/driver.sh "$(1) --libc=$(LIBC)" && \
	test/errors.sh "$(1) --libc=$(LIBC)" && \
	test/asm.sh "$(1) --libc=$(LIBC)" && \
	test/link.sh "$(1) --libc=$(LIBC)" && \
	test/attribute-layout.sh "$(1) --libc=$(LIBC)" && \
	test/libgcc.sh "$(1) --libc=$(LIBC)" && \
	test/ar.sh $(1)

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	$(call test_scripts,./mucc)

test-all: test test-stage2 selfhost test-libc test-single

# Differential testing: random programs compiled by mucc and gcc must print
# the same thing. `make difftest N=2000` for more. See test/difftest.sh.
N=300
difftest: mucc
	test/difftest.sh $(N)

# Stage 2 (mucc compiled by mucc)
#
# Every stage 2 object depends on ./mucc, so stage 1 always builds first.
# mucc's own source may only use C features that mucc supports, or this
# stage fails to build.

$(S2)/mucc: $(OBJNAMES:%=$(S2)/%)
	$(link_mucc)

$(S2)/%.o: mucc src/%.c src/mucc.h $(MUSL_DEP)
	mkdir -p $(S2)/test
	./mucc --libc=$(LIBC) -Iinclude -c -o $@ src/$*.c

$(S2)/test/%$(X).exe: $(S2)/mucc test/%.c
	mkdir -p $(S2)/test
	./$(S2)/mucc --libc=$(LIBC) -Iinclude -Itest $(call test_flags,$*) -c -o $(S2)/test/$*.o test/$*.c
	$(call link_test,./$(S2)/mucc,$(S2)/test/$*.o)

test-stage2: $(TESTS:test/%=$(S2)/test/%)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	$(call test_scripts,./$(S2)/mucc)

# Stage 3 (mucc compiled by the mucc that was compiled by mucc)
#
# If the compiler is truly self-hosting, the stage 2 compiler must produce
# byte-for-byte the same object files as the stage 1 compiler did.

$(S3)/%.o: $(S2)/mucc src/%.c src/mucc.h
	mkdir -p $(S3)
	./$(S2)/mucc --libc=$(LIBC) -Iinclude -c -o $@ src/$*.c

selfhost: $(OBJNAMES:%=$(S3)/%) $(OBJNAMES:%=$(S2)/%)
	for i in $(OBJNAMES); do cmp $(S2)/$$i $(S3)/$$i || exit 1; done
	@echo "selfhost: $(S2) and $(S3) objects are identical"

# The bundled C library (Phase 3 of PLAN.md)
#
# musl, from thirdparty/musl, built by mucc with musl's own configure and
# Makefile in build/musl, archived by `mucc -ar`, and with no `as`
# (-fno-as-fallback). Left out: musl's complex numbers and its x86-64
# math overrides (x87 assembly and SSE asm operands, which mucc doesn't
# have); musl's portable C math is built instead. It's built from scratch
# whenever mucc changes, since musl's Makefile doesn't know its objects
# depend on the compiler. MUSL_BUILD is set at the top. Its headers go in
# $(MUSL_BUILD)/include and the rest in $(MUSL_BUILD)/lib, where
# --libc=mucc finds them, with musl's empty libm.a, libpthread.a, ... so
# -lm and -lpthread work as with glibc.

libc: $(MUSL_BUILD)/lib/libc.a

test-libc: mucc
	test/libc.sh

$(MUSL_BUILD)/lib/libc.a: mucc
	rm -rf $(MUSL_BUILD)
	mkdir -p $(MUSL_BUILD)
	cd $(MUSL_BUILD) && $(CURDIR)/thirdparty/musl/configure --target=x86_64 \
	  --disable-shared CC=$(CURDIR)/mucc > configure.log
	printf '%s\n' \
	  'BASE_SRCS = $$(filter-out $$(srcdir)/src/complex/%,$$(sort $$(wildcard $$(BASE_GLOBS))))' \
	  'ARCH_SRCS = $$(filter-out $$(srcdir)/src/math/x86_64/%,$$(sort $$(wildcard $$(ARCH_GLOBS))))' \
	  >> $(MUSL_BUILD)/config.mak
	$(MAKE) -C $(MUSL_BUILD) CFLAGS=-fno-as-fallback AR="$(CURDIR)/mucc -ar" \
	  RANLIB="$(CURDIR)/mucc -ranlib" lib/libc.a lib/crt1.o lib/crti.o lib/crtn.o \
	  $(patsubst %,lib/lib%.a,m rt pthread crypt util xnet resolv dl)
	$(MAKE) -C $(MUSL_BUILD) install-headers DESTDIR= \
	  includedir=$(CURDIR)/$(MUSL_BUILD)/include > /dev/null

# The single binary (Phase 4 of PLAN.md)
#
# build/mucc is mucc with its own headers, musl's headers and musl's
# libraries and startup files inside it, read from memory, so it needs no
# files next to it. build/files.s lists them for mucc_files[] in
# src/main.c: a {path, data, size} entry each, with the bytes put in by
# .incbin (see src/asm.c), under the path "<mucc>/include/..." or
# "<mucc>/musl/...".

build/files.s: $(MUSL_BUILD)/lib/libc.a $(wildcard include/*.h include/sys/*.h)
	( find include -type f -name '*.h' -printf '%p %s\n' | \
	    sed 's|^\([^ ]*\) .*|& <mucc>/\1|'; \
	  find $(MUSL_BUILD)/include $(MUSL_BUILD)/lib -type f -printf '%p %s\n' | \
	    sed 's|^$(MUSL_BUILD)/\([^ ]*\) .*|& <mucc>/musl/\1|' ) | sort | awk '\
	  BEGIN { for (c = 1; c < 128; c++) ord[sprintf("%c", c)] = c } \
	  { src[NR] = $$1; size[NR] = $$2; path[NR] = $$3 } \
	  END { \
	    print "  .data"; print "  .align 8"; print "  .globl mucc_files"; \
	    print "mucc_files:"; \
	    for (i = 1; i <= NR; i++) \
	      printf "  .quad .Lpath%d\n  .quad .Ldata%d\n  .quad %d\n", i, i, size[i]; \
	    print "  .quad 0\n  .quad 0\n  .quad 0"; print "  .section .rodata"; \
	    for (i = 1; i <= NR; i++) { \
	      printf ".Lpath%d:\n", i; \
	      for (j = 1; j <= length(path[i]); j++) \
	        printf "  .byte %d\n", ord[substr(path[i], j, 1)]; \
	      printf "  .byte 0\n.Ldata%d:\n  .incbin \"%s\"\n", i, src[i]; \
	    } \
	  }' > $@

build/files.o: mucc build/files.s
	./mucc -c -o $@ build/files.s

build/mucc: $(OBJS) build/files.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test-single: build/mucc
	test/single.sh build/mucc

# Install

install: mucc
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/mucc/include
	install -m 755 mucc $(DESTDIR)$(PREFIX)/bin/mucc
	install -m 644 include/*.h $(DESTDIR)$(PREFIX)/lib/mucc/include
	install -d $(DESTDIR)$(PREFIX)/lib/mucc/include/sys
	install -m 644 include/sys/*.h $(DESTDIR)$(PREFIX)/lib/mucc/include/sys
	if [ -f $(MUSL_BUILD)/lib/libc.a ]; then \
	  install -d $(DESTDIR)$(PREFIX)/lib/mucc/musl/lib && \
	  install -m 644 $(MUSL_BUILD)/lib/*.a $(MUSL_BUILD)/lib/*.o \
	    $(DESTDIR)$(PREFIX)/lib/mucc/musl/lib && \
	  cp -R $(MUSL_BUILD)/include $(DESTDIR)$(PREFIX)/lib/mucc/musl/; \
	fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mucc
	rm -rf $(DESTDIR)$(PREFIX)/lib/mucc

# Misc.

# test/asm-forms.s is a source file (see test/asm.sh); other .s files in
# test/ are build output.
clean:
	rm -rf mucc tmp* $(TESTS) test/*.exe stage2 stage3 stage2-musl stage3-musl \
	  difftest-failures build
	rm -f $(filter-out test/asm-forms.s,$(wildcard test/*.s))
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean test-stage2 selfhost install uninstall difftest libc test-libc test-single
