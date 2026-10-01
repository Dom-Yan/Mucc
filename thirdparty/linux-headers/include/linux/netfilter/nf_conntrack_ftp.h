/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _NF_CONNTRACK_FTP_H
#define _NF_CONNTRACK_FTP_H

enum nf_ct_ftp_type {

	NF_CT_FTP_PORT,

	NF_CT_FTP_PASV,

	NF_CT_FTP_EPRT,

	NF_CT_FTP_EPSV,
};

#endif
