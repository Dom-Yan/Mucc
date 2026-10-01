/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Userspace interface for AMD Seamless Firmware Servicing (SFS)
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 *
 * Author: Ashish Kalra <ashish.kalra@amd.com>
 */

#ifndef __PSP_SFS_USER_H__
#define __PSP_SFS_USER_H__

#include <linux/types.h>

#define PAYLOAD_NAME_SIZE	64
#define TEE_EXT_CMD_BUFFER_SIZE	4096

struct sfs_user_get_fw_versions {
	__u8	blob[TEE_EXT_CMD_BUFFER_SIZE];
	__u32	sfs_status;
	__u32	sfs_extended_status;
} __attribute__((packed));

struct sfs_user_update_package {
	char	payload_name[PAYLOAD_NAME_SIZE];
	__u32	sfs_status;
	__u32	sfs_extended_status;
} __attribute__((packed));

#define SFS_IOC_TYPE	'S'

#define SFSIOCFWVERS	_IOWR(SFS_IOC_TYPE, 0x1, struct sfs_user_get_fw_versions)

#define SFSIOCUPDATEPKG	_IOWR(SFS_IOC_TYPE, 0x2, struct sfs_user_update_package)

#endif
