/*
 * oFono - Open Source Telephony
 * Copyright (C) 2017  Kerlink SA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

struct qmi_endpoint_info;

#define QMI_WDA_SET_DATA_FORMAT	32	/* Set data format */
#define QMI_WDA_GET_DATA_FORMAT	33	/* Get data format */

/* Get and set data format interface */
#define QMI_WDA_LL_PROTOCOL	0x11	/* uint32_t */

enum qmi_wda_data_link_protocol {
	QMI_WDA_DATA_LINK_PROTOCOL_UNKNOWN =			0x00,
	QMI_WDA_DATA_LINK_PROTOCOL_802_3 =			0x01,
	QMI_WDA_DATA_LINK_PROTOCOL_RAW_IP =			0x02,
};

enum qmi_wda_aggregation_protocol {
	QMI_WDA_AGGREGATION_PROTOCOL_DISABLED =			0x00,
	QMI_WDA_AGGREGATION_PROTOCOL_QMAP =			0x05,
	QMI_WDA_AGGREGATION_PROTOCOL_QMAPV4 =			0x08,
	QMI_WDA_AGGREGATION_PROTOCOL_QMAPV5 =			0x09,
};

struct qmi_wda_data_format {
	uint32_t ll_protocol;
	uint32_t ul_aggregation_protocol;
	uint32_t dl_aggregation_protocol;
	uint32_t dl_max_datagrams;
	uint32_t dl_max_size;
};

uint16_t qmi_wda_set_data_format(struct qmi_service *wda,
				const struct qmi_endpoint_info *endpoint_info,
				const struct qmi_wda_data_format *format,
				qmi_service_result_func_t func,
				void *user_data, qmi_destroy_func_t destroy);
