#!/bin/bash
url='https://mirrors.kernel.org/gnu/sed/sed-4.9.tar.xz'
sha256=6e226b732e1cd739464ad6862bd1a1aba42d7982922da7a53519631d24975181
. test/thirdparty/common

musl=
printf '#include <stdio.h>\n#ifndef __GLIBC__\nmusl\n#endif\n' |
    $mucc -E -xc - | grep -q '^musl$' && musl=1

# Its tests include gnulib's, which check much of the C library through
# mucc's headers. With musl, the build is musl's as configure sees it:
# config.guess asks the system's compiler, which may be glibc's, and
# gnulib's tests expect musl's behavior only when told.
CC=$mucc ./configure --disable-nls ${musl:+--build=x86_64-pc-linux-musl}
$make clean
$make

# With musl, skip newline-dfa-bug, which runs sed in valgrind (if it's
# installed): valgrind can't replace malloc in a static program, and
# reports musl's own malloc as reading uninitialized memory, as it does
# for a static gcc build on musl.
[ -n "$musl" ] && sed -i 's| testsuite/newline-dfa-bug\.sh | |' Makefile
$make check
