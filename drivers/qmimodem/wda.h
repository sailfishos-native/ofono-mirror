/*
 * oFono - Open Source Telephony
 * Copyright (C) 2017  Kerlink SA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#define QMI_WDA_SET_DATA_FORMAT	32	/* Set data format */
#define QMI_WDA_GET_DATA_FORMAT	33	/* Get data format */

/* Get and set data format interface */
#define QMI_WDA_LL_PROTOCOL	0x11	/* uint32_t */

enum qmi_wda_data_link_protocol {
	QMI_WDA_DATA_LINK_PROTOCOL_UNKNOWN =			0x00,
	QMI_WDA_DATA_LINK_PROTOCOL_802_3 =			0x01,
	QMI_WDA_DATA_LINK_PROTOCOL_RAW_IP =			0x02,
};
