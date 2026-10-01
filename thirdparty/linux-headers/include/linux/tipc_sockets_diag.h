/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __TIPC_SOCKETS_DIAG_H__
#define __TIPC_SOCKETS_DIAG_H__

#include <linux/types.h>
#include <linux/sock_diag.h>

struct tipc_sock_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
	__u16	pad;
	__u32	tidiag_states;
};
#endif
