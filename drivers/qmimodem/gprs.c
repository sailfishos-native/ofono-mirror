/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2011-2012  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/gprs.h>

#include "qmi.h"
#include "nas.h"
#include "wds.h"

#include "src/common.h"
#include "util.h"

struct gprs_data {
	struct qmi_device *dev;
	struct qmi_service *nas;
	struct qmi_service *wds;
	unsigned int default_profile;
};

static bool extract_ss_info(struct qmi_result *result, int *status, int *tech)
{
	const struct qmi_nas_serving_system *ss;
	uint16_t len;
	int i;

	DBG("");

	ss = qmi_result_get(result, QMI_NAS_RESULT_SERVING_SYSTEM, &len);
	if (!ss)
		return false;

	if (ss->ps_state == QMI_NAS_ATTACH_STATE_ATTACHED)
		*status = NETWORK_REGISTRATION_STATUS_REGISTERED;
	else
		*status = NETWORK_REGISTRATION_STATUS_NOT_REGISTERED;

	*tech = -1;
	for (i = 0; i < ss->radio_if_count; i++) {
		DBG("radio in use %d", ss->radio_if[i]);

		*tech = qmi_nas_rat_to_tech(ss->radio_if[i]);
	}

	return true;
}

static void get_lte_attach_param_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_gprs *gprs = user_data;
	struct gprs_data *data = ofono_gprs_get_data(gprs);
	char *apn = NULL;
	uint16_t error;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		ofono_error("get_lte_attach_param_cb: %hd", error);
		goto noapn;
	}

	/* APN */
	apn = qmi_result_get_string(result, 0x10);
	if (!apn) {
		DBG("Default profile has no APN setting");
		goto noapn;
	}

	ofono_gprs_cid_activated(gprs, data->default_profile, apn);
	l_free(apn);

	return;

noapn:
	ofono_error("LTE bearer established but APN not set");
}

/*
 * Query the settings in effect on the default bearer.  These may be
 * implicit or may even be something other than requested as the gateway
 * is allowed to override whatever was requested by the user.
 */
static void get_lte_attach_params(struct ofono_gprs* gprs)
{
	struct gprs_data *data = ofono_gprs_get_data(gprs);

	DBG("");

	if (qmi_service_send(data->wds, QMI_WDS_GET_LTE_ATTACH_PARAMETERS,
				NULL, get_lte_attach_param_cb, gprs, NULL) > 0)
		return;
}

static int handle_ss_info(struct qmi_result *result, struct ofono_gprs *gprs)
{
	int status;
	int tech;

	DBG("");

	if (!extract_ss_info(result, &status, &tech))
		return -1;

	return status;
}

static void ss_info_notify(struct qmi_result *result, void *user_data)
{
	struct ofono_gprs *gprs = user_data;
	int status;

	DBG("");

	status = handle_ss_info(result, gprs);

	if (status >= 0)
		ofono_gprs_status_notify(gprs, status);
}

static void event_report_notify(struct qmi_result *result, void *user_data)
{
	static const uint8_t RESULT_DATA_SYSTEM_STATUS = 0x24;
	static const uint8_t RESULT_EXTENDED_DATA_BEARER_TECHNOLOGY = 0x2a;
	struct ofono_gprs *gprs = user_data;
	const void *tlv;
	uint16_t len;

	DBG("");

	/*
	 * On LTE we are effectively always attached; and the default bearer is
	 * established as soon as the network is joined.  We just need to query
	 * the parameters in effect on the default bearer and let the ofono core
	 * know about the activated context.
	 */
	tlv = qmi_result_get(result, RESULT_DATA_SYSTEM_STATUS, &len);
	if (tlv) {
		int r = qmi_wds_parse_data_system_status(tlv, len);
		static const uint32_t lte_5g = QMI_WDS_RAT_3GPP_LTE |
						QMI_WDS_RAT_3GPP_5GNR;

		if (r >= 0 && (r & lte_5g))
			get_lte_attach_params(gprs);

		return;
	}

	tlv = qmi_result_get(result,
				RESULT_EXTENDED_DATA_BEARER_TECHNOLOGY, &len);
	if (tlv) {
		int r = qmi_wds_parse_extended_data_bearer_technology(tlv, len);

		if (r < 0) {
			ofono_warn("extended_data_bearer_technology: %s(%d)",
					strerror(-r), r);
			return;
		}

		ofono_gprs_bearer_notify(gprs, r);
		return;
	}

	qmi_result_print_tlvs(result);
}

static void profile_changed_notify(struct qmi_result *result, void *user_data)
{
	DBG("");

	qmi_result_print_tlvs(result);
}

static void attach_detach_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_gprs_cb_t cb = cbd->cb;
	uint16_t error;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		if (error == 26) {
			/* no effect */
			goto done;
		}

		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

