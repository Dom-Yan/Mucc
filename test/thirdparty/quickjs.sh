#!/bin/bash
repo='https://github.com/bellard/quickjs.git'
. test/thirdparty/common
git reset --hard 535a7c250ff4a577ec36c3e103daab6dadeea650

# Bellard's JavaScript engine: a big interpreter, its own tests in JS.
$make clean
$make CC="$mucc" HOST_CC="$mucc" AR="$mucc -ar" qjs run-test262
$make CC="$mucc" HOST_CC="$mucc" AR="$mucc -ar" test
