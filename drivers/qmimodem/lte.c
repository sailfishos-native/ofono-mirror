/*
 * oFono - Open Source Telephony
 * Copyright (C) 2018  Jonas Bonn
 * Copyright (C) 2018  Norrbonn AB
 * Copyright (C) 2018  Data Respons ASA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include <ofono/modem.h>
#include <ofono/gprs-context.h>
#include <ofono/log.h>
#include <ofono/lte.h>

#include "qmi.h"
#include "wds.h"
#include "util.h"

struct lte_data {
	struct qmi_service *wds;
	uint8_t default_profile;
};

static void modify_profile_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_lte_cb_t cb = cbd->cb;
	uint16_t error;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		DBG("Failed to modify profile: %d", error);
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void qmimodem_lte_set_default_attach_info(const struct ofono_lte *lte,
			const struct ofono_lte_default_attach_info *info,
			ofono_lte_cb_t cb, void *data)
{
	static const uint8_t PARAM_PDP_TYPE = 0x11;
	static const uint8_t PARAM_USERNAME = 0x1B;
	static const uint8_t PARAM_PASSWORD = 0x1C;
	static const uint8_t PARAM_AUTHENTICATION_PREFERENCE = 0x1D;
	struct lte_data *ldd = ofono_lte_get_data(lte);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param* param;
	struct {
		uint8_t type;
		uint8_t index;
	} __attribute__((packed)) p = {
		.type = QMI_WDS_PROFILE_TYPE_3GPP,
		.index = ldd->default_profile,
	};
	uint8_t auth = qmi_wds_auth_from_ofono(info->auth_method);

	DBG("");

	param = qmi_param_new();

	qmi_param_append(param, QMI_WDS_PARAM_PROFILE_TYPE, sizeof(p), &p);
	qmi_param_append_uint8(param, PARAM_PDP_TYPE,
				qmi_wds_pdp_type_from_ofono(info->proto));
	qmi_param_append(param, QMI_WDS_PARAM_APN,
				strlen(info->apn), info->apn);

	qmi_param_append_uint8(param, PARAM_AUTHENTICATION_PREFERENCE, auth);

	if (auth && info->username[0])
		qmi_param_append(param, PARAM_USERNAME,
					strlen(info->username), info->username);

	if (auth && info->password[0])
		qmi_param_append(param, PARAM_PASSWORD,
					strlen(info->password), info->password);

	if (qmi_service_send(ldd->wds, QMI_WDS_MODIFY_PROFILE, param,
					modify_profile_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);
	CALLBACK_WITH_FAILURE(cb, cbd->data);
	l_free(cbd);
}

static void reset_profile_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_lte *lte = user_data;
	uint16_t error;

	DBG("");

	if (qmi_result_set_error(result, &error))
		ofono_error("Reset profile error: %hd", error);

	ofono_lte_register(lte);
}

static void get_default_profile_cb(struct qmi_result *result, void *user_data)
{
	static const uint8_t RESULT_DEFAULT_PROFILE_NUMBER = 0x1;
	struct ofono_lte *lte = user_data;
	struct lte_data *ldd = ofono_lte_get_data(lte);
	uint16_t error;
	uint8_t index;
	struct qmi_param *param;
	struct {
		uint8_t type;
		uint8_t index;
	} __attribute__((packed)) p = {
		.type = QMI_WDS_PROFILE_TYPE_3GPP,
	};

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		ofono_error("Get default profile error: %hd", error);
		goto error;
	}

	/* Profile index */
	if (!qmi_result_get_uint8(result, RESULT_DEFAULT_PROFILE_NUMBER,
								&index)) {
		ofono_error("Failed query default profile");
		goto error;
	}

	DBG("Default profile index: %hhd", index);

	ldd->default_profile = index;

	p.index = index;

	param = qmi_param_new();

	/* Profile selector */
	qmi_param_append(param, QMI_WDS_PARAM_PROFILE_TYPE, sizeof(p), &p);

	/* Reset profile */
	if (qmi_service_send(ldd->wds, QMI_WDS_RESET_PROFILE, param,
				reset_profile_cb, lte, NULL) > 0)
		return;

	qmi_param_free(param);

error:
	ofono_error("Failed to reset default profile");
	ofono_lte_remove(lte);
}

static int qmimodem_lte_probe(struct ofono_lte *lte,
					unsigned int vendor, void *data)
{
	struct qmi_service *wds = data;
	struct qmi_param *param;
	struct {
		uint8_t type;
		uint8_t family;
	} __attribute((packed)) p = {
		.type = QMI_WDS_PROFILE_TYPE_3GPP,
		.family = QMI_WDS_PROFILE_FAMILY_EMBEDDED,
	};
	struct lte_data *ldd;

	DBG("");

	param = qmi_param_new();
	qmi_param_append(param, QMI_WDS_PARAM_PROFILE_TYPE, sizeof(p), &p);

	if (!qmi_service_send(wds, QMI_WDS_GET_DEFAULT_PROFILE_NUMBER,
				param, get_default_profile_cb, lte, NULL)) {
		qmi_param_free(param);
		qmi_service_free(wds);
		return -EIO;
	}

	ldd = l_new(struct lte_data, 1);
	ldd->wds = wds;

	ofono_lte_set_data(lte, ldd);

	return 0;
}

static void qmimodem_lte_remove(struct ofono_lte *lte)
{
	struct lte_data *ldd = ofono_lte_get_data(lte);

	DBG("");

	ofono_lte_set_data(lte, NULL);

	qmi_service_free(ldd->wds);
	l_free(ldd);
}

static const struct ofono_lte_driver driver = {
	.probe				= qmimodem_lte_probe,
	.remove				= qmimodem_lte_remove,
	.set_default_attach_info	= qmimodem_lte_set_default_attach_info,
};

OFONO_ATOM_DRIVER_BUILTIN(lte, qmimodem, &driver)
