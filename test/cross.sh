#!/bin/bash
# mucc for the other CPU (aarch64 on x86-64, or x86_64 on ARM64), built
# by ./mucc against the bundled musl for it (`make libc-ARCH`), and run
# by qemu-user (through binfmt_misc, as Debian's qemu-user-binfmt sets
# it up): it must compile mucc's own sources into the same objects as
# ./mucc --target= does, and pass the language tests, with test/common
# built by it too, against that musl. Running there, it's exact in long
# double as cross-compiling isn't (see README.md).
target=$1
mucc=./mucc
dir=stage2-$target

check() {
  if [ $? -eq 0 ]; then
    echo "testing $target: $1 ... passed"
  else
    echo "testing $target: $1 ... failed"
    exit 1
  fi
}

# mucc for it, set up as `make install` would: include/ and its musl
# (build/musl) next to it
rm -rf $dir
mkdir -p $dir/obj $dir/obj2 $dir/build $dir/test
for f in src/*.c; do
  $mucc --target=$target-linux --libc=mucc -Iinclude -c -o $dir/obj/$(basename $f .c).o $f || exit 1
done
$mucc --target=$target-linux --libc=mucc -o $dir/mucc $dir/obj/*.o || exit 1
ln -s ../include $dir/include
ln -s ../../build/musl-$target $dir/build/musl

# Without a way to run programs for it, the rest is skipped.
if ! $dir/mucc --version > /dev/null 2>&1; then
  echo "testing $target: skipped (can't run its programs: needs qemu-user and binfmt_misc)"
  exit 0
fi

for f in src/*.c; do
  $dir/mucc --libc=mucc -Iinclude -c -o $dir/obj2/$(basename $f .c).o $f || exit 1
done
for f in $dir/obj/*.o; do
  cmp -s $f $dir/obj2/$(basename $f) || { echo "  $f differs"; false; } || break
done
check 'mucc built by itself there is the same as cross-compiled'

$dir/mucc --libc=mucc -c -o $dir/test/common.o -xc test/common
check 'test/common'
for f in test/*.c; do
  t=$(basename $f .c)
  $dir/mucc --libc=mucc -Iinclude -Itest $(sed -n 's|^// flags: ||p' $f) -pthread \
    -o $dir/test/$t $f $dir/test/common.o -lm > $dir/test/$t.out 2>&1 &&
  $dir/test/$t > $dir/test/$t.out 2>&1
  [ $? -eq 0 ] || tail -5 $dir/test/$t.out
  check $t
done

# Struct layouts must be gcc's for that CPU, which differ (zero-width
# bit-fields count in alignment on ARM64): compared with gcc for it,
# where there is one (gcc-aarch64-linux-gnu, ...).
if command -v $target-linux-gnu-gcc > /dev/null; then
  $target-linux-gnu-gcc -static -o $dir/test/layout-gcc test/attribute-layout.c &&
  $dir/mucc --libc=mucc -Iinclude -o $dir/test/layout test/attribute-layout.c &&
  diff <($dir/test/layout-gcc) <($dir/test/layout)
  check 'attribute layout against gcc'
fi
