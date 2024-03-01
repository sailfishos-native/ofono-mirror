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
#include <ofono/call-forwarding.h>

#include "qmi.h"
#include "voice.h"
#include "util.h"

struct call_forwarding_data {
	struct qmi_service *voice;
};

struct call_forwarding_info_ext {
	uint8_t active;
	uint8_t cls;
	uint8_t time;
	uint8_t pind;
	uint8_t sind;
	uint8_t type;
	uint8_t plan;
	uint8_t len;
	uint8_t number[];
};

static int forw_type_to_reason(int type)
{
	switch (type) {
	case 0:
		return QMI_VOICE_SS_RSN_FWD_UNCONDITIONAL;
	case 1:
		return QMI_VOICE_SS_RSN_FWD_MOBILE_BUSY;
	case 2:
		return QMI_VOICE_SS_RSN_FWD_NO_REPLY;
	case 3:
		return QMI_VOICE_SS_RSN_FWD_UNREACHABLE;
	case 4:
		return QMI_VOICE_SS_RSN_FWD_ALL;
	case 5:
		return QMI_VOICE_SS_RSN_FWD_ALL_CONDITIONAL;
	default:
		DBG("Unknown forwarding type %d", type);
		return 0;
	}
}

static void set_fwd_cond(struct ofono_call_forwarding_condition *cond,
				int status, int cls, int time, int type,
				uint8_t *number, uint8_t nlen)
{
	uint8_t maxlen = OFONO_MAX_PHONE_NUMBER_LENGTH;

	cond->status = status;
	cond->cls = cls;
	cond->time = time;
	cond->phone_number.type = type;

	if (nlen < maxlen)
		maxlen = nlen;

	memcpy(&cond->phone_number.number, number, maxlen);
	cond->phone_number.number[maxlen] = 0;
}

static void query_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_forwarding_query_cb_t cb = cbd->cb;
	const uint8_t *p;
	uint8_t num;
	uint16_t length;

	DBG("");

	if (qmi_result_set_error(result, NULL))
		goto error;

	/*
	 * we want extended info, because of the number type.
	 * FIXME - shall we fallback to 0x10 if there is no extended info?
	 */
	p = qmi_result_get(result, 0x16, &length);
	if (p && length) {
		struct ofono_call_forwarding_condition *list;
		const uint8_t *end = p + length;
		int i;

		num = *p++;

		list = l_new(struct ofono_call_forwarding_condition, num);

		for (i = 0; i < num; i++) {
			struct call_forwarding_info_ext *info = (void *)p;
			int type;

			/* do not try to access beyond buffer end */
			if (p + sizeof(*info) > end ||
					p + sizeof(*info) + info->len > end) {
				l_free(list);
				goto error;
			}

			if (info->type == 1)
				type = OFONO_NUMBER_TYPE_INTERNATIONAL;
			else
				type = OFONO_NUMBER_TYPE_UNKNOWN;

			set_fwd_cond(&list[i], info->active, info->cls,
					info->time, type, info->number,
					info->len);
			p += sizeof(*info) + info->len;
		}

		CALLBACK_WITH_SUCCESS(cb, num, list, cbd->data);
		l_free(list);
		return;
	}

error:
	CALLBACK_WITH_FAILURE(cb, 0, NULL, cbd->data);
}

static void qmi_query(struct ofono_call_forwarding *cf, int type, int cls,
			ofono_call_forwarding_query_cb_t cb, void *data)
{
	struct call_forwarding_data *cfd = ofono_call_forwarding_get_data(cf);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	uint8_t reason = forw_type_to_reason(type);

	DBG("");

	if (!cfd || !reason)
		goto error;

	param = qmi_param_new();
	qmi_param_append_uint8(param, 0x01, reason);

	if (cls != 7 /* BEARER_CLASS_DEFAULT */)
		qmi_param_append_uint8(param, 0x10, cls);

	if (qmi_service_send(cfd->voice, QMI_VOICE_GET_CALL_FWDING, param,
				query_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);
error:
	l_free(cbd);
	CALLBACK_WITH_FAILURE(cb, 0, NULL, data);
}

static void set_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_call_forwarding_set_cb_t cb = cbd->cb;

	DBG("");

	if (!qmi_result_set_error(result, NULL))
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
	else
		CALLBACK_WITH_FAILURE(cb, cbd->data);
}

