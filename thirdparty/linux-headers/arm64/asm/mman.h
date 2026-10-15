/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_MMAN_H
#define __ASM_MMAN_H

#include <asm-generic/mman.h>

#define PROT_BTI	0x10
#define PROT_MTE	0x20

#define PKEY_DISABLE_EXECUTE	0x4
#define PKEY_DISABLE_READ	0x8
#undef PKEY_ACCESS_MASK
#define PKEY_ACCESS_MASK       (PKEY_DISABLE_ACCESS |\
				PKEY_DISABLE_WRITE  |\
				PKEY_DISABLE_READ   |\
				PKEY_DISABLE_EXECUTE)

#endif
