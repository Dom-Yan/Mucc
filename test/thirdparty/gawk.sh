#!/bin/bash
url='https://mirrors.kernel.org/gnu/gawk/gawk-5.3.0.tar.xz'
sha256=ca9c16d3d11d0ff8c69d79dc0b47267e1329a69b39b799895604ed447d3ca90b
. test/thirdparty/common

# Its extensions are shared libraries, which the bundled musl (static
# only) can't load.
ext=
echo 'int x;' | $mucc -shared -fPIC -o /dev/null -xc - 2>/dev/null || ext=--disable-extensions
CC=$mucc ./configure --disable-nls $ext
$make clean
$make
$make check
