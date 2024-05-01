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

#include "wds.h"

int qmi_wds_auth_from_ofono(enum ofono_gprs_auth_method method)
{
	/* QMI uses a bitmap */
	switch (method) {
	case OFONO_GPRS_AUTH_METHOD_CHAP:
		return QMI_WDS_AUTHENTICATION_CHAP;
	case OFONO_GPRS_AUTH_METHOD_PAP:
		return QMI_WDS_AUTHENTICATION_PAP;
	case OFONO_GPRS_AUTH_METHOD_NONE:
		return 0;
	}

	return -ENOENT;
}

int qmi_wds_pdp_type_from_ofono(enum ofono_gprs_proto proto)
{
	switch (proto) {
	case OFONO_GPRS_PROTO_IP:
		return QMI_WDS_PDP_TYPE_IPV4;
	case OFONO_GPRS_PROTO_IPV6:
		return QMI_WDS_PDP_TYPE_IPV6;
	case OFONO_GPRS_PROTO_IPV4V6:
		return QMI_WDS_PDP_TYPE_IPV4V6;
	}

	return -ENOENT;
}

int qmi_wds_parse_data_system_status(const void *dss, uint16_t len)
{
	const size_t network_info_size = sizeof(uint8_t) + 2 * sizeof(uint32_t);
	uint8_t num_networks;
	uint8_t network;
	uint32_t rat_mask;

	if (len < 2 * sizeof(uint8_t))
		return -EBADMSG;

	/* uint8_t preferred network type followed by number of network infos */
	num_networks = l_get_u8(dss + 1);

	len -= 2 * sizeof(uint8_t);
	dss += 2 * sizeof(uint8_t);

	if (len != num_networks * network_info_size)
		return -EBADMSG;

	while (len >= network_info_size) {
		network = l_get_u8(dss);
		rat_mask = l_get_le32(dss + 1);

		if (network == QMI_WDS_PROFILE_TYPE_3GPP)
			return rat_mask;

		len -= network_info_size;
		dss += network_info_size;
	}

	return -ENOENT;
}
