#!/bin/bash
repo='https://github.com/richgel999/miniz.git'
. test/thirdparty/common
git reset --hard 293d4db1b7d0ffee9756d035b9ac6f7431ef8492 # 3.0.2

# miniz_export.h comes from its CMake build; this is what it says for a
# static library.
echo '#define MINIZ_EXPORT' > miniz_export.h
$mucc -I. -o miniz-check ../../miniz-check.c miniz.c miniz_tdef.c miniz_tinfl.c miniz_zip.c
./miniz-check > miniz-check.out
diff - miniz-check.out <<'END'
level 0 0b78a428 200041 1
level 1 14f068c1 61763 1
level 2 f70b4e56 61786 1
level 3 d0599a5f 61540 1
level 4 7f0705a4 61760 1
level 5 d0599a5f 61540 1
level 6 dc04b0b9 61422 1
level 7 12e88c4f 61423 1
level 8 170f24f5 61424 1
level 9 0c02296f 61426 1
level 10 7cfee272 61433 1
tdefl 183a5464 1
zip 62125 1
crc b31f99e7 adler 2ee3cd79
END
