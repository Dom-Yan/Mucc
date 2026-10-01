/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/capability.h>

struct pt_regs;

#define MAX_ARG_STRLEN (PAGE_SIZE * 32)
#define MAX_ARG_STRINGS 0x7FFFFFFF

#define BINPRM_BUF_SIZE 256

#define AT_FLAGS_PRESERVE_ARGV0_BIT 0
#define AT_FLAGS_PRESERVE_ARGV0 (1 << AT_FLAGS_PRESERVE_ARGV0_BIT)

#endif
