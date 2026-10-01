/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VFIO Region definitions for ZPCI devices
 *
 * Copyright IBM Corp. 2020
 *
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *            Matthew Rosato <mjrosato@linux.ibm.com>
 */

#ifndef _VFIO_ZDEV_H_
#define _VFIO_ZDEV_H_

#include <linux/types.h>
#include <linux/vfio.h>

struct vfio_device_info_cap_zpci_base {
	struct vfio_info_cap_header header;
	__u64 start_dma;
	__u64 end_dma;
	__u16 pchid;
	__u16 vfn;
	__u16 fmb_length;
	__u8 pft;
	__u8 gid;

	__u32 fh;

};

struct vfio_device_info_cap_zpci_group {
	struct vfio_info_cap_header header;
	__u64 dasm;
	__u64 msi_addr;
	__u64 flags;
#define VFIO_DEVICE_INFO_ZPCI_FLAG_REFRESH 1
	__u16 mui;
	__u16 noi;
	__u16 maxstbl;
	__u8 version;

	__u8 reserved;
	__u16 imaxstbl;

};

struct vfio_device_info_cap_zpci_util {
	struct vfio_info_cap_header header;
	__u32 size;
	__u8 util_str[];
};

struct vfio_device_info_cap_zpci_pfip {
	struct vfio_info_cap_header header;
	__u32 size;
	__u8 pfip[];
};

#endif
