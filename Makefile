# -O2 makes the installed mucc about 40% faster than an unoptimized build.
CFLAGS=-std=c11 -O2 -g -fno-common -Wall -Wno-switch

# All compiler source is in src/. OBJNAMES is the bare object names
# (main.o, ...), shared by the stage 1, 2 and 3 builds.
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
OBJNAMES=$(notdir $(OBJS))

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

# A test program can ask for options with a `// flags: ...` line, like
# test/c23.c's `-std=c23`. test/link.sh and test/asm.sh read it too.
test_flags=$(shell sed -n 's|^// flags: ||p' test/$(1).c)

# `make install` puts mucc in $(PREFIX)/bin and its headers in
# $(PREFIX)/lib/mucc/include, where mucc looks for them.
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

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./mucc
	test/errors.sh ./mucc
	test/asm.sh ./mucc
	test/link.sh ./mucc
	test/attribute-layout.sh ./mucc

test-all: test test-stage2 selfhost

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

# Install

install: mucc
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/mucc/include
	install -m 755 mucc $(DESTDIR)$(PREFIX)/bin/mucc
	install -m 644 include/*.h $(DESTDIR)$(PREFIX)/lib/mucc/include
	install -d $(DESTDIR)$(PREFIX)/lib/mucc/include/sys
	install -m 644 include/sys/*.h $(DESTDIR)$(PREFIX)/lib/mucc/include/sys

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mucc
	rm -rf $(DESTDIR)$(PREFIX)/lib/mucc

# Misc.

# test/asm-forms.s is a source file (see test/asm.sh); other .s files in
# test/ are build output.
clean:
	rm -rf mucc tmp* $(TESTS) test/*.exe stage2 stage3 difftest-failures
	rm -f $(filter-out test/asm-forms.s,$(wildcard test/*.s))
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean test-stage2 selfhost install uninstall difftest
