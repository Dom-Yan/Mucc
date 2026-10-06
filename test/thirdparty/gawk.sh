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

# With musl, two tests differ, as they do with gcc on musl: commas needs
# printf's ' grouping, which musl doesn't do, and clos1way6's output
# order depends on stdio's buffering. (With glibc both pass.) A failed
# test leaves test/_NAME.
if printf '#include <stdio.h>\n#ifndef __GLIBC__\nmusl\n#endif\n' |
       $mucc -E -xc - | grep -q '^musl$'; then
    $make check || true
    [ "$(cd test && ls _* | tr '\n' ' ')" = "_clos1way6 _commas " ]
else
    $make check
fi
