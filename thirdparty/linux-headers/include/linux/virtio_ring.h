#ifndef _LINUX_VIRTIO_RING_H
#define _LINUX_VIRTIO_RING_H
/* An interface for efficient virtio implementation, currently for use by KVM,
 * but hopefully others soon.  Do NOT change this since it will
 * break existing servers and clients.
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright Rusty Russell IBM Corporation 2007. */
#include <stdint.h>
#include <linux/types.h>
#include <linux/virtio_types.h>

#define VRING_DESC_F_NEXT	1

#define VRING_DESC_F_WRITE	2

#define VRING_DESC_F_INDIRECT	4

#define VRING_PACKED_DESC_F_AVAIL	7
#define VRING_PACKED_DESC_F_USED	15

#define VRING_USED_F_NO_NOTIFY	1

#define VRING_AVAIL_F_NO_INTERRUPT	1

#define VRING_PACKED_EVENT_FLAG_ENABLE	0x0

#define VRING_PACKED_EVENT_FLAG_DISABLE	0x1

#define VRING_PACKED_EVENT_FLAG_DESC	0x2

#define VRING_PACKED_EVENT_F_WRAP_CTR	15

#define VIRTIO_RING_F_INDIRECT_DESC	28

#define VIRTIO_RING_F_EVENT_IDX		29

#define VRING_AVAIL_ALIGN_SIZE 2
#define VRING_USED_ALIGN_SIZE 4
#define VRING_DESC_ALIGN_SIZE 16

struct vring_desc {
	__virtio64 addr;
	__virtio32 len;
	__virtio16 flags;
	__virtio16 next;
};

struct vring_avail {
	__virtio16 flags;
	__virtio16 idx;
	__virtio16 ring[];
};

struct vring_used_elem {

	__virtio32 id;

	__virtio32 len;
};

typedef struct vring_used_elem __attribute__((aligned(VRING_USED_ALIGN_SIZE)))
	vring_used_elem_t;

struct vring_used {
	__virtio16 flags;
	__virtio16 idx;
	vring_used_elem_t ring[];
};

typedef struct vring_desc __attribute__((aligned(VRING_DESC_ALIGN_SIZE)))
	vring_desc_t;
typedef struct vring_avail __attribute__((aligned(VRING_AVAIL_ALIGN_SIZE)))
	vring_avail_t;
typedef struct vring_used __attribute__((aligned(VRING_USED_ALIGN_SIZE)))
	vring_used_t;

struct vring {
	unsigned int num;

	vring_desc_t *desc;

	vring_avail_t *avail;

	vring_used_t *used;
};

#ifndef VIRTIO_RING_NO_LEGACY

#define vring_used_event(vr) ((vr)->avail->ring[(vr)->num])
#define vring_avail_event(vr) (*(__virtio16 *)&(vr)->used->ring[(vr)->num])

static __inline__ void vring_init(struct vring *vr, unsigned int num, void *p,
			      unsigned long align)
{
	vr->num = num;
	vr->desc = p;
	vr->avail = (struct vring_avail *)((char *)p + num * sizeof(struct vring_desc));
	vr->used = (void *)(((uintptr_t)&vr->avail->ring[num] + sizeof(__virtio16)
		+ align-1) & ~(align - 1));
}

static __inline__ unsigned vring_size(unsigned int num, unsigned long align)
{
	return ((sizeof(struct vring_desc) * num + sizeof(__virtio16) * (3 + num)
		 + align - 1) & ~(align - 1))
		+ sizeof(__virtio16) * 3 + sizeof(struct vring_used_elem) * num;
}

#endif

static __inline__ int vring_need_event(__u16 event_idx, __u16 new_idx, __u16 old)
{

	return (__u16)(new_idx - event_idx - 1) < (__u16)(new_idx - old);
}

struct vring_packed_desc_event {

	__le16 off_wrap;

	__le16 flags;
};

struct vring_packed_desc {

	__le64 addr;

	__le32 len;

	__le16 id;

	__le16 flags;
};

#endif
