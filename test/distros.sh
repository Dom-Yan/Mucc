#!/bin/bash
# test/distros.sh [MUCC [IMAGE...]]: the single binary (build/mucc) on
# other Linux distributions, in Docker. In each image, with only MUCC and
# hello.c mounted, it builds and runs a program; then it does the same in
# an empty image (FROM scratch). Not part of `make test-all`, since it
# needs Docker and downloads the images. Containers share the host's
# kernel, so this checks the distributions' files, not their kernels.
mucc=${1:-build/mucc}
shift
images=${@:-ubuntu:24.04 fedora:latest alpine:latest centos:7}

tmp=`mktemp -d /tmp/mucc-distros-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
cp $mucc $tmp/mucc
cat > $tmp/hello.c <<'EOF'
#include <math.h>
#include <stdio.h>
int main(void) { printf("hello %.3f\n", sqrt(2.0)); return 0; }
EOF

status=0
for img in $images; do
    # Pulled first, so Docker's progress messages don't mix into $out.
    docker pull -q $img > /dev/null
    out=$(docker run --rm -v $tmp:/m:ro --tmpfs /work:exec $img sh -c \
        'cd /work && /m/mucc -o hello /m/hello.c -lm && ./hello' 2>&1)
    if [ "$out" = "hello 1.414" ]; then
        echo "testing $img ... passed"
    else
        echo "testing $img ... failed: $out"
        status=1
    fi
done

# An image with nothing but mucc and hello.c: no shell, no /tmp.
printf 'FROM scratch\nCOPY mucc hello.c /\nRUN ["/mucc", "-o", "/hello", "/hello.c"]\nCMD ["/hello"]\n' \
    > $tmp/Dockerfile
if docker build -q -t mucc-distros-scratch $tmp > /dev/null &&
   [ "$(docker run --rm mucc-distros-scratch)" = "hello 1.414" ]; then
    echo "testing FROM scratch ... passed"
else
    echo "testing FROM scratch ... failed"
    status=1
fi
docker rmi -f mucc-distros-scratch > /dev/null 2>&1

# In an empty image, mucc rebuilds itself from src/ with the files it
# embeds (build/files.s and what it names), and that mucc rebuilds itself
# again: the two must be the same, byte for byte. (MUCC itself differs
# from them only in its debug info, which names musl's headers by their
# paths on disk, not "<mucc>/musl/...".)
if [ -f build/files.s ]; then
    mkdir -p $tmp/m/build
    cp -r src include $tmp/m
    cp -r build/files.s build/musl $tmp/m/build
    srcs=$(ls src/*.c | sed 's/.*/"&", /' | tr -d '\n')
    build='"-o", "NEW", '"$srcs"'"build/files.s"]'
    printf '%s\n' 'FROM scratch' 'COPY mucc /mucc' 'COPY m /m' 'WORKDIR /m' \
        "RUN [\"/mucc\", \"--libc=mucc\", \"-Iinclude\", ${build/NEW/mucc2}" \
        "RUN [\"./mucc2\", \"--libc=mucc\", \"-Iinclude\", ${build/NEW/mucc3}" \
        > $tmp/Dockerfile
    if docker build -q -t mucc-distros-rebuild $tmp > /dev/null &&
       id=$(docker create mucc-distros-rebuild /m/mucc3) &&
       docker cp -q $id:/m $tmp/out && docker rm $id > /dev/null &&
       cmp $tmp/out/mucc2 $tmp/out/mucc3; then
        echo "testing rebuild FROM scratch ... passed"
    else
        echo "testing rebuild FROM scratch ... failed"
        status=1
    fi
    docker rmi -f mucc-distros-rebuild > /dev/null 2>&1
fi
exit $status
