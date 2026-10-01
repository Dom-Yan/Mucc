/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022, Google LLC
 */

#ifndef _LINUX_LOOP_LOADPIN_H
#define _LINUX_LOOP_LOADPIN_H

#define LOADPIN_IOC_MAGIC	'L'

#define LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS _IOW(LOADPIN_IOC_MAGIC, 0x00, unsigned int)

#endif