static void qmi_register(struct ofono_call_forwarding *cf, int type, int cls,
				const struct ofono_phone_number *ph, int time,
				ofono_call_forwarding_set_cb_t cb, void *data)
{
	struct call_forwarding_data *cfd = ofono_call_forwarding_get_data(cf);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	struct __attribute__((__packed__)) {
		uint8_t service;
		uint8_t reason;
	} ssd;
	struct __attribute__((__packed__)) {
		uint8_t type;
		uint8_t plan;
	} tpd;

	DBG("");

	ssd.reason = forw_type_to_reason(type);

	if (!cfd || !ssd.reason)
		goto error;

	ssd.service = QMI_VOICE_SS_ACTION_REGISTER;

	param = qmi_param_new();
	qmi_param_append(param, 0x01, sizeof(ssd), &ssd);

	if (cls != 7 /* BEARER_CLASS_DEFAULT */)
		qmi_param_append_uint8(param, 0x10, cls);

	qmi_param_append(param, 0x12, strlen(ph->number), ph->number);
	qmi_param_append_uint8(param, 0x13, time);

	tpd.type = ph->type == OFONO_NUMBER_TYPE_INTERNATIONAL ? 1 : 0;
	tpd.plan = tpd.type;
	qmi_param_append(param, 0x14, sizeof(tpd), &tpd);

	if (qmi_service_send(cfd->voice, QMI_VOICE_SET_SUPS_SERVICE, param,
				set_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);
error:
	l_free(cbd);
	CALLBACK_WITH_FAILURE(cb, data);
}

static void qmi_set(struct ofono_call_forwarding *cf, int type, int cls,
			int service, ofono_call_forwarding_set_cb_t cb,
			void *data)
{
	struct call_forwarding_data *cfd = ofono_call_forwarding_get_data(cf);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param;
	struct __attribute__((__packed__)) {
		uint8_t service;
		uint8_t reason;
	} ssd;

	DBG("");

	ssd.reason = forw_type_to_reason(type);

	if (!cfd || !ssd.reason)
		goto error;

	ssd.service = service;

	param = qmi_param_new();
	qmi_param_append(param, 0x01, sizeof(ssd), &ssd);

	if (cls != 7 /* BEARER_CLASS_DEFAULT */)
		qmi_param_append_uint8(param, 0x10, cls);

	if (qmi_service_send(cfd->voice, QMI_VOICE_SET_SUPS_SERVICE, param,
				set_cb, cbd, l_free) > 0)
		return;

	qmi_param_free(param);

error:
	l_free(cbd);
	CALLBACK_WITH_FAILURE(cb, data);
}

static void qmi_activate(struct ofono_call_forwarding *cf, int type, int cls,
				ofono_call_forwarding_set_cb_t cb, void *data)
{
	qmi_set(cf, type, cls, QMI_VOICE_SS_ACTION_ACTIVATE, cb, data);
}

static void qmi_deactivate(struct ofono_call_forwarding *cf, int type, int cls,
				ofono_call_forwarding_set_cb_t cb, void *data)
{
	qmi_set(cf, type, cls, QMI_VOICE_SS_ACTION_DEACTIVATE, cb, data);
}

static void qmi_erase(struct ofono_call_forwarding *cf, int type, int cls,
			ofono_call_forwarding_set_cb_t cb, void *data)
{
	qmi_set(cf, type, cls, QMI_VOICE_SS_ACTION_ERASE, cb, data);
}

static void create_voice_cb(struct qmi_service *service, void *user_data)
{
	struct ofono_call_forwarding *cf = user_data;
	struct call_forwarding_data *cfd = ofono_call_forwarding_get_data(cf);

	DBG("");

	if (!service) {
		ofono_error("Failed to request Voice service");
		ofono_call_forwarding_remove(cf);
		return;
	}

	cfd->voice = qmi_service_ref(service);

	ofono_call_forwarding_register(cf);
}

static int qmi_call_forwarding_probe(struct ofono_call_forwarding *cf,
					unsigned int vendor, void *user_data)
{
	struct qmi_device *device = user_data;
	struct call_forwarding_data *cfd;

	DBG("");

	cfd = l_new(struct call_forwarding_data, 1);

	ofono_call_forwarding_set_data(cf, cfd);

	qmi_service_create_shared(device, QMI_SERVICE_VOICE,
					create_voice_cb, cf, NULL);

	return 0;
}

static void qmi_call_forwarding_remove(struct ofono_call_forwarding *cf)
{
	struct call_forwarding_data *cfd = ofono_call_forwarding_get_data(cf);

	DBG("");

	ofono_call_forwarding_set_data(cf, NULL);

	if (cfd->voice)
		qmi_service_unref(cfd->voice);

	l_free(cfd);
}

static const struct ofono_call_forwarding_driver driver = {
	.probe			= qmi_call_forwarding_probe,
	.remove			= qmi_call_forwarding_remove,
	.registration		= qmi_register,
	.activation		= qmi_activate,
	.query			= qmi_query,
	.deactivation		= qmi_deactivate,
	.erasure		= qmi_erase
};

OFONO_ATOM_DRIVER_BUILTIN(call_forwarding, qmimodem, &driver)
