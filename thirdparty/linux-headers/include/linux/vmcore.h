/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _VMCORE_H
#define _VMCORE_H

#include <linux/types.h>

#define VMCOREDD_NOTE_NAME "LINUX"
#define VMCOREDD_MAX_NAME_BYTES 44

struct vmcoredd_header {
	__u32 n_namesz;
	__u32 n_descsz;
	__u32 n_type;
	__u8 name[8];
	__u8 dump_name[VMCOREDD_MAX_NAME_BYTES];
};

#endif
