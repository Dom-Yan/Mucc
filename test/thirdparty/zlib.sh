#!/bin/bash
repo='https://github.com/madler/zlib.git'
. test/thirdparty/common
git reset --hard 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf # v1.3.1

# configure only adds -fPIC and a shared-library link command for compilers
# it recognizes, so pass them.
CC=$mucc CFLAGS='-O -fPIC' LDSHARED="$mucc -shared" ./configure
$make clean
$make
$make test
