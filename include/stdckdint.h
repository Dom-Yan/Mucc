#ifndef __STDCKDINT_H
#define __STDCKDINT_H

#define __STDC_VERSION_STDCKDINT_H__ 202311L

// C23's checked integer arithmetic: each stores a op b, cut to *r's type,
// in *r, and is true if the exact result didn't fit.
#define ckd_add(r, a, b) __builtin_add_overflow((a), (b), (r))
#define ckd_sub(r, a, b) __builtin_sub_overflow((a), (b), (r))
#define ckd_mul(r, a, b) __builtin_mul_overflow((a), (b), (r))

#endif
