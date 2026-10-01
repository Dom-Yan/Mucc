/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _TRACE_MMAP_H_
#define _TRACE_MMAP_H_

#include <linux/types.h>

struct trace_buffer_meta {
	__u32		meta_page_size;
	__u32		meta_struct_len;

	__u32		subbuf_size;
	__u32		nr_subbufs;

	struct {
		__u64	lost_events;
		__u32	id;
		__u32	read;
	} reader;

	__u64	flags;

	__u64	entries;
	__u64	overrun;
	__u64	read;

	__u64	Reserved1;
	__u64	Reserved2;
};

#define TRACE_MMAP_IOCTL_GET_READER		_IO('R', 0x20)

#endif