done:
	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void qmi_set_attached(struct ofono_gprs *gprs, int attached,
					ofono_gprs_cb_t cb, void *user_data)
{
	struct gprs_data *data = ofono_gprs_get_data(gprs);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	struct qmi_param *param;
	uint8_t action;

	DBG("attached %d", attached);

	if (attached)
		action = QMI_NAS_ATTACH_ACTION_ATTACH;
	else
		action = QMI_NAS_ATTACH_ACTION_DETACH;

	param = qmi_param_new_uint8(QMI_NAS_PARAM_ATTACH_ACTION, action);

	if (qmi_service_send(data->nas, QMI_NAS_ATTACH_DETACH, param,
					attach_detach_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);
	CALLBACK_WITH_FAILURE(cb, cbd->data);
	l_free(cbd);
}

static void get_ss_info_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_gprs *gprs = cbd->user;
	ofono_gprs_status_cb_t cb = cbd->cb;
	int status;

	DBG("");

	if (qmi_result_set_error(result, NULL))
		goto error;

	status = handle_ss_info(result, gprs);

	if (status < 0)
		goto error;

	CALLBACK_WITH_SUCCESS(cb, status, cbd->data);

	return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
}

static void qmi_attached_status(struct ofono_gprs *gprs,
				ofono_gprs_status_cb_t cb, void *user_data)
{
	struct gprs_data *data = ofono_gprs_get_data(gprs);
	struct cb_data *cbd = cb_data_new(cb, user_data);

	DBG("");

	cbd->user = gprs;
	if (qmi_service_send(data->nas, QMI_NAS_GET_SERVING_SYSTEM, NULL,
					get_ss_info_cb, cbd, l_free) > 0)
		return;

	CALLBACK_WITH_FAILURE(cb, -1, cbd->data);

	l_free(cbd);
}

static void indication_register_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_gprs *gprs = user_data;
	struct gprs_data *data = ofono_gprs_get_data(gprs);
	uint16_t error;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		ofono_error("indication_register_cb: %hd", error);
		goto error;
	}

	/*
	 * First get the SS info - the modem may already be connected,
	 * and the state-change notification may never arrive
	 */
	qmi_service_send(data->nas, QMI_NAS_GET_SERVING_SYSTEM, NULL,
					ss_info_notify, gprs, NULL);

	ofono_gprs_register(gprs);
	return;
error:
	ofono_gprs_remove(gprs);
}

static int indication_register_request(struct ofono_gprs *gprs)
{
	static const uint8_t PARAM_PROFILE_CHANGES = 0x19;

	struct gprs_data *data = ofono_gprs_get_data(gprs);
	struct qmi_param *param = qmi_param_new();

	qmi_param_append_uint8(param, PARAM_PROFILE_CHANGES, 1);

	if (qmi_service_send(data->wds, QMI_WDS_INDICATION_REGISTER,
				param, indication_register_cb, gprs, NULL) > 0)
		return 0;

	qmi_param_free(param);
	return -EIO;
}

static void set_event_report_cb(struct qmi_result *result, void *user_data)
{
	struct ofono_gprs *gprs = user_data;
	uint16_t error;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		ofono_error("set_event_report_cb: %hd", error);
		goto error;
	}

	if (indication_register_request(gprs) >= 0)
		return;
error:
	ofono_gprs_remove(gprs);
}

static int set_event_report_request(struct ofono_gprs *gprs)
{
	static const uint8_t PARAM_CHANNEL_RATE = 0x10;
	static const uint8_t PARAM_TRANSFER_STATISTICS = 0x11;
	static const uint8_t PARAM_DATA_BEARER_TECHNOLOGY = 0x12;
	static const uint8_t PARAM_DORMANCY_STATUS = 0x13;
	static const uint8_t PARAM_CURRENT_DATA_BEARER_TECHNOLOGY = 0x15;
	static const uint8_t PARAM_PREFERRED_DATA_SYSTEM = 0x18;
	static const uint8_t PARAM_DATA_SYSTEM_STATUS = 0x1A;
	static const uint8_t PARAM_LIMITED_DATA_SYSTEM_STATUS = 0x1C;
	static const uint8_t PARAM_PDN_FILTER_REMOVALS = 0x1D;
	static const uint8_t PARAM_DATA_BEARER_TECHNOLOGY_EXTENDED = 0x1E;

	struct gprs_data *data = ofono_gprs_get_data(gprs);
	struct {
		uint8_t interval;
		uint32_t indicators;
	} __attribute((packed)) ts = {
		.interval = 5,
		.indicators = ~0, /* register for everything */
	};
	struct qmi_param *param = qmi_param_new();

	qmi_param_append_uint8(param, PARAM_CHANNEL_RATE, 1);
	qmi_param_append(param, PARAM_TRANSFER_STATISTICS, sizeof(ts), &ts);
	qmi_param_append_uint8(param, PARAM_DATA_BEARER_TECHNOLOGY, 1);
	qmi_param_append_uint8(param, PARAM_DORMANCY_STATUS, 1);
	qmi_param_append_uint8(param, PARAM_CURRENT_DATA_BEARER_TECHNOLOGY, 1);
	qmi_param_append_uint8(param, PARAM_PREFERRED_DATA_SYSTEM, 1);
	qmi_param_append_uint8(param, PARAM_DATA_SYSTEM_STATUS, 1);
	qmi_param_append_uint8(param, PARAM_LIMITED_DATA_SYSTEM_STATUS, 1);
	qmi_param_append_uint8(param, PARAM_PDN_FILTER_REMOVALS, 1);
	qmi_param_append_uint8(param, PARAM_DATA_BEARER_TECHNOLOGY_EXTENDED, 1);

	if (qmi_service_send(data->wds, QMI_WDS_EVENT_REPORT,
				param, set_event_report_cb, gprs, NULL) > 0)
		return 0;

	qmi_param_free(param);
	return -EIO;
}

