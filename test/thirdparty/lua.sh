#!/bin/bash
repo='https://github.com/lua/lua.git'
. test/thirdparty/common
git reset --hard 1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3 # v5.4.7

# Lua's own flags, without readline (a system package, not a compiler test).
$make clean
$make CC=$mucc MYCFLAGS='$(LOCAL) -std=c99 -DLUA_USE_LINUX' MYLIBS=-ldl

# Lua's own test suite, in its basic mode (_U=true skips the tests that
# need a specially built interpreter).
cd testes
../lua -e"_U=true" all.lua
