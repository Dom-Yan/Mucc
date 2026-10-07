#!/bin/bash
url='https://busybox.net/downloads/busybox-1.36.1.tar.bz2'
sha256=b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314
. test/thirdparty/common

# The default configuration (about 400 applets), every tool built by mucc,
# its own build helpers too. Its testsuite expects some applets' fuller
# options; the failures are compared with gcc's build in the log.
make HOSTCC="$mucc" CC="$mucc" defconfig
make HOSTCC="$mucc" CC="$mucc" AR="$mucc -ar" -j$(nproc)
make HOSTCC="$mucc" CC="$mucc" check
