/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Controls header for NPCM video driver
 *
 * Copyright (C) 2022 Nuvoton Technologies
 */

#ifndef _LINUX_NPCM_VIDEO_H
#define _LINUX_NPCM_VIDEO_H

#include <linux/v4l2-controls.h>

#define V4L2_CID_NPCM_CAPTURE_MODE	(V4L2_CID_USER_NPCM_BASE + 0)

enum v4l2_npcm_capture_mode {
	V4L2_NPCM_CAPTURE_MODE_COMPLETE	= 0,
	V4L2_NPCM_CAPTURE_MODE_DIFF	= 1,
};

#define V4L2_CID_NPCM_RECT_COUNT	(V4L2_CID_USER_NPCM_BASE + 1)

#endif
