/* SPDX-License-Identifier: ((GPL-2.0-only WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * linux/can/isotp.h
 *
 * Definitions for ISO 15765-2 CAN transport protocol sockets
 *
 * Copyright (c) 2020 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef _CAN_ISOTP_H
#define _CAN_ISOTP_H

#include <linux/types.h>
#include <linux/can.h>

#define SOL_CAN_ISOTP (SOL_CAN_BASE + CAN_ISOTP)

#define CAN_ISOTP_OPTS		1

#define CAN_ISOTP_RECV_FC	2

#define CAN_ISOTP_TX_STMIN	3

#define CAN_ISOTP_RX_STMIN	4

#define CAN_ISOTP_LL_OPTS	5

struct can_isotp_options {

	__u32 flags;

	__u32 frame_txtime;

	__u8  ext_address;

	__u8  txpad_content;

	__u8  rxpad_content;

	__u8  rx_ext_address;

};

struct can_isotp_fc_options {

	__u8  bs;

	__u8  stmin;

	__u8  wftmax;

};

struct can_isotp_ll_options {

	__u8  mtu;

	__u8  tx_dl;

	__u8  tx_flags;

};

#define CAN_ISOTP_LISTEN_MODE	0x0001
#define CAN_ISOTP_EXTEND_ADDR	0x0002
#define CAN_ISOTP_TX_PADDING	0x0004
#define CAN_ISOTP_RX_PADDING	0x0008
#define CAN_ISOTP_CHK_PAD_LEN	0x0010
#define CAN_ISOTP_CHK_PAD_DATA	0x0020
#define CAN_ISOTP_HALF_DUPLEX	0x0040
#define CAN_ISOTP_FORCE_TXSTMIN	0x0080
#define CAN_ISOTP_FORCE_RXSTMIN	0x0100
#define CAN_ISOTP_RX_EXT_ADDR	0x0200
#define CAN_ISOTP_WAIT_TX_DONE	0x0400
#define CAN_ISOTP_SF_BROADCAST	0x0800
#define CAN_ISOTP_CF_BROADCAST	0x1000
#define CAN_ISOTP_DYN_FC_PARMS	0x2000

#define CAN_ISOTP_DEFAULT_FLAGS		0
#define CAN_ISOTP_DEFAULT_EXT_ADDRESS	0x00
#define CAN_ISOTP_DEFAULT_PAD_CONTENT	0xCC
#define CAN_ISOTP_DEFAULT_FRAME_TXTIME	50000
#define CAN_ISOTP_DEFAULT_RECV_BS	0
#define CAN_ISOTP_DEFAULT_RECV_STMIN	0x00
#define CAN_ISOTP_DEFAULT_RECV_WFTMAX	0

#define CAN_ISOTP_DEFAULT_LL_MTU	CAN_MTU
#define CAN_ISOTP_DEFAULT_LL_TX_DL	CAN_MAX_DLEN
#define CAN_ISOTP_DEFAULT_LL_TX_FLAGS	0

#define CAN_ISOTP_FRAME_TXTIME_ZERO	0xFFFFFFFF

#endif
