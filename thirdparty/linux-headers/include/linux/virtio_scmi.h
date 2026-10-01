/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Copyright (C) 2020-2021 OpenSynergy GmbH
 * Copyright (C) 2021 ARM Ltd.
 */

#ifndef _LINUX_VIRTIO_SCMI_H
#define _LINUX_VIRTIO_SCMI_H

#include <linux/virtio_types.h>

#define VIRTIO_SCMI_F_P2A_CHANNELS 0

#define VIRTIO_SCMI_F_SHARED_MEMORY 1

#define VIRTIO_SCMI_VQ_TX 0
#define VIRTIO_SCMI_VQ_RX 1
#define VIRTIO_SCMI_VQ_MAX_CNT 2

#endif
