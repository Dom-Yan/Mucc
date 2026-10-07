#!/bin/bash
url='https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz'
sha256=8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
. test/thirdparty/common

# Its Huffman decoder is a .S file with BMI2 instructions, and it picks
# BMI2 code at run time with __attribute__((target)).
$make clean
$make CC="$mucc" AR="$mucc -ar" zstd
$make -C tests CC="$mucc" AR="$mucc -ar" test-zstd
