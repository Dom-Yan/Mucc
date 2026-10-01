/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __IP_SET_BITMAP_H
#define __IP_SET_BITMAP_H

#include <linux/netfilter/ipset/ip_set.h>

enum {

	IPSET_ERR_BITMAP_RANGE = IPSET_ERR_TYPE_SPECIFIC,

	IPSET_ERR_BITMAP_RANGE_SIZE,
};

#endif
