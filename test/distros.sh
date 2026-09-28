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
    out=$(docker run --rm -v $tmp:/m:ro --tmpfs /work:exec $img sh -c \
        'cd /work && /m/mucc --libc=mucc -o hello /m/hello.c -lm && ./hello' 2>&1)
    if [ "$out" = "hello 1.414" ]; then
        echo "testing $img ... passed"
    else
        echo "testing $img ... failed: $out"
        status=1
    fi
done

# An image with nothing but mucc and hello.c: no shell, no /tmp.
printf 'FROM scratch\nCOPY mucc hello.c /\nRUN ["/mucc", "--libc=mucc", "-o", "/hello", "/hello.c"]\nCMD ["/hello"]\n' \
    > $tmp/Dockerfile
if docker build -q -t mucc-distros-scratch $tmp > /dev/null &&
   [ "$(docker run --rm mucc-distros-scratch)" = "hello 1.414" ]; then
    echo "testing FROM scratch ... passed"
else
    echo "testing FROM scratch ... failed"
    status=1
fi
docker rmi -f mucc-distros-scratch > /dev/null 2>&1
exit $status
