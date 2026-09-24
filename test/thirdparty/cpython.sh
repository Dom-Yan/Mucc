#!/bin/bash
repo='https://github.com/python/cpython.git'
. test/thirdparty/common
git reset --hard c75330605d4795850ec74fdc4d69aa5d92f76c00

# Python's './configure' misidentified chibicc (mucc's ancestor) as icc
# (Intel C Compiler) because icc is a substring of chibicc. This removes
# that check; it may no longer be needed for mucc.
sed -i -e 1996,2011d configure.ac
autoreconf

CC=$mucc ./configure
$make clean
$make
$make test
