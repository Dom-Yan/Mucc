/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Intel SCIF driver.
 *
 */

#ifndef SCIF_IOCTL_H
#define SCIF_IOCTL_H

#include <linux/types.h>

struct scif_port_id {
	__u16 node;
	__u16 port;
};

struct scifioctl_connect {
	struct scif_port_id	self;
	struct scif_port_id	peer;
};

struct scifioctl_accept {
	__s32			flags;
	struct scif_port_id	peer;
	__u64			endpt;
};

struct scifioctl_msg {
	__u64	msg;
	__s32	len;
	__s32	flags;
	__s32	out_len;
};

struct scifioctl_reg {
	__u64		addr;
	__u64		len;
	__s64		offset;
	__s32		prot;
	__s32		flags;
	__s64		out_offset;
};

struct scifioctl_unreg {
	__s64		offset;
	__u64		len;
};

struct scifioctl_copy {
	__s64		loffset;
	__u64		len;
	__s64		roffset;
	__u64		addr;
	__s32		flags;
};

struct scifioctl_fence_mark {
	__s32	flags;
	__u64	mark;
};

struct scifioctl_fence_signal {
	__s64		loff;
	__u64		lval;
	__s64		roff;
	__u64		rval;
	__s32		flags;
};

struct scifioctl_node_ids {
	__u64	nodes;
	__u64	self;
	__s32	len;
};

#define SCIF_BIND		_IOWR('s', 1, __u64)
#define SCIF_LISTEN		_IOW('s', 2, __s32)
#define SCIF_CONNECT		_IOWR('s', 3, struct scifioctl_connect)
#define SCIF_ACCEPTREQ		_IOWR('s', 4, struct scifioctl_accept)
#define SCIF_ACCEPTREG		_IOWR('s', 5, __u64)
#define SCIF_SEND		_IOWR('s', 6, struct scifioctl_msg)
#define SCIF_RECV		_IOWR('s', 7, struct scifioctl_msg)
#define SCIF_REG		_IOWR('s', 8, struct scifioctl_reg)
#define SCIF_UNREG		_IOWR('s', 9, struct scifioctl_unreg)
#define SCIF_READFROM		_IOWR('s', 10, struct scifioctl_copy)
#define SCIF_WRITETO		_IOWR('s', 11, struct scifioctl_copy)
#define SCIF_VREADFROM		_IOWR('s', 12, struct scifioctl_copy)
#define SCIF_VWRITETO		_IOWR('s', 13, struct scifioctl_copy)
#define SCIF_GET_NODEIDS	_IOWR('s', 14, struct scifioctl_node_ids)
#define SCIF_FENCE_MARK		_IOWR('s', 15, struct scifioctl_fence_mark)
#define SCIF_FENCE_WAIT		_IOWR('s', 16, __s32)
#define SCIF_FENCE_SIGNAL	_IOWR('s', 17, struct scifioctl_fence_signal)

#endif
