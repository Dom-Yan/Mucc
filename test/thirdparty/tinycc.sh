#!/bin/bash
repo='https://github.com/TinyCC/tinycc.git'
. test/thirdparty/common
git reset --hard df67d8617b7d1d03a480a28f9f901848ffbfb7ec

./configure --cc="$mucc"
$make clean
$make
# Its tests compare with the system compiler, as C17: gcc 15's default,
# C23, rejects tcctest.c's implicit ints.
$make CC="cc -std=gnu17" test
