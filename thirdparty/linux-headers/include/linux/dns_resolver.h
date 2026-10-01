/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* DNS resolver interface definitions.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_DNS_RESOLVER_H
#define _LINUX_DNS_RESOLVER_H

#include <linux/types.h>

enum dns_payload_content_type {
	DNS_PAYLOAD_IS_SERVER_LIST	= 0,
};

enum dns_payload_address_type {
	DNS_ADDRESS_IS_IPV4		= 0,
	DNS_ADDRESS_IS_IPV6		= 1,
};

enum dns_payload_protocol_type {
	DNS_SERVER_PROTOCOL_UNSPECIFIED	= 0,
	DNS_SERVER_PROTOCOL_UDP		= 1,
	DNS_SERVER_PROTOCOL_TCP		= 2,
};

enum dns_record_source {
	DNS_RECORD_UNAVAILABLE		= 0,
	DNS_RECORD_FROM_CONFIG		= 1,
	DNS_RECORD_FROM_DNS_A		= 2,
	DNS_RECORD_FROM_DNS_AFSDB	= 3,
	DNS_RECORD_FROM_DNS_SRV		= 4,
	DNS_RECORD_FROM_NSS		= 5,
	NR__dns_record_source
};

enum dns_lookup_status {
	DNS_LOOKUP_NOT_DONE		= 0,
	DNS_LOOKUP_GOOD			= 1,
	DNS_LOOKUP_GOOD_WITH_BAD	= 2,
	DNS_LOOKUP_BAD			= 3,
	DNS_LOOKUP_GOT_NOT_FOUND	= 4,
	DNS_LOOKUP_GOT_LOCAL_FAILURE	= 5,
	DNS_LOOKUP_GOT_TEMP_FAILURE	= 6,
	DNS_LOOKUP_GOT_NS_FAILURE	= 7,
	NR__dns_lookup_status
};

struct dns_payload_header {
	__u8		zero;
	__u8		content;
	__u8		version;
} __attribute__((packed));

struct dns_server_list_v1_header {
	struct dns_payload_header hdr;
	__u8		source;
	__u8		status;
	__u8		nr_servers;
} __attribute__((packed));

struct dns_server_list_v1_server {
	__u16		name_len;
	__u16		priority;
	__u16		weight;
	__u16		port;
	__u8		source;
	__u8		status;
	__u8		protocol;
	__u8		nr_addrs;
} __attribute__((packed));

struct dns_server_list_v1_address {
	__u8		address_type;
} __attribute__((packed));

#endif
