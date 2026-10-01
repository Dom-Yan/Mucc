#!/usr/bin/env python3
# Copies a tree of C headers without their comments, trailing spaces and
# runs of blank lines, keeping the comments that carry a license or
# copyright notice. Used for linux-headers/ (see README.md):
#
#   strip-comments.py SRC DEST
#
# The tokens are untouched; only what's between them shrinks.

import os
import re
import sys

NOTICE = re.compile(r'SPDX|[Cc]opyright|COPYRIGHT|\([Cc]\)|[Ll]icen[cs]e|LICEN[CS]E|GPL')

def strip(src):
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith('/*', i):
            j = src.index('*/', i + 2)
            if NOTICE.search(src, i, j):
                out.append(src[i:j + 2])
            else:
                nl = src.count('\n', i, j)
                out.append('\n' * nl if nl else ' ')
            i = j + 2
        elif src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            if NOTICE.search(src, i, j):
                out.append(src[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    s = ''.join(out)
    s = re.sub(r'[ \t]+\n', '\n', s)
    s = re.sub(r'\n{3,}', '\n\n', s)
    return s

def main():
    src_dir, dest_dir = sys.argv[1], sys.argv[2]
    for root, _, files in os.walk(src_dir):
        for name in files:
            path = os.path.join(root, name)
            dest = os.path.join(dest_dir, os.path.relpath(path, src_dir))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(path, encoding='latin-1') as f:
                text = f.read()
            with open(dest, 'w', encoding='latin-1', newline='\n') as f:
                f.write(strip(text))

main()
