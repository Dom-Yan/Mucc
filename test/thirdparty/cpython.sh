#!/bin/bash
repo='https://github.com/python/cpython.git'
. test/thirdparty/common
git reset --hard c75330605d4795850ec74fdc4d69aa5d92f76c00

CC=$mucc ./configure
$make clean
$make
$make test
