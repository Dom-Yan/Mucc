#!/bin/bash
# Tcl 8.6.14, built by mucc as a static library and tclsh, installed in
# test/thirdparty/work/tcl-install, for the projects whose tests are Tcl
# scripts (SQLite's, redis's). Built once.
repo='https://github.com/tcltk/tcl.git'
. test/thirdparty/common
work=`cd .. && pwd`
[ -x $work/tcl-install/bin/tclsh8.6 ] && exit 0
git reset --hard core-8-6-14
cd unix
CC=$mucc AR="$mucc -ar" RANLIB="$mucc -ranlib" ./configure \
    --prefix=$work/tcl-install --disable-shared --disable-load
$make clean
$make
make install
ln -sf tclsh8.6 $work/tcl-install/bin/tclsh
