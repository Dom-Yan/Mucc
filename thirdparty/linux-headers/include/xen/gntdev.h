/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR MIT) */
/******************************************************************************
 * gntdev.h
 *
 * Interface to /dev/xen/gntdev.
 *
 * Copyright (c) 2007, D G Murray
 * Copyright (c) 2018, Oleksandr Andrushchenko, EPAM Systems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __LINUX_PUBLIC_GNTDEV_H__
#define __LINUX_PUBLIC_GNTDEV_H__

#include <linux/types.h>

struct ioctl_gntdev_grant_ref {

	__u32 domid;

	__u32 ref;
};

#define IOCTL_GNTDEV_MAP_GRANT_REF \
_IOC(_IOC_NONE, 'G', 0, sizeof(struct ioctl_gntdev_map_grant_ref))
struct ioctl_gntdev_map_grant_ref {

	__u32 count;
	__u32 pad;

	__u64 index;

	struct ioctl_gntdev_grant_ref refs[1];
};

#define IOCTL_GNTDEV_UNMAP_GRANT_REF \
_IOC(_IOC_NONE, 'G', 1, sizeof(struct ioctl_gntdev_unmap_grant_ref))
struct ioctl_gntdev_unmap_grant_ref {

	__u64 index;

	__u32 count;
	__u32 pad;
};

#define IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR \
_IOC(_IOC_NONE, 'G', 2, sizeof(struct ioctl_gntdev_get_offset_for_vaddr))
struct ioctl_gntdev_get_offset_for_vaddr {

	__u64 vaddr;

	__u64 offset;

	__u32 count;
	__u32 pad;
};

#define IOCTL_GNTDEV_SET_MAX_GRANTS \
_IOC(_IOC_NONE, 'G', 3, sizeof(struct ioctl_gntdev_set_max_grants))
struct ioctl_gntdev_set_max_grants {

	__u32 count;
};

#define IOCTL_GNTDEV_SET_UNMAP_NOTIFY \
_IOC(_IOC_NONE, 'G', 7, sizeof(struct ioctl_gntdev_unmap_notify))
struct ioctl_gntdev_unmap_notify {

	__u64 index;

	__u32 action;

	__u32 event_channel_port;
};

struct gntdev_grant_copy_segment {
	union {
		void *virt;
		struct {
			grant_ref_t ref;
			__u16 offset;
			domid_t domid;
		} foreign;
	} source, dest;
	__u16 len;

	__u16 flags;
	__s16 status;
};

#define IOCTL_GNTDEV_GRANT_COPY \
	_IOC(_IOC_NONE, 'G', 8, sizeof(struct ioctl_gntdev_grant_copy))
struct ioctl_gntdev_grant_copy {
	unsigned int count;
	struct gntdev_grant_copy_segment *segments;
};

#define UNMAP_NOTIFY_CLEAR_BYTE 0x1

#define UNMAP_NOTIFY_SEND_EVENT 0x2

#define GNTDEV_DMA_FLAG_WC		(1 << 0)

#define GNTDEV_DMA_FLAG_COHERENT	(1 << 1)

#define IOCTL_GNTDEV_DMABUF_EXP_FROM_REFS \
	_IOC(_IOC_NONE, 'G', 9, \
	     sizeof(struct ioctl_gntdev_dmabuf_exp_from_refs))
struct ioctl_gntdev_dmabuf_exp_from_refs {

	__u32 flags;

	__u32 count;

	__u32 fd;

	__u32 domid;

	__u32 refs[1];
};

#define IOCTL_GNTDEV_DMABUF_EXP_WAIT_RELEASED \
	_IOC(_IOC_NONE, 'G', 10, \
	     sizeof(struct ioctl_gntdev_dmabuf_exp_wait_released))
struct ioctl_gntdev_dmabuf_exp_wait_released {

	__u32 fd;
	__u32 wait_to_ms;
};

#define IOCTL_GNTDEV_DMABUF_IMP_TO_REFS \
	_IOC(_IOC_NONE, 'G', 11, \
	     sizeof(struct ioctl_gntdev_dmabuf_imp_to_refs))
struct ioctl_gntdev_dmabuf_imp_to_refs {

	__u32 fd;

	__u32 count;

	__u32 domid;

	__u32 reserved;

	__u32 refs[1];
};

#define IOCTL_GNTDEV_DMABUF_IMP_RELEASE \
	_IOC(_IOC_NONE, 'G', 12, \
	     sizeof(struct ioctl_gntdev_dmabuf_imp_release))
struct ioctl_gntdev_dmabuf_imp_release {

	__u32 fd;
	__u32 reserved;
};

#endif
