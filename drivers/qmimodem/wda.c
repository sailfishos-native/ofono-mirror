/*
 * oFono - Open Source Telephony
 * Copyright (C) 2024  Cruise, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdint.h>
#include <stddef.h>

#include <ell/ell.h>

#include "src/common.h"

#include "qmi.h"
#include "common.h"
#include "wda.h"

int qmi_wda_parse_data_format(struct qmi_result *result,
				struct qmi_wda_data_format *out_format)
{
	static const uint8_t RESULT_LL_PROTO = 0x11;
	static const uint8_t RESULT_UL_AGGREGATION_PROTOCOL = 0x12;
	static const uint8_t RESULT_DL_AGGREGATION_PROTOCOL = 0x13;
	static const uint8_t RESULT_DL_MAX_DATAGRAMS = 0x15;
	static const uint8_t RESULT_DL_MAX_SIZE = 0x16;
	struct qmi_wda_data_format format;

	if (!qmi_result_get_uint32(result, RESULT_LL_PROTO,
						&format.ll_protocol))
		return -ENOENT;

	if (!qmi_result_get_uint32(result, RESULT_UL_AGGREGATION_PROTOCOL,
					&format.ul_aggregation_protocol))
		return -ENOENT;

	if (!qmi_result_get_uint32(result, RESULT_DL_AGGREGATION_PROTOCOL,
					&format.dl_aggregation_protocol))
		return -ENOENT;

	if (!qmi_result_get_uint32(result, RESULT_DL_MAX_DATAGRAMS,
					&format.dl_max_datagrams))
		return -ENOENT;

	if (!qmi_result_get_uint32(result, RESULT_DL_MAX_SIZE,
					&format.dl_max_size))
		return -ENOENT;

	if (out_format)
		memcpy(out_format, &format, sizeof(struct qmi_wda_data_format));

	return 0;
}

uint16_t qmi_wda_set_data_format(struct qmi_service *wda,
				const struct qmi_endpoint_info *endpoint_info,
				const struct qmi_wda_data_format *format,
				qmi_service_result_func_t func,
				void *user_data, qmi_destroy_func_t destroy)
{
	static const uint8_t PARAM_LL_PROTO = 0x11;
	static const uint8_t PARAM_UL_AGGREGATION_PROTOCOL = 0x12;
	static const uint8_t PARAM_DL_AGGREGATION_PROTOCOL = 0x13;
	static const uint8_t PARAM_DL_MAX_DATAGRAMS = 0x15;
	static const uint8_t PARAM_DL_MAX_SIZE = 0x16;
	static const uint8_t PARAM_EP_INFO = 0x17;
	struct qmi_param *param = qmi_param_new();
	uint32_t req_id;

	qmi_param_append_uint32(param, PARAM_LL_PROTO, format->ll_protocol);
	qmi_param_append_uint32(param, PARAM_UL_AGGREGATION_PROTOCOL,
					format->ul_aggregation_protocol);
	qmi_param_append_uint32(param, PARAM_DL_AGGREGATION_PROTOCOL,
					format->dl_aggregation_protocol);
	qmi_param_append_uint32(param, PARAM_DL_MAX_DATAGRAMS,
					format->dl_max_datagrams);
	qmi_param_append_uint32(param, PARAM_DL_MAX_SIZE, format->dl_max_size);
	qmi_param_append(param, PARAM_EP_INFO,
				sizeof(*endpoint_info), endpoint_info);

	req_id = qmi_service_send(wda, QMI_WDA_SET_DATA_FORMAT,
					param, func, user_data, destroy);
	if (!req_id)
		qmi_param_free(param);

	return req_id;
}
