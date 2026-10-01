/*
 * Virtio platform device driver
 *
 * Copyright 2011, ARM Ltd.
 *
 * Based on Virtio PCI driver by Anthony Liguori, copyright IBM Corp. 2007
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
 */

#ifndef _LINUX_VIRTIO_MMIO_H
#define _LINUX_VIRTIO_MMIO_H

#define VIRTIO_MMIO_MAGIC_VALUE		0x000

#define VIRTIO_MMIO_VERSION		0x004

#define VIRTIO_MMIO_DEVICE_ID		0x008

#define VIRTIO_MMIO_VENDOR_ID		0x00c

#define VIRTIO_MMIO_DEVICE_FEATURES	0x010

#define VIRTIO_MMIO_DEVICE_FEATURES_SEL	0x014

#define VIRTIO_MMIO_DRIVER_FEATURES	0x020

#define VIRTIO_MMIO_DRIVER_FEATURES_SEL	0x024

#ifndef VIRTIO_MMIO_NO_LEGACY

#define VIRTIO_MMIO_GUEST_PAGE_SIZE	0x028

#endif

#define VIRTIO_MMIO_QUEUE_SEL		0x030

#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034

#define VIRTIO_MMIO_QUEUE_NUM		0x038

#ifndef VIRTIO_MMIO_NO_LEGACY

#define VIRTIO_MMIO_QUEUE_ALIGN		0x03c

#define VIRTIO_MMIO_QUEUE_PFN		0x040

#endif

#define VIRTIO_MMIO_QUEUE_READY		0x044

#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050

#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060

#define VIRTIO_MMIO_INTERRUPT_ACK	0x064

#define VIRTIO_MMIO_STATUS		0x070

#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084

#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094

#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4

#define VIRTIO_MMIO_SHM_SEL             0x0ac

#define VIRTIO_MMIO_SHM_LEN_LOW         0x0b0
#define VIRTIO_MMIO_SHM_LEN_HIGH        0x0b4

#define VIRTIO_MMIO_SHM_BASE_LOW        0x0b8
#define VIRTIO_MMIO_SHM_BASE_HIGH       0x0bc

#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc

#define VIRTIO_MMIO_CONFIG		0x100

#define VIRTIO_MMIO_INT_VRING		(1 << 0)
#define VIRTIO_MMIO_INT_CONFIG		(1 << 1)

#endif
