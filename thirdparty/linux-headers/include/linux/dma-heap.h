/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * DMABUF Heaps Userspace API
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */
#ifndef _LINUX_DMABUF_POOL_H
#define _LINUX_DMABUF_POOL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DMA_HEAP_VALID_FD_FLAGS (O_CLOEXEC | O_ACCMODE)

#define DMA_HEAP_VALID_HEAP_FLAGS (0ULL)

struct dma_heap_allocation_data {
	__u64 len;
	__u32 fd;
	__u32 fd_flags;
	__u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC		'H'

#define DMA_HEAP_IOCTL_ALLOC	_IOWR(DMA_HEAP_IOC_MAGIC, 0x0,\
				      struct dma_heap_allocation_data)

#endif
