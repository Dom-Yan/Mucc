/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Virtio Mem Device
 *
 * Copyright Red Hat, Inc. 2020
 *
 * Authors:
 *     David Hildenbrand <david@redhat.com>
 *
 * This header is BSD licensed so anyone can use the definitions
 * to implement compatible drivers/servers:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LINUX_VIRTIO_MEM_H
#define _LINUX_VIRTIO_MEM_H

#include <linux/types.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

#define VIRTIO_MEM_F_ACPI_PXM		0

#define VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE	1

#define VIRTIO_MEM_F_PERSISTENT_SUSPEND		2

#define VIRTIO_MEM_REQ_PLUG			0

#define VIRTIO_MEM_REQ_UNPLUG			1

#define VIRTIO_MEM_REQ_UNPLUG_ALL		2

#define VIRTIO_MEM_REQ_STATE			3

struct virtio_mem_req_plug {
	__virtio64 addr;
	__virtio16 nb_blocks;
	__virtio16 padding[3];
};

struct virtio_mem_req_unplug {
	__virtio64 addr;
	__virtio16 nb_blocks;
	__virtio16 padding[3];
};

struct virtio_mem_req_state {
	__virtio64 addr;
	__virtio16 nb_blocks;
	__virtio16 padding[3];
};

struct virtio_mem_req {
	__virtio16 type;
	__virtio16 padding[3];

	union {
		struct virtio_mem_req_plug plug;
		struct virtio_mem_req_unplug unplug;
		struct virtio_mem_req_state state;
	} u;
};

#define VIRTIO_MEM_RESP_ACK			0

#define VIRTIO_MEM_RESP_NACK			1

#define VIRTIO_MEM_RESP_BUSY			2

#define VIRTIO_MEM_RESP_ERROR			3

#define VIRTIO_MEM_STATE_PLUGGED		0

#define VIRTIO_MEM_STATE_UNPLUGGED		1

#define VIRTIO_MEM_STATE_MIXED			2

struct virtio_mem_resp_state {
	__virtio16 state;
};

struct virtio_mem_resp {
	__virtio16 type;
	__virtio16 padding[3];

	union {
		struct virtio_mem_resp_state state;
	} u;
};

struct virtio_mem_config {

	__le64 block_size;

	__le16 node_id;
	__u8 padding[6];

	__le64 addr;

	__le64 region_size;

	__le64 usable_region_size;

	__le64 plugged_size;

	__le64 requested_size;
};

#endif
