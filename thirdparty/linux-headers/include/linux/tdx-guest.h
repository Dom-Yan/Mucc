/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface for TDX guest driver
 *
 * Copyright (C) 2022 Intel Corporation
 */

#ifndef _LINUX_TDX_GUEST_H_
#define _LINUX_TDX_GUEST_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define TDX_REPORTDATA_LEN              64

#define TDX_REPORT_LEN                  1024

struct tdx_report_req {
	__u8 reportdata[TDX_REPORTDATA_LEN];
	__u8 tdreport[TDX_REPORT_LEN];
};

#define TDX_CMD_GET_REPORT0              _IOWR('T', 1, struct tdx_report_req)

#endif
