/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * Definitions for virtio I2C Adpter
 *
 * Copyright (c) 2021 Intel Corporation. All rights reserved.
 */

#ifndef _LINUX_VIRTIO_I2C_H
#define _LINUX_VIRTIO_I2C_H

#include <linux/const.h>
#include <linux/types.h>

#define VIRTIO_I2C_F_ZERO_LENGTH_REQUEST	0

#define VIRTIO_I2C_FLAGS_FAIL_NEXT	_BITUL(0)

#define VIRTIO_I2C_FLAGS_M_RD		_BITUL(1)

struct virtio_i2c_out_hdr {
	__le16 addr;
	__le16 padding;
	__le32 flags;
};

struct virtio_i2c_in_hdr {
	__u8 status;
};

#define VIRTIO_I2C_MSG_OK	0
#define VIRTIO_I2C_MSG_ERR	1

#endif
