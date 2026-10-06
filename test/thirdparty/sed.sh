#!/bin/bash
url='https://mirrors.kernel.org/gnu/sed/sed-4.9.tar.xz'
sha256=6e226b732e1cd739464ad6862bd1a1aba42d7982922da7a53519631d24975181
. test/thirdparty/common

# Its tests include gnulib's, which check much of the C library through
# mucc's headers.
CC=$mucc ./configure --disable-nls
$make clean
$make
$make check
