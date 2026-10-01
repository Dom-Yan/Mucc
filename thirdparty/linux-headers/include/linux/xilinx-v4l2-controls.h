/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Xilinx Controls Header
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __UAPI_XILINX_V4L2_CONTROLS_H__
#define __UAPI_XILINX_V4L2_CONTROLS_H__

#include <linux/v4l2-controls.h>

#define V4L2_CID_XILINX_OFFSET	0xc000
#define V4L2_CID_XILINX_BASE	(V4L2_CID_USER_BASE + V4L2_CID_XILINX_OFFSET)

#define V4L2_CID_XILINX_TPG			(V4L2_CID_USER_BASE + 0xc000)

#define V4L2_CID_XILINX_TPG_CROSS_HAIRS		(V4L2_CID_XILINX_TPG + 1)

#define V4L2_CID_XILINX_TPG_MOVING_BOX		(V4L2_CID_XILINX_TPG + 2)

#define V4L2_CID_XILINX_TPG_COLOR_MASK		(V4L2_CID_XILINX_TPG + 3)

#define V4L2_CID_XILINX_TPG_STUCK_PIXEL		(V4L2_CID_XILINX_TPG + 4)

#define V4L2_CID_XILINX_TPG_NOISE		(V4L2_CID_XILINX_TPG + 5)

#define V4L2_CID_XILINX_TPG_MOTION		(V4L2_CID_XILINX_TPG + 6)

#define V4L2_CID_XILINX_TPG_MOTION_SPEED	(V4L2_CID_XILINX_TPG + 7)

#define V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW	(V4L2_CID_XILINX_TPG + 8)

#define V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN	(V4L2_CID_XILINX_TPG + 9)

#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_START	(V4L2_CID_XILINX_TPG + 10)

#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED	(V4L2_CID_XILINX_TPG + 11)

#define V4L2_CID_XILINX_TPG_ZPLATE_VER_START	(V4L2_CID_XILINX_TPG + 12)

#define V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED	(V4L2_CID_XILINX_TPG + 13)

#define V4L2_CID_XILINX_TPG_BOX_SIZE		(V4L2_CID_XILINX_TPG + 14)

#define V4L2_CID_XILINX_TPG_BOX_COLOR		(V4L2_CID_XILINX_TPG + 15)

#define V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH	(V4L2_CID_XILINX_TPG + 16)

#define V4L2_CID_XILINX_TPG_NOISE_GAIN		(V4L2_CID_XILINX_TPG + 17)

#endif
