#!/bin/bash
repo='https://github.com/antirez/kilo.git'
. test/thirdparty/common
git reset --hard 323d93b29bd89a2cb446de90c4ed4fea1764176e

# A text editor in one file, built without a warning. (It needs a
# terminal, so it isn't run.)
$mucc -Wall -Werror -std=c99 -o kilo kilo.c
