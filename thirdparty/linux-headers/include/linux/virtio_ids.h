#ifndef _LINUX_VIRTIO_IDS_H
#define _LINUX_VIRTIO_IDS_H
/*
 * Virtio IDs
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#define VIRTIO_ID_NET			1
#define VIRTIO_ID_BLOCK			2
#define VIRTIO_ID_CONSOLE		3
#define VIRTIO_ID_RNG			4
#define VIRTIO_ID_BALLOON		5
#define VIRTIO_ID_IOMEM			6
#define VIRTIO_ID_RPMSG			7
#define VIRTIO_ID_SCSI			8
#define VIRTIO_ID_9P			9
#define VIRTIO_ID_MAC80211_WLAN		10
#define VIRTIO_ID_RPROC_SERIAL		11
#define VIRTIO_ID_CAIF			12
#define VIRTIO_ID_MEMORY_BALLOON	13
#define VIRTIO_ID_GPU			16
#define VIRTIO_ID_CLOCK			17
#define VIRTIO_ID_INPUT			18
#define VIRTIO_ID_VSOCK			19
#define VIRTIO_ID_CRYPTO		20
#define VIRTIO_ID_SIGNAL_DIST		21
#define VIRTIO_ID_PSTORE		22
#define VIRTIO_ID_IOMMU			23
#define VIRTIO_ID_MEM			24
#define VIRTIO_ID_SOUND			25
#define VIRTIO_ID_FS			26
#define VIRTIO_ID_PMEM			27
#define VIRTIO_ID_RPMB			28
#define VIRTIO_ID_MAC80211_HWSIM	29
#define VIRTIO_ID_VIDEO_ENCODER		30
#define VIRTIO_ID_VIDEO_DECODER		31
#define VIRTIO_ID_SCMI			32
#define VIRTIO_ID_NITRO_SEC_MOD		33
#define VIRTIO_ID_I2C_ADAPTER		34
#define VIRTIO_ID_WATCHDOG		35
#define VIRTIO_ID_CAN			36
#define VIRTIO_ID_DMABUF		37
#define VIRTIO_ID_PARAM_SERV		38
#define VIRTIO_ID_AUDIO_POLICY		39
#define VIRTIO_ID_BT			40
#define VIRTIO_ID_GPIO			41
#define VIRTIO_ID_SPI			45

#define VIRTIO_TRANS_ID_NET		0x1000
#define VIRTIO_TRANS_ID_BLOCK		0x1001
#define VIRTIO_TRANS_ID_BALLOON		0x1002
#define VIRTIO_TRANS_ID_CONSOLE		0x1003
#define VIRTIO_TRANS_ID_SCSI		0x1004
#define VIRTIO_TRANS_ID_RNG		0x1005
#define VIRTIO_TRANS_ID_9P		0x1009

#endif
