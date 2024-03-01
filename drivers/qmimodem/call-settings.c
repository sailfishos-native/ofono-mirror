/*
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2024 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
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

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-settings.h>

#include "qmi.h"
#include "voice.h"
#include "util.h"

struct call_settings_data {
	struct qmi_service *voice;
	uint16_t sups_ind_id;
};

static void query_status(struct ofono_call_settings *cs, uint16_t message,
				qmi_result_func_t fn,
				ofono_call_settings_status_cb_t cb, void *data)
{
	struct call_settings_data *csd = ofono_call_settings_get_data(cs);
	struct cb_data *cbd = cb_data_new(cb, data);

	DBG("");

	if (!csd)
		goto error;

	if (qmi_service_send(csd->voice, message, NULL, fn, cbd, l_free) > 0)
		return;
error:
	l_free(cbd);
	CALLBACK_WITH_FAILURE(cb, -1, data);
}

static void cw_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_settings_status_cb_t cb = cbd->cb;
	uint8_t status;

	DBG("");

	if (qmi_result_set_error(result, NULL) ||
			!qmi_result_get_uint8(result, 0x10, &status)) {
		CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
		return;
	}

	CALLBACK_WITH_SUCCESS(cb, status, cbd->data);
}

static void qmi_cw_query(struct ofono_call_settings *cs, int cls,
				ofono_call_settings_status_cb_t cb, void *data)
{
	query_status(cs, QMI_VOICE_GET_CALL_WAITING, cw_cb, cb, data);
}

static void status_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_settings_status_cb_t cb = cbd->cb;
	uint16_t len;
	const struct __attribute__((__packed__)) {
		uint8_t active;
		uint8_t provisioned;
	} *rsp;

	DBG("");

	if (qmi_result_set_error(result, NULL))
		goto error;

	rsp = qmi_result_get(result, 0x10, &len);
	if (!rsp || len != sizeof(*rsp))
		goto error;

	CALLBACK_WITH_SUCCESS(cb, rsp->provisioned, cbd->data);
	return;
error:
	CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
}

static void qmi_clip_query(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data)
{
	query_status(cs, QMI_VOICE_GET_CLIP, status_cb, cb, data);
}

static void qmi_colp_query(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data)
{
	query_status(cs, QMI_VOICE_GET_COLP, status_cb, cb, data);
}

static void qmi_colr_query(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data)
{
	query_status(cs, QMI_VOICE_GET_COLR, status_cb, cb, data);
}

static void qmi_cnap_query(struct ofono_call_settings *cs,
				ofono_call_settings_status_cb_t cb, void *data)
{
	query_status(cs, QMI_VOICE_GET_CNAP, status_cb, cb, data);
}

static void clir_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_settings_clir_cb_t cb = cbd->cb;
	uint16_t len;
	const struct __attribute__((__packed__)) {
		uint8_t active;
		uint8_t provisioned;
	} *rsp;
	uint8_t network;

	DBG("");

	if (qmi_result_set_error(result, NULL))
		goto error;

	rsp = qmi_result_get(result, 0x10, &len);
	if (!rsp || len != sizeof(*rsp))
		goto error;

	network = rsp->provisioned;
	/* we do not have UNKNOWN */
	if (network > 1)
		network++;

	CALLBACK_WITH_SUCCESS(cb, rsp->active, network, cbd->data);
	return;
error:
	CALLBACK_WITH_FAILURE(cb, -1, -1, cbd->data);
}

static void qmi_clir_query(struct ofono_call_settings *cs,
				ofono_call_settings_clir_cb_t cb, void *data)
{
	struct call_settings_data *csd = ofono_call_settings_get_data(cs);
	struct cb_data *cbd = cb_data_new(cb, data);

	DBG("");

	if (!csd)
		goto error;

	if (qmi_service_send(csd->voice, QMI_VOICE_GET_CLIR, NULL,
						clir_cb, cbd, l_free) > 0)
		return;
error:
	l_free(cbd);
	CALLBACK_WITH_FAILURE(cb, -1, -1, data);
}

