#!/bin/bash
repo='https://github.com/sqlite/sqlite.git'
. test/thirdparty/common
git reset --hard 86f477edaa17767b39c7bae5b67cac8580f7a8c1

if echo 'int x;' | $mucc -shared -fPIC -o /dev/null -xc - 2>/dev/null; then
    CC=$mucc CFLAGS=-D_GNU_SOURCE ./configure
    sed -i 's/^wl=.*/wl=-Wl,/; s/^pic_flag=.*/pic_flag=-fPIC/' libtool
else
    # With the bundled musl, which links only statically, SQLite's test
    # program (testfixture) needs Tcl and zlib as static libraries built
    # by mucc too: zlib from zlib.sh, and Tcl from tcl.sh, installed in
    # work/tcl-install. Its tclsh runs SQLite's build scripts.
    (cd ../../../.. && test/thirdparty/zlib.sh && test/thirdparty/tcl.sh)
    work=`cd .. && pwd`
    PATH=$work/tcl-install/bin:$PATH
    CC=$mucc LD=$mucc AR="$mucc -ar" RANLIB="$mucc -ranlib" \
        CFLAGS=-D_GNU_SOURCE CPPFLAGS=-I$work/zlib LDFLAGS=-L$work/zlib \
        ./configure --disable-shared --with-tcl=$work/tcl-install/lib
fi
$make clean
$make
$make test
