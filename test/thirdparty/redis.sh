#!/bin/bash
repo='https://github.com/redis/redis.git'
. test/thirdparty/common
git reset --hard 7.2.5

# With libc's malloc rather than the jemalloc it bundles (whose build
# needs more than a C compiler), and its own tests, which are Tcl.
$make distclean
$make CC=$mucc AR="$mucc -ar" RANLIB="$mucc -ranlib" MALLOC=libc
(cd ../../../.. && test/thirdparty/tcl.sh)
PATH=`cd .. && pwd`/tcl-install/bin:$PATH ./runtest --clients 8
