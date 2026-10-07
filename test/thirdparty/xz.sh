#!/bin/bash
url='https://github.com/tukaani-project/xz/releases/download/v5.6.3/xz-5.6.3.tar.gz'
sha256=b1d45295d3f71f25a4c9101bd7c8d16cb56348bbef3bbc738da0351e17c73317
. test/thirdparty/common

CC=$mucc AR="$mucc -ar" RANLIB="$mucc -ranlib" ./configure --disable-nls --disable-shared
$make clean
$make
$make check
