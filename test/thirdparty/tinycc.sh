#!/bin/bash
repo='https://github.com/TinyCC/tinycc.git'
. test/thirdparty/common
git reset --hard df67d8617b7d1d03a480a28f9f901848ffbfb7ec

# tcc links its programs against the system's C library, so build it for
# that (MUCC set to a script running `mucc --libc=system`: its configure
# can't take a compiler with options).
./configure --cc="$mucc"
$make clean
$make
# Its tests compare with the system compiler, which since gcc 14 rejects
# tcctest.c's implicit ints unless -fpermissive. They run one at a time:
# several build and run the same a.exe.
make CC="cc -fpermissive" test
