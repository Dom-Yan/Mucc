/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <asm/types.h>

#ifndef __ASSEMBLY__

#include <linux/posix_types.h>

#ifdef __SIZEOF_INT128__
typedef __signed__ __int128 __s128 __attribute__((aligned(16)));
typedef unsigned __int128 __u128 __attribute__((aligned(16)));
#endif

#ifdef __CHECKER__
#define __bitwise	__attribute__((bitwise))
#else
#define __bitwise
#endif

#define __bitwise__ __bitwise

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;

typedef __u16 __bitwise __sum16;
typedef __u32 __bitwise __wsum;

#define __aligned_u64 __u64 __attribute__((aligned(8)))
#define __aligned_s64 __s64 __attribute__((aligned(8)))
#define __aligned_be64 __be64 __attribute__((aligned(8)))
#define __aligned_le64 __le64 __attribute__((aligned(8)))

typedef unsigned __bitwise __poll_t;

#endif
#endif
