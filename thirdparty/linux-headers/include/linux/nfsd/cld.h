/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Upcall description for nfsdcld communication
 *
 * Copyright (c) 2012 Red Hat, Inc.
 * Author(s): Jeff Layton <jlayton@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _NFSD_CLD_H
#define _NFSD_CLD_H

#include <linux/types.h>

#define CLD_UPCALL_VERSION 2

#define NFS4_OPAQUE_LIMIT 1024

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE      32
#endif

enum cld_command {
	Cld_Create,
	Cld_Remove,
	Cld_Check,
	Cld_GraceDone,
	Cld_GraceStart,
	Cld_GetVersion,
};

struct cld_name {
	__u16		cn_len;
	unsigned char	cn_id[NFS4_OPAQUE_LIMIT];
} __attribute__((packed));

struct cld_princhash {
	__u8		cp_len;
	unsigned char	cp_data[SHA256_DIGEST_SIZE];
} __attribute__((packed));

struct cld_clntinfo {
	struct cld_name		cc_name;
	struct cld_princhash	cc_princhash;
} __attribute__((packed));

struct cld_msg {
	__u8		cm_vers;
	__u8		cm_cmd;
	__s16		cm_status;
	__u32		cm_xid;
	union {
		__s64		cm_gracetime;
		struct cld_name	cm_name;
		__u8		cm_version;
	} __attribute__((packed)) cm_u;
} __attribute__((packed));

struct cld_msg_v2 {
	__u8		cm_vers;
	__u8		cm_cmd;
	__s16		cm_status;
	__u32		cm_xid;
	union {
		struct cld_name	cm_name;
		__u8		cm_version;
		struct cld_clntinfo cm_clntinfo;
	} __attribute__((packed)) cm_u;
} __attribute__((packed));

struct cld_msg_hdr {
	__u8		cm_vers;
	__u8		cm_cmd;
	__s16		cm_status;
	__u32		cm_xid;
} __attribute__((packed));

#endif
