/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Copyright (C) 2021 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#ifndef _LINUX_VIRTIO_PCIDEV_H
#define _LINUX_VIRTIO_PCIDEV_H
#include <linux/types.h>

enum virtio_pcidev_ops {
	VIRTIO_PCIDEV_OP_RESERVED = 0,
	VIRTIO_PCIDEV_OP_CFG_READ,
	VIRTIO_PCIDEV_OP_CFG_WRITE,
	VIRTIO_PCIDEV_OP_MMIO_READ,
	VIRTIO_PCIDEV_OP_MMIO_WRITE,
	VIRTIO_PCIDEV_OP_MMIO_MEMSET,
	VIRTIO_PCIDEV_OP_INT,
	VIRTIO_PCIDEV_OP_MSI,
	VIRTIO_PCIDEV_OP_PME,
};

struct virtio_pcidev_msg {
	__u8 op;
	__u8 bar;
	__u16 reserved;
	__u32 size;
	__u64 addr;
	__u8 data[];
};

#endif
