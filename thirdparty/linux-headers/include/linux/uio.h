/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *	Berkeley style UIO structures	-	Alan Cox 1994.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

#include <linux/types.h>

struct iovec
{
	void *iov_base;
	__kernel_size_t iov_len;
};

struct dmabuf_cmsg {
	__u64 frag_offset;

	__u32 frag_size;
	__u32 frag_token;

	__u32  dmabuf_id;
	__u32 flags;

};

struct dmabuf_token {
	__u32 token_start;
	__u32 token_count;
};

#define UIO_FASTIOV	8
#define UIO_MAXIOV	1024

#endif
