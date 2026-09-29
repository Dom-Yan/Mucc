#!/bin/bash
# test/thirdparty/distros.sh MUCC [IMAGE...]: the single binary (build/mucc)
# builds real programs on other Linux distributions. By hand: it needs
# Docker and takes hours. In each image, with make and git but no C
# compiler, binutils or C headers, MUCC builds zlib, Lua and SQLite, and
# each passes its own tests, and it builds mucc, whose test programs pass
# and whose stages 2 and 3 are identical, all against the bundled musl.
# (mucc's test scripts compare it with gcc and binutils, so CI runs them.)
# A log for each image goes in test/thirdparty/work/distros-IMAGE.log.
mucc=`realpath ${1:-build/mucc}`
shift
images=${@:-ubuntu:24.04 fedora:latest alpine:latest centos:7}

# What each image needs installed: make, git and a shell to run the
# scripts, and nothing that compiles.
packages() {
    case $1 in
    ubuntu*) echo 'apt-get update -qq && apt-get install -y -qq --no-install-recommends make git ca-certificates file diffutils' ;;
    fedora*) echo 'dnf install -y -q make git-core file diffutils findutils' ;;
    alpine*) echo 'apk add -q bash make git file' ;;
    # CentOS 7's mirrors are gone; its packages are in the vault. Its rpm
    # walks every possible file descriptor, which takes hours under
    # docker build's open-file limit, so lower it. Its git pulls in
    # binutils, which is removed again.
    centos*) echo "sed -i 's/^mirrorlist/#&/; s|^#baseurl=http://mirror|baseurl=http://vault|' /etc/yum.repos.d/*.repo && ulimit -n 1024 && yum install -y -q make git file && rpm -e --nodeps binutils" ;;
    esac
}

tmp=`mktemp -d /tmp/mucc-tp-distros-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
mkdir -p test/thirdparty/work

status=0
for img in $images; do
    log=test/thirdparty/work/distros-${img//[:\/]/-}.log
    printf 'FROM %s\nRUN %s\n' $img "$(packages $img)" > $tmp/Dockerfile
    docker build -q -t mucc-tp-distros $tmp > /dev/null || { status=1; continue; }

    # A copy of the tree for each run, as the container's user; clones
    # already in test/thirdparty/work are reused. That user has no name,
    # which CentOS 7's git needs unless it's given one.
    docker run --rm -u `id -u`:`id -g` -e HOME=/tmp -e MUCC=/m/mucc \
        -e GIT_COMMITTER_NAME=mucc -e GIT_COMMITTER_EMAIL=mucc@localhost \
        -v $mucc:/m/mucc:ro -v `pwd`:/src:ro mucc-tp-distros bash -c '
        set -e
        cp -r /src /tmp/r && cd /tmp/r
        rm -rf build stage2* stage3* mucc src/*.o
        command -v gcc cc as ld && exit 1
        test/thirdparty/zlib.sh
        test/thirdparty/lua.sh
        test/thirdparty/sqlite.sh
        make -j`nproc` mucc CC=$MUCC && make libc
        tests=`ls test/*.c | sed "s/\.c$/.musl.exe/"`
        make -j`nproc` LIBC=mucc $tests
        for t in $tests; do ./$t > /dev/null || { echo "$t failed"; exit 1; }; done
        make -j`nproc` LIBC=mucc selfhost
        echo "all passed"' > $log 2>&1
    if tail -1 $log | grep -q '^all passed$'; then
        echo "testing $img ... passed"
    else
        echo "testing $img ... failed (see $log)"
        status=1
    fi
done
docker rmi -f mucc-tp-distros > /dev/null 2>&1
exit $status
