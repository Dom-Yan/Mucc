/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB) */
/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IB_USER_MAD_H
#define IB_USER_MAD_H

#include <linux/types.h>
#include <rdma/rdma_user_ioctl.h>

#define IB_USER_MAD_ABI_VERSION	5

struct ib_user_mad_hdr_old {
	__u32	id;
	__u32	status;
	__u32	timeout_ms;
	__u32	retries;
	__u32	length;
	__be32	qpn;
	__be32  qkey;
	__be16	lid;
	__u8	sl;
	__u8	path_bits;
	__u8	grh_present;
	__u8	gid_index;
	__u8	hop_limit;
	__u8	traffic_class;
	__u8	gid[16];
	__be32	flow_label;
};

struct ib_user_mad_hdr {
	__u32	id;
	__u32	status;
	__u32	timeout_ms;
	__u32	retries;
	__u32	length;
	__be32	qpn;
	__be32  qkey;
	__be16	lid;
	__u8	sl;
	__u8	path_bits;
	__u8	grh_present;
	__u8	gid_index;
	__u8	hop_limit;
	__u8	traffic_class;
	__u8	gid[16];
	__be32	flow_label;
	__u16	pkey_index;
	__u8	reserved[6];
};

struct ib_user_mad {
	struct ib_user_mad_hdr hdr;
	__aligned_u64	data[];
};

typedef unsigned long __attribute__((aligned(4))) packed_ulong;
#define IB_USER_MAD_LONGS_PER_METHOD_MASK (128 / (8 * sizeof (long)))

struct ib_user_mad_reg_req {
	__u32	id;
	packed_ulong method_mask[IB_USER_MAD_LONGS_PER_METHOD_MASK];
	__u8	qpn;
	__u8	mgmt_class;
	__u8	mgmt_class_version;
	__u8    oui[3];
	__u8	rmpp_version;
};

enum {
	IB_USER_MAD_USER_RMPP = (1 << 0),
};
#define IB_USER_MAD_REG_FLAGS_CAP (IB_USER_MAD_USER_RMPP)
struct ib_user_mad_reg_req2 {
	__u32	id;
	__u32	qpn;
	__u8	mgmt_class;
	__u8	mgmt_class_version;
	__u16   res;
	__u32   flags;
	__aligned_u64 method_mask[2];
	__u32   oui;
	__u8	rmpp_version;
	__u8	reserved[3];
};

#endif
