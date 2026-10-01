/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */

#ifndef RVT_ABI_USER_H
#define RVT_ABI_USER_H

#include <linux/types.h>
#include <rdma/ib_user_verbs.h>
#ifndef RDMA_ATOMIC_UAPI
#define RDMA_ATOMIC_UAPI(_type, _name) struct{ _type val; } _name
#endif

struct rvt_wqe_sge {
	__aligned_u64 addr;
	__u32 length;
	__u32 lkey;
};

struct rvt_cq_wc {

	RDMA_ATOMIC_UAPI(__u32, head);

	RDMA_ATOMIC_UAPI(__u32, tail);

	struct ib_uverbs_wc uqueue[];
};

struct rvt_rwqe {
	__u64 wr_id;
	__u8 num_sge;
	__u8 padding[7];
	struct rvt_wqe_sge sg_list[];
};

struct rvt_rwq {

	RDMA_ATOMIC_UAPI(__u32, head);

	RDMA_ATOMIC_UAPI(__u32, tail);
	struct rvt_rwqe wq[];
};
#endif
