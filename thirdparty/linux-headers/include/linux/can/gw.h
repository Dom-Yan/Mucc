/* SPDX-License-Identifier: ((GPL-2.0-only WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * linux/can/gw.h
 *
 * Definitions for CAN frame Gateway/Router/Bridge
 *
 * Author: Oliver Hartkopp <oliver.hartkopp@volkswagen.de>
 * Copyright (c) 2011 Volkswagen Group Electronic Research
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

#ifndef _CAN_GW_H
#define _CAN_GW_H

#include <linux/types.h>
#include <linux/can.h>

struct rtcanmsg {
	__u8  can_family;
	__u8  gwtype;
	__u16 flags;
};

enum {
	CGW_TYPE_UNSPEC,
	CGW_TYPE_CAN_CAN,
	__CGW_TYPE_MAX
};

#define CGW_TYPE_MAX (__CGW_TYPE_MAX - 1)

enum {
	CGW_UNSPEC,
	CGW_MOD_AND,
	CGW_MOD_OR,
	CGW_MOD_XOR,
	CGW_MOD_SET,
	CGW_CS_XOR,
	CGW_CS_CRC8,
	CGW_HANDLED,
	CGW_DROPPED,
	CGW_SRC_IF,
	CGW_DST_IF,
	CGW_FILTER,
	CGW_DELETED,
	CGW_LIM_HOPS,
	CGW_MOD_UID,
	CGW_FDMOD_AND,
	CGW_FDMOD_OR,
	CGW_FDMOD_XOR,
	CGW_FDMOD_SET,
	__CGW_MAX
};

#define CGW_MAX (__CGW_MAX - 1)

#define CGW_FLAGS_CAN_ECHO 0x01
#define CGW_FLAGS_CAN_SRC_TSTAMP 0x02
#define CGW_FLAGS_CAN_IIF_TX_OK 0x04
#define CGW_FLAGS_CAN_FD 0x08

#define CGW_MOD_FUNCS 4

#define CGW_MOD_ID	0x01
#define CGW_MOD_DLC	0x02
#define CGW_MOD_LEN	CGW_MOD_DLC
#define CGW_MOD_DATA	0x04
#define CGW_MOD_FLAGS	0x08

#define CGW_FRAME_MODS 4

#define MAX_MODFUNCTIONS (CGW_MOD_FUNCS * CGW_FRAME_MODS)

struct cgw_frame_mod {
	struct can_frame cf;
	__u8 modtype;
} __attribute__((packed));

struct cgw_fdframe_mod {
	struct canfd_frame cf;
	__u8 modtype;
} __attribute__((packed));

#define CGW_MODATTR_LEN sizeof(struct cgw_frame_mod)
#define CGW_FDMODATTR_LEN sizeof(struct cgw_fdframe_mod)

struct cgw_csum_xor {
	__s8 from_idx;
	__s8 to_idx;
	__s8 result_idx;
	__u8 init_xor_val;
} __attribute__((packed));

struct cgw_csum_crc8 {
	__s8 from_idx;
	__s8 to_idx;
	__s8 result_idx;
	__u8 init_crc_val;
	__u8 final_xor_val;
	__u8 crctab[256];
	__u8 profile;
	__u8 profile_data[20];
} __attribute__((packed));

#define CGW_CS_XOR_LEN  sizeof(struct cgw_csum_xor)
#define CGW_CS_CRC8_LEN  sizeof(struct cgw_csum_crc8)

enum {
	CGW_CRC8PRF_UNSPEC,
	CGW_CRC8PRF_1U8,
	CGW_CRC8PRF_16U8,
	CGW_CRC8PRF_SFFID_XOR,
	__CGW_CRC8PRF_MAX
};

#define CGW_CRC8PRF_MAX (__CGW_CRC8PRF_MAX - 1)

#endif
