#!/bin/bash
# musl's own tests (libc-test) against the musl that mucc builds (make
# libc), and against the same musl built by gcc, with the same options
# and the same parts left out. The tests themselves are built by gcc
# both times, so a difference comes from how musl was compiled. Prints
# each side's failures and exits 1 if they differ.
repo='https://repo.or.cz/libc-test.git'
. test/thirdparty/common
git reset --hard 7b95dfa5f5d5ca4d949221e0228ccc290bacc14e
top=$(cd ../../../.. && pwd)
work=$top/test/thirdparty/work

# musl built by mucc
(cd $top && make libc)
musl_mucc=$top/build/musl

# musl built by gcc, as `make libc` builds it (see the Makefile)
musl_gcc=$work/musl-gcc
rm -rf $musl_gcc
mkdir -p $musl_gcc
(cd $musl_gcc && $top/thirdparty/musl/configure --target=x86_64 --disable-shared CC=gcc > configure.log)
printf '%s\n' \
  'BASE_SRCS = $(filter-out $(srcdir)/src/complex/%,$(sort $(wildcard $(BASE_GLOBS))))' \
  'ARCH_SRCS = $(filter-out $(srcdir)/src/math/x86_64/%,$(sort $(wildcard $(ARCH_GLOBS))))' \
  >> $musl_gcc/config.mak
$make -C $musl_gcc AR=ar RANLIB=ranlib lib/libc.a lib/crt1.o lib/crti.o lib/crtn.o > $musl_gcc/make.log

# libc-test's settings, less the ones for glibc. Everything is in libc.a.
cat > config.mak <<'EOF'
CFLAGS += -pipe -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wno-unused-function -Wno-missing-braces -Wno-unused -Wno-overflow
CFLAGS += -Wno-unknown-pragmas -fno-builtin -frounding-math
CFLAGS += -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith
EOF

# Runs the tests against the musl in $2, into out-$1. gcc uses that musl
# through a specs file, like musl's musl-gcc wrapper but always static.
run() {
    local name=$1 musl=$2
    make -s -C $musl install-headers DESTDIR= includedir=$musl/include > /dev/null
    cat > $musl/static.specs <<EOF
%rename cpp_options old_cpp_options

*cpp_options:
-nostdinc -isystem $musl/include -isystem include%s %(old_cpp_options)

*cc1:
%(cc1_cpu) -nostdinc -isystem $musl/include -isystem include%s

*link_libgcc:
-L$musl/lib -L .%s

*libgcc:
libgcc.a%s %:if-exists(libgcc_eh.a%s)

*startfile:
$musl/lib/crt1.o $musl/lib/crti.o crtbegin.o%s

*endfile:
crtend.o%s $musl/lib/crtn.o

*link:
-nostdlib -static
EOF
    # (With -j, libc-test can write into its directories before it makes
    # them.)
    rm -rf out-$name
    mkdir -p $(ls -d src/*/ | sed "s|^src/|out-$name/|")
    $make -k B=out-$name CC="gcc -no-pie -specs=$musl/static.specs" > out-$name.log 2>&1 || true
    grep -E '^FAIL' out-$name/REPORT | sed "s/ \[.*//; s|out-$name/||" | sort > fails-$name
}

set +x
run mucc $musl_mucc
run gcc $musl_gcc

total=$(ls out-gcc/*/*.err | grep -vcE '\.(o|lo|ld|so)\.err$')
[ -x out-gcc/common/runtest.exe ] && [ -x out-mucc/common/runtest.exe ] &&
    [ "$total" -gt 400 ] || { echo "libc-test did not run"; exit 1; }
echo "libc-test: $total tests"
echo "  musl built by gcc:  $(wc -l < fails-gcc) fail"
echo "  musl built by mucc: $(wc -l < fails-mucc) fail"
if ! diff fails-gcc fails-mucc > fails.diff; then
    echo "They differ (< gcc only, > mucc only):"
    cat fails.diff
    exit 1
fi
echo "The same tests fail with both."
