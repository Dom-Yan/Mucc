#!/bin/bash
url='https://mirrors.kernel.org/gnu/make/make-4.4.1.tar.gz'
sha256=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
. test/thirdparty/common

# Without gettext (a system package, not a compiler test)
CC=$mucc ./configure --disable-nls
$make clean
$make

# With musl, skip misc/close_stdout: make calls exit() from an atexit()
# handler when stdout fails, and musl traps a recursive exit(), so the
# test crashes with gcc on musl too.
if printf '#include <stdio.h>\n#ifndef __GLIBC__\nmusl\n#endif\n' |
       $mucc -E -xc - | grep -q '^musl$'; then
    rm -f tests/scripts/misc/close_stdout
fi
$make check
