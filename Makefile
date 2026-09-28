# -O2 makes the installed mucc about 40% faster than an unoptimized build.
CFLAGS=-std=c11 -O2 -g -fno-common -Wall -Wno-switch

# All compiler source is in src/. OBJNAMES is the bare object names
# (main.o, ...), shared by the stage 1, 2 and 3 builds.
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
OBJNAMES=$(notdir $(OBJS))

TEST_SRCS=$(wildcard test/*.c)

# `make test LIBC=mucc` builds the tests against the bundled musl (see
# `make libc`), linked by mucc itself, instead of glibc. They're named
# test/*.musl.exe, so the two builds don't mix.
LIBC=system
MUSL_BUILD=build/musl
ifeq ($(LIBC),mucc)
TESTS=$(TEST_SRCS:.c=.musl.exe)
else
TESTS=$(TEST_SRCS:.c=.exe)
endif

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

test/%.exe: mucc test/%.c
	./mucc -Iinclude -Itest $(call test_flags,$*) -c -o test/$*.o test/$*.c
	$(CC) -pthread -o $@ test/$*.o -xc test/common

test/%.musl.exe: mucc test/%.c test/common.musl.o
	./mucc --libc=mucc -Iinclude -Itest $(call test_flags,$*) -c -o test/$*.musl.o test/$*.c
	./mucc --libc=mucc -o $@ test/$*.musl.o test/common.musl.o

test/common.musl.o: mucc test/common $(MUSL_BUILD)/lib/libc.a
	./mucc --libc=mucc -c -o $@ -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh "./mucc --libc=$(LIBC)"
	test/errors.sh "./mucc --libc=$(LIBC)"
	test/asm.sh "./mucc --libc=$(LIBC)"
	test/link.sh "./mucc --libc=$(LIBC)"
	test/attribute-layout.sh "./mucc --libc=$(LIBC)"
	test/libgcc.sh "./mucc --libc=$(LIBC)"
	test/ar.sh ./mucc

test-all: test test-stage2 selfhost test-libc

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

stage2/mucc: $(OBJNAMES:%=stage2/%)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

stage2/%.o: mucc src/%.c src/mucc.h
	mkdir -p stage2/test
	./mucc -Iinclude -c -o $@ src/$*.c

stage2/test/%.exe: stage2/mucc test/%.c
	mkdir -p stage2/test
	./stage2/mucc -Iinclude -Itest $(call test_flags,$*) -c -o stage2/test/$*.o test/$*.c
	$(CC) -pthread -o $@ stage2/test/$*.o -xc test/common

test-stage2: $(TESTS:test/%=stage2/test/%)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/mucc
	test/errors.sh ./stage2/mucc
	test/asm.sh ./stage2/mucc
	test/link.sh ./stage2/mucc
	test/attribute-layout.sh ./stage2/mucc
	test/libgcc.sh ./stage2/mucc
	test/ar.sh ./stage2/mucc

# Stage 3 (mucc compiled by the mucc that was compiled by mucc)
#
# If the compiler is truly self-hosting, the stage 2 compiler must produce
# byte-for-byte the same object files as the stage 1 compiler did.

stage3/%.o: stage2/mucc src/%.c src/mucc.h
	mkdir -p stage3
	./stage2/mucc -Iinclude -c -o $@ src/$*.c

selfhost: $(OBJNAMES:%=stage3/%) $(OBJNAMES:%=stage2/%)
	for i in $(OBJNAMES); do cmp stage2/$$i stage3/$$i || exit 1; done
	@echo "selfhost: stage2 and stage3 objects are identical"

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
	rm -rf mucc tmp* $(TESTS) test/*.exe stage2 stage3 difftest-failures build
	rm -f $(filter-out test/asm-forms.s,$(wildcard test/*.s))
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean test-stage2 selfhost install uninstall difftest libc test-libc
