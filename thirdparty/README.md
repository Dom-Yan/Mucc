# Vendored code

Code from other projects that mucc bundles. Each one is kept as released,
except for the patches listed here, so it can be checked against its release
and upgraded by replacing it.

## musl

The C library mucc bundles (see Phase 3 of `PLAN.md`). MIT license, in
`musl/COPYRIGHT`.

- Version: 1.2.6
- From: https://musl.libc.org/releases/musl-1.2.6.tar.gz
- SHA-256 of the tarball:
  `d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`
- Signature: https://musl.libc.org/releases/musl-1.2.6.tar.gz.asc, a good
  signature from "musl libc <musl@libc.org>", key `56BCDB593020450F`, as
  published at https://musl.libc.org/musl.pub.
- `musl/` is the tarball's `musl-1.2.6/` directory.

Local patches, in `patches/`, applied in this order. Both are musl's own
fixes for the advisories on its front page that cover 1.2.6, taken from its
mailing list:

- `musl-cve-2026-40200.patch`: qsort could write past a stack array. Only
  reachable on x86-64 with more than about 34 trillion elements, but the
  patch also fixes undefined shifts in the same code. From
  https://www.openwall.com/lists/musl/2026/04/10/3/1
- `musl-cve-2026-6042.patch`: iconv's GB18030 decoder could take seconds
  per character (a denial of service), and decoded some characters wrong.
  From https://www.openwall.com/lists/musl/2026/04/03/2/1

### Checking the tree

```
tar xzf musl-1.2.6.tar.gz
cd musl-1.2.6
patch -p1 < ../thirdparty/patches/musl-cve-2026-40200.patch
patch -p1 < ../thirdparty/patches/musl-cve-2026-6042.patch
cd ..
diff -r musl-1.2.6 thirdparty/musl
```

prints nothing.
