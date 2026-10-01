/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2024-2025 Intel Corporation
 *
 * These are definitions for the mailbox command interface of CXL subsystem.
 */
#ifndef _FWCTL_CXL_H_
#define _FWCTL_CXL_H_

#include <linux/types.h>
#include <linux/stddef.h>
#include <cxl/features.h>

struct fwctl_rpc_cxl {
	__struct_group(fwctl_rpc_cxl_hdr, hdr,  ,
		__u32 opcode;
		__u32 flags;
		__u32 op_size;
		__u32 reserved1;
	);
	union {
		struct cxl_mbox_get_sup_feats_in get_sup_feats_in;
		struct cxl_mbox_get_feat_in get_feat_in;
		struct cxl_mbox_set_feat_in set_feat_in;
	};
};

struct fwctl_rpc_cxl_out {
	__struct_group(fwctl_rpc_cxl_out_hdr, hdr,  ,
		__u32 size;
		__u32 retval;
	);
	union {
		struct cxl_mbox_get_sup_feats_out get_sup_feats_out;
		__DECLARE_FLEX_ARRAY(__u8, payload);
	};
};

#endif
