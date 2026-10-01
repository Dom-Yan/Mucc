/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * include/linux/v4l2-common.h
 *
 * Common V4L2 and V4L2 subdev definitions.
 *
 * Users are advised to #include this file either through videodev2.h
 * (V4L2) or through v4l2-subdev.h (V4L2 subdev) rather than to refer
 * to this file directly.
 *
 * Copyright (C) 2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef __V4L2_COMMON__
#define __V4L2_COMMON__

#include <linux/types.h>

#define V4L2_SEL_TGT_CROP		0x0000

#define V4L2_SEL_TGT_CROP_DEFAULT	0x0001

#define V4L2_SEL_TGT_CROP_BOUNDS	0x0002

#define V4L2_SEL_TGT_NATIVE_SIZE	0x0003

#define V4L2_SEL_TGT_COMPOSE		0x0100

#define V4L2_SEL_TGT_COMPOSE_DEFAULT	0x0101

#define V4L2_SEL_TGT_COMPOSE_BOUNDS	0x0102

#define V4L2_SEL_TGT_COMPOSE_PADDED	0x0103

#define V4L2_SEL_FLAG_GE		(1 << 0)
#define V4L2_SEL_FLAG_LE		(1 << 1)
#define V4L2_SEL_FLAG_KEEP_CONFIG	(1 << 2)

struct v4l2_edid {
	__u32 pad;
	__u32 start_block;
	__u32 blocks;
	__u32 reserved[5];
	__u8  *edid;
};

#define V4L2_SEL_TGT_CROP_ACTIVE	V4L2_SEL_TGT_CROP
#define V4L2_SEL_TGT_COMPOSE_ACTIVE	V4L2_SEL_TGT_COMPOSE
#define V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL	V4L2_SEL_TGT_CROP
#define V4L2_SUBDEV_SEL_TGT_COMPOSE_ACTUAL V4L2_SEL_TGT_COMPOSE
#define V4L2_SUBDEV_SEL_TGT_CROP_BOUNDS	V4L2_SEL_TGT_CROP_BOUNDS
#define V4L2_SUBDEV_SEL_TGT_COMPOSE_BOUNDS V4L2_SEL_TGT_COMPOSE_BOUNDS

#define V4L2_SUBDEV_SEL_FLAG_SIZE_GE	V4L2_SEL_FLAG_GE
#define V4L2_SUBDEV_SEL_FLAG_SIZE_LE	V4L2_SEL_FLAG_LE
#define V4L2_SUBDEV_SEL_FLAG_KEEP_CONFIG V4L2_SEL_FLAG_KEEP_CONFIG

#endif
