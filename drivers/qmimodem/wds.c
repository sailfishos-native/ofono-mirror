/*
 * oFono - Open Source Telephony
 * Copyright (C) 2024  Cruise, LLC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

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
