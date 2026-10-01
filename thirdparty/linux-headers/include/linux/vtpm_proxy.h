/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Definitions for the VTPM proxy driver
 * Copyright (c) 2015, 2016, IBM Corporation
 * Copyright (C) 2016 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _LINUX_VTPM_PROXY_H
#define _LINUX_VTPM_PROXY_H

#include <linux/types.h>
#include <linux/ioctl.h>

enum vtpm_proxy_flags {
	VTPM_PROXY_FLAG_TPM2	= 1,
};

struct vtpm_proxy_new_dev {
	__u32 flags;
	__u32 tpm_num;
	__u32 fd;
	__u32 major;
	__u32 minor;
};

#define VTPM_PROXY_IOC_NEW_DEV	_IOWR(0xa1, 0x00, struct vtpm_proxy_new_dev)

#define TPM2_CC_SET_LOCALITY	0x20001000
#define TPM_ORD_SET_LOCALITY	0x20001000

#endif
