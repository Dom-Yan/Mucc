#!/bin/bash
url='https://busybox.net/downloads/busybox-1.36.1.tar.bz2'
sha256=b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314
. test/thirdparty/common

# The default configuration (about 400 applets), every tool built by mucc,
# its own build helpers too.
make HOSTCC="$mucc" CC="$mucc" defconfig
# Without tc: 1.36.1's uses CBQ, which Linux 6.8's headers dropped (gcc
# fails on it too).
sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config
make HOSTCC="$mucc" CC="$mucc" AR="$mucc -ar" -j$(nproc)

# Its testsuite: of 914 tests, these fail with gcc's build too, on WSL 2
# (they depend on the filesystem and the host's tools), and with musl
# date-timezone too (musl's strptime doesn't take a 'Z'; with glibc
# mucc's build passes it). Any other failure fails this script.
make HOSTCC="$mucc" CC="$mucc" check > check.log 2>&1 || true
grep '^FAIL:' check.log | grep -v -e 'cpio lists hardlinks' -e 'sed NUL in command' \
    -e 'sed embedded NUL' -e 'sed nonexistent label' -e 'tar writing into read-only dir' \
    -e 'unzip (subdir only)' -e 'date-timezone' > unexpected.log || true
grep -c '^PASS:' check.log
cat unexpected.log
[ ! -s unexpected.log ]
