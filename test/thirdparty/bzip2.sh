#!/bin/bash
url='https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz'
sha256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
. test/thirdparty/common

# Its default target builds and then checks it on its sample files.
make clean
make CC="$mucc" AR="$mucc -ar" RANLIB="$mucc -ranlib"