static void cw_set_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_settings_set_cb_t cb = cbd->cb;

	DBG("");

	if (qmi_result_set_error(result, NULL)) {
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void qmi_cw_set(struct ofono_call_settings *cs, int mode, int cls,
				ofono_call_settings_set_cb_t cb, void *data)
{
	struct call_settings_data *csd = ofono_call_settings_get_data(cs);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	struct __attribute__((__packed__)) {
		uint8_t service;
		uint8_t reason;
	} ssd;

	DBG("");

	if (!csd)
		goto error;

	param = qmi_param_new();

	ssd.service = mode ? QMI_VOICE_SS_ACTION_ACTIVATE :
				QMI_VOICE_SS_ACTION_DEACTIVATE;
	ssd.reason = QMI_VOICE_SS_RSN_CALL_WAITING;

	qmi_param_append(param, 0x01, sizeof(ssd), &ssd);

	if (cls != 7 /* BEARER_CLASS_DEFAULT */)
		qmi_param_append_uint8(param, 0x10, cls);

	if (qmi_service_send(csd->voice, QMI_VOICE_SET_SUPS_SERVICE, param,
				cw_set_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);
error:
	l_free(cbd);
	CALLBACK_WITH_FAILURE(cb, data);
}

/* call_settings API lacks change notifications, just log data for now */
static void sups_ind(struct qmi_result *result, void *user_data)
{
	const struct __attribute__((__packed__)) {
		uint8_t request;
		uint8_t mod_cc;
	} *info;
	const struct __attribute__((__packed__)) {
		uint8_t active;
		uint8_t status;
	} *clir;
	const struct __attribute__((__packed__)) {
		uint8_t active;
		uint8_t provisioned;
	} *clip;
	uint8_t cls;
	uint8_t reason;
	uint8_t data;
	uint16_t len;

	DBG("SS notification");

	info = qmi_result_get(result, 0x01, &len);
	if (info && len == sizeof(*info))
		DBG("request %d", info->request);

	if (qmi_result_get_uint8(result, 0x10, &cls))
		DBG("class %d", cls);

	if (qmi_result_get_uint8(result, 0x11, &reason))
		DBG("reason %d", reason);

	if (qmi_result_get_uint8(result, 0x19, &data))
		DBG("data %d", data);

	clir = qmi_result_get(result, 0x1c, &len);
	if (clir && len == sizeof(*clir))
		DBG("clir active %d, status %d", clir->active, clir->status);

	clip = qmi_result_get(result, 0x1d, &len);
	if (clip && len == sizeof(*clip))
		DBG("clip active %d, provisioned %d",
					clip->active, clip->provisioned);
}

static void create_voice_cb(struct qmi_service *service, void *user_data)
{
	struct ofono_call_settings *cs = user_data;
	struct call_settings_data *csd = ofono_call_settings_get_data(cs);

	DBG("");

	if (!service) {
		ofono_error("Failed to request Voice service");
		ofono_call_settings_remove(cs);
		return;
	}

	csd->voice = qmi_service_ref(service);

	csd->sups_ind_id = qmi_service_register(csd->voice, QMI_VOICE_SUPS_IND,
						sups_ind, cs, NULL);

	ofono_call_settings_register(cs);
}

static int qmi_call_settings_probe(struct ofono_call_settings *cs,
					unsigned int vendor, void *user_data)
{
	struct qmi_device *device = user_data;
	struct call_settings_data *csd;

	DBG("");

	csd = l_new(struct call_settings_data, 1);

	ofono_call_settings_set_data(cs, csd);

	qmi_service_create_shared(device, QMI_SERVICE_VOICE,
					create_voice_cb, cs, NULL);

	return 0;
}

static void qmi_call_settings_remove(struct ofono_call_settings *cs)
{
	struct call_settings_data *csd = ofono_call_settings_get_data(cs);

	DBG("");

	ofono_call_settings_set_data(cs, NULL);

	if (csd->voice) {
		qmi_service_unregister(csd->voice, csd->sups_ind_id);
		qmi_service_unref(csd->voice);
	}

	l_free(csd);
}

static const struct ofono_call_settings_driver driver = {
	.probe			= qmi_call_settings_probe,
	.remove			= qmi_call_settings_remove,
	.clip_query		= qmi_clip_query,
	.colp_query		= qmi_colp_query,
	.colr_query		= qmi_colr_query,
	.cnap_query		= qmi_cnap_query,
	.clir_query		= qmi_clir_query,
	.cw_query		= qmi_cw_query,
	.cw_set			= qmi_cw_set
};

OFONO_ATOM_DRIVER_BUILTIN(call_settings, qmimodem, &driver)
