/*
 * oFono - Open Source Telephony
 * Copyright (C) 2017  Intel Corporation
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/types.h>

/* MBIM v1.0, Section 6.4: MBIM Functional Descriptor */
struct mbim_desc {
	uint8_t bFunctionLength;
	uint8_t bDescriptorType;
	uint8_t bDescriptorSubtype;
	__le16 bcdMBIMVersion;
	__le16 wMaxControlMessage;
	uint8_t bNumberFilters;
	uint8_t bMaxFilterSize;
	__le16 wMaxSegmentSize;
	uint8_t bmNetworkCapabilities;
} __attribute__ ((packed));

/* MBIM v1.0, Section 6.5: MBIM Extended Functional Descriptor */
struct mbim_extended_desc {
	uint8_t	bFunctionLength;
	uint8_t	bDescriptorType;
	uint8_t bDescriptorSubtype;
	__le16 bcdMBIMExtendedVersion;
	uint8_t bMaxOutstandingCommandMessages;
	__le16 wMTU;
} __attribute__ ((packed));

bool mbim_find_descriptors(const uint8_t *data, size_t data_len,
				const struct mbim_desc **out_desc,
				const struct mbim_extended_desc **out_ext_desc);
