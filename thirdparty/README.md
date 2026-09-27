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

Local patches: none.

### Checking the tree

```
tar xzf musl-1.2.6.tar.gz
diff -r musl-1.2.6 thirdparty/musl
```

prints nothing.
