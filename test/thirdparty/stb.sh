#!/bin/bash
repo='https://github.com/nothings/stb.git'
. test/thirdparty/common
git reset --hard 2c980bb59875b0d32144a71867fbdebb2f77cd20

# stb's own check that every header, with its implementation, compiles
$mucc -c -o /dev/null tests/test_c_compilation.c

# Image reading, writing and resizing, and stb_sprintf: the hashes are
# gcc's, at -O0 and -O2 alike.
$mucc -I. -o stb-check ../../stb-check.c -lm
./stb-check > stb-check.out
diff - stb-check.out <<'END'
png 00845e2f 1
jpg 93845d6c 9c922042
bmp+tga cfd162db
resize e13d5b00 657f2574
resize float aad32665
sprintf 582e550b 138
END
