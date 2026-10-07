#!/bin/bash
url='https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz'
sha256=86b9cb0aa3bcb7994faa88018292bc704cdbb708e785f7c74352ff6ea7d3175b
. test/thirdparty/common

# With its 16- and 32-bit libraries and Unicode, as most builds have.
CC=$mucc AR="$mucc -ar" RANLIB="$mucc -ranlib" ./configure --disable-shared \
    --enable-pcre2-16 --enable-pcre2-32
$make clean
$make

# With musl, grep test 150 takes the path it has for systems without
# locales: musl accepts any locale name, so the bad one it sets works.
if printf '#include <stdio.h>\n#ifndef __GLIBC__\nmusl\n#endif\n' |
       $mucc -E -xc - | grep -q '^musl$'; then
    sed -i 's|^which locale >/dev/null 2>&1$|false|' RunGrepTest
fi
$make check
