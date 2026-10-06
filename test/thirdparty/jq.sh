#!/bin/bash
url='https://github.com/jqlang/jq/releases/download/jq-1.7.1/jq-1.7.1.tar.gz'
sha256=478c9ca129fd2e3443fe27314b455e211e0d8c60bc8ff7df703873deeee580c2
. test/thirdparty/common

# With the oniguruma regex library jq bundles, built by mucc too
CC=$mucc AR="$mucc -ar" RANLIB="$mucc -ranlib" ./configure \
    --with-oniguruma=builtin --disable-docs --disable-shared
$make clean
$make
$make check
