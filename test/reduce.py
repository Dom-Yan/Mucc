#!/usr/bin/env python3
# reduce.py - shrinks a program on which mucc and gcc disagree.
#
#   test/reduce.py difftest-failures/123.c      (writes 123.reduced.c)
#
# Repeatedly deletes chunks of lines, keeping a deletion only if the
# program still compiles with both compilers, still has no undefined
# behavior (checked with gcc's sanitizers), and mucc and gcc still print
# different things. Run from the repo root, after `make`.
import os
import subprocess
import sys
import tempfile

TMP = tempfile.mkdtemp(prefix='mucc-reduce-')


def run(cmd, timeout=10):
    try:
        return subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


def build_and_run(cc, src, exe):
    if run(cc + ['-o', exe, src]).returncode != 0:
        return None
    r = run([exe])
    return None if r is None else (r.returncode, r.stdout)


def interesting(lines):
    src = os.path.join(TMP, 't.c')
    with open(src, 'w') as f:
        f.write(''.join(lines))

    # -Werror=return-type: deleting a `return` is undefined behavior that
    # the sanitizers below don't catch in C. (No -w here: it would
    # silence this error too.)
    g = build_and_run(['gcc', '-O0', '-Werror=return-type'], src, os.path.join(TMP, 'g'))
    d = build_and_run(['./mucc'], src, os.path.join(TMP, 'd'))
    if g is None or d is None or g == d:
        return False

    # Make sure we didn't delete our way into undefined behavior.
    san = ['gcc', '-w', '-O0', '-fsanitize=undefined,float-cast-overflow,address',
           '-fno-sanitize-recover=all']
    return build_and_run(san, src, os.path.join(TMP, 's')) is not None


def main():
    path = sys.argv[1]
    lines = open(path).readlines()
    if not interesting(lines):
        sys.exit(f'{path}: mucc and gcc agree (or it does not compile); nothing to reduce')

    chunk = len(lines) // 2
    while chunk >= 1:
        i = 0
        while i < len(lines):
            candidate = lines[:i] + lines[i + chunk:]
            if interesting(candidate):
                lines = candidate
                print(f'{len(lines)} lines', file=sys.stderr)
            else:
                i += chunk
        chunk //= 2

    out = path[:-2] + '.reduced.c'
    with open(out, 'w') as f:
        f.write(''.join(lines))
    print(f'wrote {out} ({len(lines)} lines)')


main()
