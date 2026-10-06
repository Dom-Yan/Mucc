#!/bin/bash
repo='https://github.com/git/git.git'
. test/thirdparty/common
git reset --hard 54e85e7af1ac9e9a92888060d6811ae767fea1bc

# Without the optional libraries (OpenSSL, curl, expat, gettext, Tk):
# system packages, not a compiler test. zlib is needed.
$make clean
$make V=1 CC="$mucc" NO_OPENSSL=1 NO_CURL=1 NO_EXPAT=1 NO_GETTEXT=1 NO_TCLTK=1 test
