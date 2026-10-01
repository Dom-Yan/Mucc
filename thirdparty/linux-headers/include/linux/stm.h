/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * System Trace Module (STM) userspace interfaces
 * Copyright (c) 2014, Intel Corporation.
 *
 * STM class implements generic infrastructure for  System Trace Module devices
 * as defined in MIPI STPv2 specification.
 */

#ifndef _LINUX_STM_H
#define _LINUX_STM_H

#include <linux/types.h>

#define STP_MASTER_MAX	0xffff
#define STP_CHANNEL_MAX	0xffff

struct stp_policy_id {
	__u32		size;
	__u16		master;
	__u16		channel;
	__u16		width;

	__u16		__reserved_0;
	__u32		__reserved_1;
	char		id[];
};

#define STP_POLICY_ID_SET	_IOWR('%', 0, struct stp_policy_id)
#define STP_POLICY_ID_GET	_IOR('%', 1, struct stp_policy_id)
#define STP_SET_OPTIONS		_IOW('%', 2, __u64)

#endif
