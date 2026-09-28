#!/bin/bash
repo='https://github.com/lua/lua.git'
. test/thirdparty/common
git reset --hard 1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3 # v5.4.7

# Lua's own flags, without readline (a system package, not a compiler test),
# archived by mucc -ar, so no binutils are needed.
$make clean
$make CC=$mucc AR="$mucc -ar rcu" RANLIB="$mucc -ranlib" \
    MYCFLAGS='$(LOCAL) -std=c99 -DLUA_USE_LINUX' MYLIBS=-ldl

# Lua's own test suite, in its basic mode (_U=true skips the tests that
# need a specially built interpreter).
cd testes

# With musl, skip the decimal-point locale test: musl accepts any locale
# name, so the test runs, but never changes the decimal point, so it
# fails with gcc on Alpine too.
if printf '#include <stdio.h>\n#ifndef __GLIBC__\nmusl\n#endif\n' |
       $mucc -E -xc - | grep -q '^musl$'; then
    git checkout -q literals.lua
    sed -i 's/^if os.setlocale("pt_BR") or os.setlocale("ptb") then$/if false then/' literals.lua
fi
../lua -e"_U=true" all.lua
