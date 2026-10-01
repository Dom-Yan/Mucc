/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _IIO_BUFFER_H_
#define _IIO_BUFFER_H_

#include <linux/types.h>

#define IIO_BUFFER_DMABUF_CYCLIC		(1 << 0)
#define IIO_BUFFER_DMABUF_SUPPORTED_FLAGS	0x00000001

struct iio_dmabuf {
	__u32 fd;
	__u32 flags;
	__u64 bytes_used;
};

#define IIO_BUFFER_GET_FD_IOCTL			_IOWR('i', 0x91, int)
#define IIO_BUFFER_DMABUF_ATTACH_IOCTL		_IOW('i', 0x92, int)
#define IIO_BUFFER_DMABUF_DETACH_IOCTL		_IOW('i', 0x93, int)
#define IIO_BUFFER_DMABUF_ENQUEUE_IOCTL		_IOW('i', 0x94, struct iio_dmabuf)

#endif
