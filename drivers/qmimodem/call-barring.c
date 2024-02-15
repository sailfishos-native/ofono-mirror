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

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-barring.h>

#include "qmi.h"
#include "voice.h"
#include "util.h"

struct call_barring_data {
	struct qmi_service *voice;
};

static uint8_t lock_code_to_reason(const char *lock)
{
	if (!strcmp(lock, "AO"))
		return QMI_VOICE_SS_RSN_ALL_OUTGOING;
	else if (!strcmp(lock, "OI"))
		return QMI_VOICE_SS_RSN_OUT_INT;
	else if (!strcmp(lock, "OX"))
		return QMI_VOICE_SS_RSN_OUT_INT_EXT_TO_HOME;
	else if (!strcmp(lock, "AI"))
		return QMI_VOICE_SS_RSN_ALL_IN;
	else if (!strcmp(lock, "IR"))
		return QMI_VOICE_SS_RSN_IN_ROAMING;
	else if (!strcmp(lock, "AB"))
		return QMI_VOICE_SS_RSN_BAR_ALL;
	else if (!strcmp(lock, "AG"))
		return QMI_VOICE_SS_RSN_BAR_ALL_OUTGOING;
	else if (!strcmp(lock, "AC"))
		return QMI_VOICE_SS_RSN_BAR_ALL_IN;
	else {
		DBG("Unknown lock code %s", lock);
		return 0;
	}
}

static void set_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_barring_set_cb_t cb = cbd->cb;
	uint16_t error;

	DBG("");

	if (!qmi_result_set_error(result, &error))
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
	else {
		/* Check for invalid password error */
		if (error != 92 ||
				!qmi_result_get_uint16(result, 0x10, &error) ||
				error != 129)
			CALLBACK_WITH_FAILURE(cb, cbd->data);
		else
			CALLBACK_WITH_CME_ERROR(cb, 16, cbd->data);
	}
}

static void qmi_set(struct ofono_call_barring *barr, const char *lock,
			int enable, const char *passwd, int cls,
			ofono_call_barring_set_cb_t cb, void *data)
{
	struct call_barring_data *bd = ofono_call_barring_get_data(barr);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	struct __attribute__((__packed__)) {
		uint8_t service;
		uint8_t reason;
	} ssd;

	DBG("");

	ssd.reason = lock_code_to_reason(lock);

	if (!bd || !ssd.reason)
		goto error;

	ssd.service = enable ? QMI_VOICE_SS_ACTION_ACTIVATE :
				QMI_VOICE_SS_ACTION_DEACTIVATE;

	param = qmi_param_new();
	qmi_param_append(param, 0x01, sizeof(ssd), &ssd);

	if (cls != 7 /* BEARER_CLASS_DEFAULT */)
		qmi_param_append_uint8(param, 0x10, cls);

	qmi_param_append(param, 0x11, 4, passwd);

	if (qmi_service_send(bd->voice, QMI_VOICE_SET_SUPS_SERVICE, param,
				set_cb, cbd, g_free) > 0)
		return;

	qmi_param_free(param);
error:
	g_free(cbd);
	CALLBACK_WITH_FAILURE(cb, data);
}

static void query_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_barring_query_cb_t cb = cbd->cb;
	uint8_t mask;

	DBG("");

	if (qmi_result_set_error(result, NULL) ||
			!qmi_result_get_uint8(result, 0x10, &mask)) {
		CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
		return;
	}

	CALLBACK_WITH_SUCCESS(cb, mask, cbd->data);
}

static void qmi_query(struct ofono_call_barring *barr, const char *lock,
			int cls, ofono_call_barring_query_cb_t cb, void *data)
{
	struct call_barring_data *bd = ofono_call_barring_get_data(barr);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	uint8_t reason = lock_code_to_reason(lock);

	DBG("");

	if (!bd || !reason)
		goto error;

	param = qmi_param_new();
	qmi_param_append_uint8(param, 0x01, reason);

	if (cls != 7 /* BEARER_CLASS_DEFAULT */)
		qmi_param_append_uint8(param, 0x10, cls);

	if (qmi_service_send(bd->voice, QMI_VOICE_GET_CALL_BARRING, param,
				query_cb, cbd, g_free) > 0)
		return;

	qmi_param_free(param);
error:
	g_free(cbd);
	CALLBACK_WITH_FAILURE(cb, -1, data);
}

static void qmi_set_passwd(struct ofono_call_barring *barr, const char *lock,
				const char *old_passwd, const char *new_passwd,
				ofono_call_barring_set_cb_t cb, void *data)
{
	struct call_barring_data *bd = ofono_call_barring_get_data(barr);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	struct __attribute__((__packed__)) {
		uint8_t reason;
		uint8_t old_passwd[4];
		uint8_t new_passwd[4];
		uint8_t new_passwd_rpt[4];
	} ssd;

	DBG("");

	if (!bd)
		goto error;

	ssd.reason = lock_code_to_reason(lock);
	memcpy(&ssd.old_passwd, old_passwd, 4);
	memcpy(&ssd.new_passwd, new_passwd, 4);
	memcpy(&ssd.new_passwd_rpt, new_passwd, 4);

	param = qmi_param_new();

	qmi_param_append(param, 0x01, sizeof(ssd), &ssd);

	if (qmi_service_send(bd->voice, QMI_VOICE_SET_CALL_BARRING_PWD, param,
				set_cb, cbd, g_free) > 0)
		return;

	qmi_param_free(param);
error:
	g_free(cbd);
	CALLBACK_WITH_FAILURE(cb, data);
}

static void create_voice_cb(struct qmi_service *service, void *user_data)
{
	struct ofono_call_barring *barr = user_data;
	struct call_barring_data *bd = ofono_call_barring_get_data(barr);

	DBG("");

	if (!service) {
		ofono_error("Failed to request Voice service");
		ofono_call_barring_remove(barr);
		return;
	}

	bd->voice = qmi_service_ref(service);

	ofono_call_barring_register(barr);
}

static int qmi_call_barring_probe(struct ofono_call_barring *barr,
					unsigned int vendor, void *user_data)
{
	struct qmi_device *device = user_data;
	struct call_barring_data *bd;

	DBG("");

	bd = g_new0(struct call_barring_data, 1);

	ofono_call_barring_set_data(barr, bd);

	qmi_service_create_shared(device, QMI_SERVICE_VOICE,
					create_voice_cb, barr, NULL);

	return 0;
}

static void qmi_call_barring_remove(struct ofono_call_barring *barr)
{
	struct call_barring_data *bd = ofono_call_barring_get_data(barr);

	DBG("");

	ofono_call_barring_set_data(barr, NULL);

	if (bd->voice)
		qmi_service_unref(bd->voice);

	g_free(bd);
}

static const struct ofono_call_barring_driver driver = {
	.probe			= qmi_call_barring_probe,
	.remove			= qmi_call_barring_remove,
	.set			= qmi_set,
	.query			= qmi_query,
	.set_passwd		= qmi_set_passwd
};

OFONO_ATOM_DRIVER_BUILTIN(call_barring, qmimodem, &driver)
