/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 */

#ifndef _LINUX__HFI1_IOCTL_H
#define _LINUX__HFI1_IOCTL_H
#include <linux/types.h>

struct hfi1_user_info {

	__u32 userversion;
	__u32 pad;

	__u16 subctxt_cnt;
	__u16 subctxt_id;

	__u8 uuid[16];
};

struct hfi1_ctxt_info {
	__aligned_u64 runtime_flags;
	__u32 rcvegr_size;
	__u16 num_active;
	__u16 unit;
	__u16 ctxt;
	__u16 subctxt;
	__u16 rcvtids;
	__u16 credits;
	__u16 numa_node;
	__u16 rec_cpu;
	__u16 send_ctxt;
	__u16 egrtids;
	__u16 rcvhdrq_cnt;
	__u16 rcvhdrq_entsize;
	__u16 sdma_ring_size;
};

struct hfi1_tid_info {

	__aligned_u64 vaddr;

	__aligned_u64 tidlist;

	__u32 tidcnt;

	__u32 length;
};

struct hfi1_base_info {

	__u32 hw_version;

	__u32 sw_version;

	__u16 jkey;
	__u16 padding1;

	__u32 bthqp;

	__aligned_u64 sc_credits_addr;

	__aligned_u64 pio_bufbase_sop;

	__aligned_u64 pio_bufbase;

	__aligned_u64 rcvhdr_bufbase;

	__aligned_u64 rcvegr_bufbase;

	__aligned_u64 sdma_comp_bufbase;

	__aligned_u64 user_regbase;

	__aligned_u64 events_bufbase;

	__aligned_u64 status_bufbase;

	__aligned_u64 rcvhdrtail_base;

	__aligned_u64 subctxt_uregbase;
	__aligned_u64 subctxt_rcvegrbuf;
	__aligned_u64 subctxt_rcvhdrbuf;
};
#endif