static void get_default_profile_number_cb(struct qmi_result *result,
								void *user_data)
{
	static const uint8_t RESULT_DEFAULT_PROFILE_NUMBER = 0x1;
	struct ofono_gprs *gprs = user_data;
	struct gprs_data *data = ofono_gprs_get_data(gprs);
	uint16_t error;
	uint8_t index;

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
	data->default_profile = index;
	ofono_gprs_set_cid_range(gprs, index, index);

	if (set_event_report_request(gprs) >= 0)
		return;
error:
	ofono_gprs_remove(gprs);
}

static int get_default_profile_number_request(struct ofono_gprs *gprs)
{
	struct gprs_data *data = ofono_gprs_get_data(gprs);
	struct {
		uint8_t type;
		uint8_t family;
	} __attribute((packed)) p = {
		.type = QMI_WDS_PROFILE_TYPE_3GPP,
		.family = QMI_WDS_PROFILE_FAMILY_EMBEDDED,
	};
	struct qmi_param *param = qmi_param_new();

	/*
	 * Query the default profile.  We never change the default profile
	 * number, so querying it once should be sufficient
	 */
	qmi_param_append(param, QMI_WDS_PARAM_PROFILE_TYPE, sizeof(p), &p);

	if (qmi_service_send(data->wds, QMI_WDS_GET_DEFAULT_PROFILE_NUMBER,
				param, get_default_profile_number_cb,
				gprs, NULL) > 0)
		return 0;

	qmi_param_free(param);
	return -EIO;
}

static void create_wds_cb(struct qmi_service *service, void *user_data)
{
	struct ofono_gprs *gprs = user_data;
	struct gprs_data *data = ofono_gprs_get_data(gprs);

	DBG("");

	if (!service) {
		ofono_error("Failed to request WDS service");
		goto error;
	}

	data->wds = service;
	qmi_service_register(data->wds, QMI_WDS_EVENT_REPORT,
				event_report_notify, gprs, NULL);
	qmi_service_register(data->wds, QMI_WDS_PROFILE_CHANGED,
				profile_changed_notify, gprs, NULL);

	if (get_default_profile_number_request(gprs) >= 0)
		return;
error:
	ofono_gprs_remove(gprs);
}

static void create_nas_cb(struct qmi_service *service, void *user_data)
{
	struct ofono_gprs *gprs = user_data;
	struct gprs_data *data = ofono_gprs_get_data(gprs);

	DBG("");

	if (!service) {
		ofono_error("Failed to request NAS service");
		ofono_gprs_remove(gprs);
		return;
	}

	data->nas = service;
	qmi_service_register(data->nas, QMI_NAS_SERVING_SYSTEM_INDICATION,
					ss_info_notify, gprs, NULL);

	qmi_service_create_shared(data->dev, QMI_SERVICE_WDS,
						create_wds_cb, gprs, NULL);
}

static int qmi_gprs_probe(struct ofono_gprs *gprs,
				unsigned int vendor, void *user_data)
{
	struct qmi_device *device = user_data;
	struct gprs_data *data;

	DBG("");

	data = l_new(struct gprs_data, 1);

	ofono_gprs_set_data(gprs, data);

	data->dev = device;

	qmi_service_create_shared(device, QMI_SERVICE_NAS,
						create_nas_cb, gprs, NULL);

	return 0;
}

static void qmi_gprs_remove(struct ofono_gprs *gprs)
{
	struct gprs_data *data = ofono_gprs_get_data(gprs);

	DBG("");

	ofono_gprs_set_data(gprs, NULL);

	qmi_service_free(data->wds);
	qmi_service_free(data->nas);

	l_free(data);
}

static const struct ofono_gprs_driver driver = {
	.probe			= qmi_gprs_probe,
	.remove			= qmi_gprs_remove,
	.set_attached		= qmi_set_attached,
	.attached_status	= qmi_attached_status,
};

OFONO_ATOM_DRIVER_BUILTIN(gprs, qmimodem, &driver)
