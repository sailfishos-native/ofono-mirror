/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2011-2012  Intel Corporation. All rights reserved.
 *  Copyright (C) 2024 Adam Pigg <adam@piggz.co.uk>
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
#include <ofono/voicecall.h>
#include <src/common.h>
#include <ell/ell.h>

#include "voice.h"

#include "qmi.h"

#include "util.h"

struct voicecall_data {
	struct qmi_service *voice;
	uint16_t major;
	uint16_t minor;
	struct l_queue *call_list;
	struct ofono_phone_number dialed;
};

struct qmi_voice_call_information_instance {
	uint8_t id;
	uint8_t state;
	uint8_t type;
	uint8_t direction;
	uint8_t mode;
	uint8_t multipart_indicator;
	uint8_t als;
} __attribute__((__packed__));

struct qmi_voice_call_information {
	uint8_t size;
	struct qmi_voice_call_information_instance instance[0];
} __attribute__((__packed__));

struct qmi_voice_remote_party_number_instance {
	uint8_t call_id;
	uint8_t presentation_indicator;
	uint8_t number_size;
	char number[0];
} __attribute__((__packed__));

struct qmi_voice_remote_party_number {
	uint8_t size;
	struct qmi_voice_remote_party_number_instance instance[0];
} __attribute__((__packed__));

static int ofono_call_compare(const void *a, const void *b, void *data)
{
	const struct ofono_call *ca = a;
	const struct ofono_call *cb = b;

	if (ca->id < cb->id)
		return -1;

	if (ca->id > cb->id)
		return 1;

	return 0;
}

static bool ofono_call_match_by_id(const void *a, const void *b)
{
	const struct ofono_call *call = a;
	unsigned int id = L_PTR_TO_UINT(b);

	return call->id == id;
}

static bool ofono_call_match_by_status(const void *a, const void *b)
{
	const struct ofono_call *call = a;
	int status = L_PTR_TO_INT(b);

	return status == call->status;
}

static void ofono_call_list_dial_callback(struct ofono_voicecall *vc,
						int call_id)
{
	struct ofono_call *call;
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct l_queue *call_list = vd->call_list;
	const struct ofono_phone_number *ph = &vd->dialed;

	/* check if call_id already present */
	call = l_queue_find(call_list, ofono_call_match_by_id,
				L_UINT_TO_PTR(call_id));

	if (call)
		return;

	call = l_new(struct ofono_call, 1);
	call->id = call_id;

	memcpy(&call->called_number, ph, sizeof(*ph));
	call->direction = CALL_DIRECTION_MOBILE_ORIGINATED;
	call->status = CALL_STATUS_DIALING;
	call->type = 0; /* voice */

	l_queue_insert(call_list, call, ofono_call_compare, NULL);

	ofono_voicecall_notify(vc, call);
}

static void ofono_call_list_notify(struct ofono_voicecall *vc,
					struct l_queue *calls)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct l_queue *old_calls = vd->call_list;
	struct l_queue *new_calls = calls;
	struct ofono_call *new_call, *old_call;
	const struct l_queue_entry *old_entry, *new_entry;
	uint i;

	uint loop_length =
		MAX(l_queue_length(old_calls), l_queue_length(new_calls));

	old_entry = l_queue_get_entries(old_calls);
	new_entry = l_queue_get_entries(new_calls);

	for (i = 0; i < loop_length; ++i) {
		old_call = old_entry ? old_entry->data : NULL;
		new_call = new_entry ? new_entry->data : NULL;

		if (new_call && new_call->status == CALL_STATUS_DISCONNECTED) {
			ofono_voicecall_disconnected(
				vc, new_call->id,
				OFONO_DISCONNECT_REASON_REMOTE_HANGUP, NULL);
			new_entry = new_entry->next;
			l_queue_remove(calls, new_call);
			l_free(new_call);
			continue;
		}

		if (old_call &&
				(!new_call || (new_call->id > old_call->id)))
			ofono_voicecall_disconnected(
				vc, old_call->id,
				OFONO_DISCONNECT_REASON_REMOTE_HANGUP, NULL);
		else if (new_call &&
				(!old_call || (new_call->id < old_call->id))) {
			DBG("Notify new call %d", new_call->id);
			/* new call, signal it */
			if (new_call->type == 0)
				ofono_voicecall_notify(vc, new_call);
		} else if (memcmp(new_call, old_call, sizeof(*new_call)) &&
				new_call->type == 0)
			ofono_voicecall_notify(vc, new_call);

		if (old_entry)
			old_entry = old_entry->next;
		if (new_entry)
			new_entry = new_entry->next;
	}

	l_queue_destroy(old_calls, l_free);
	vd->call_list = calls;
}

static const char *qmi_voice_call_state_name(enum qmi_voice_call_state value)
{
	switch (value) {
	case QMI_VOICE_CALL_STATE_IDLE:
		return "QMI_VOICE_CALL_STATE_IDLE";
	case QMI_VOICE_CALL_STATE_ORIG:
		return "QMI_VOICE_CALL_STATE_ORIG";
	case QMI_VOICE_CALL_STATE_INCOMING:
		return "QMI_VOICE_CALL_STATE_INCOMING";
	case QMI_VOICE_CALL_STATE_CONV:
		return "QMI_VOICE_CALL_STATE_CONV";
	case QMI_VOICE_CALL_STATE_CC_IN_PROG:
		return "QMI_VOICE_CALL_STATE_CC_IN_PROG";
	case QMI_VOICE_CALL_STATE_ALERTING:
		return "QMI_VOICE_CALL_STATE_ALERTING";
	case QMI_VOICE_CALL_STATE_HOLD:
		return "QMI_VOICE_CALL_STATE_HOLD";
	case QMI_VOICE_CALL_STATE_WAITING:
		return "QMI_VOICE_CALL_STATE_WAITING";
	case QMI_VOICE_CALL_STATE_DISCONNECTING:
		return "QMI_VOICE_CALL_STATE_DISCONNECTING";
	case QMI_VOICE_CALL_STATE_END:
		return "QMI_VOICE_CALL_STATE_END";
	case QMI_VOICE_CALL_STATE_SETUP:
		return "QMI_VOICE_CALL_STATE_SETUP";
	}
	return "QMI_CALL_STATE_<UNKNOWN>";
}

static bool qmi_to_ofono_status(uint8_t status, int *ret)
{
	int err = false;

	switch (status) {
	case QMI_VOICE_CALL_STATE_IDLE:
	case QMI_VOICE_CALL_STATE_END:
	case QMI_VOICE_CALL_STATE_DISCONNECTING:
		*ret = CALL_STATUS_DISCONNECTED;
		break;
	case QMI_VOICE_CALL_STATE_HOLD:
		*ret = CALL_STATUS_HELD;
		break;
	case QMI_VOICE_CALL_STATE_WAITING:
		*ret = CALL_STATUS_WAITING;
		break;
	case QMI_VOICE_CALL_STATE_ORIG:
		*ret = CALL_STATUS_DIALING;
		break;
	case QMI_VOICE_CALL_STATE_SETUP:
	case QMI_VOICE_CALL_STATE_INCOMING:
		*ret = CALL_STATUS_INCOMING;
		break;
	case QMI_VOICE_CALL_STATE_CONV:
		*ret = CALL_STATUS_ACTIVE;
		break;
	case QMI_VOICE_CALL_STATE_CC_IN_PROG:
		*ret = CALL_STATUS_DIALING;
		break;
	case QMI_VOICE_CALL_STATE_ALERTING:
		*ret = CALL_STATUS_ALERTING;
		break;
	default:
		err = true;
	}
	return err;
}

static enum call_direction qmi_to_ofono_direction(uint8_t qmi_direction)
{
	return qmi_direction - 1;
}

static void all_call_status_ind(struct qmi_result *result, void *user_data)
{
	struct ofono_voicecall *vc = user_data;

	int i;
	int offset;
	uint16_t len;
	int instance_size;
	const struct qmi_voice_call_information *call_information;
	const struct qmi_voice_remote_party_number *remote_party_number;
	const struct qmi_voice_remote_party_number_instance *remote_party_number_inst[16];
	struct l_queue *calls;

	static const uint8_t RESULT_CALL_STATUS_CALL_INFORMATION = 0x01;
	static const uint8_t RESULT_CALL_STATUS_REMOTE_NUMBER = 0x10;
	static const uint8_t RESULT_CALL_INFO_CALL_INFORMATION = 0x10;
	static const uint8_t RESULT_CALL_INFO_REMOTE_NUMBER = 0x11;

	uint8_t remote_number_tlv = RESULT_CALL_STATUS_REMOTE_NUMBER;

	DBG("");

	/* mandatory */
	call_information = qmi_result_get(
		result, RESULT_CALL_STATUS_CALL_INFORMATION, &len);

	if (!call_information) {
		call_information = qmi_result_get(
			result, RESULT_CALL_INFO_CALL_INFORMATION,
			&len);
		remote_number_tlv = RESULT_CALL_INFO_REMOTE_NUMBER;
	}

	if (!call_information || len < sizeof(call_information->size)) {
		DBG("Parsing of all call status indication failed");
		return;
	}

	if (!call_information->size) {
		DBG("No call information received!");
		return;
	}

	if (len != call_information->size *
			sizeof(struct qmi_voice_call_information_instance) +
			sizeof(call_information->size)) {
		DBG("Call information size incorrect");
		return;
	}

	/* mandatory */
	remote_party_number = qmi_result_get(result, remote_number_tlv, &len);

	if (!remote_party_number) {
		DBG("Unable to retrieve remote numbers");
		return;
	}

	/* verify the length */
	if (len < sizeof(remote_party_number->size)) {
		DBG("Parsing of remote numbers failed");
		return;
	}

	/* expect we have valid fields for every call */
	if (call_information->size != remote_party_number->size) {
		DBG("Not all fields have the same size");
		return;
	}

	/* pull the remote call info into a local array */
	instance_size = sizeof(struct qmi_voice_remote_party_number_instance);

	for (i = 0, offset = sizeof(remote_party_number->size);
			offset < len && i < 16 && i < remote_party_number->size;
			i++) {
		const struct qmi_voice_remote_party_number_instance *instance;

		if (offset + instance_size > len) {
			DBG("Error parsing remote numbers");
			return;
		}

		instance = (void *)remote_party_number + offset;
		if (offset + instance_size + instance->number_size > len) {
			DBG("Error parsing remote numbers");
			return;
		}

		remote_party_number_inst[i] = instance;
		offset +=
			sizeof(struct qmi_voice_remote_party_number_instance) +
			instance->number_size;
	}

	calls = l_queue_new();

	for (i = 0; i < call_information->size && i < 16; i++) {
		struct ofono_call *call = l_new(struct ofono_call, 1);
		struct qmi_voice_call_information_instance call_info;
		const struct qmi_voice_remote_party_number_instance
			*remote_party = remote_party_number_inst[i];
		int number_size;
		char *tmp;

		call_info = call_information->instance[i];

		call->id = call_info.id;
		call->direction = qmi_to_ofono_direction(call_info.direction);
		call->type = 0; /* always voice */

		number_size = MIN(remote_party->number_size,
						OFONO_MAX_PHONE_NUMBER_LENGTH);
		tmp = l_strndup(remote_party->number, number_size);
		l_strlcpy(call->phone_number.number, tmp,
				sizeof(call->phone_number.number));
		l_free(tmp);

		if (strlen(call->phone_number.number) > 0)
			call->clip_validity = 0;
		else
			call->clip_validity = 2;

		if (qmi_to_ofono_status(call_info.state, &call->status)) {
			DBG("Ignore call id %d, because can not convert QMI state 0x%x to ofono.",
				call_info.id, call_info.state);
			l_free(call);
			continue;
		}

		DBG("Call %d in state %s(%d)", call_info.id,
			qmi_voice_call_state_name(call_info.state),
			call_info.state);

		l_queue_push_tail(calls, call);
	}

	ofono_call_list_notify(vc, calls);
}

static void dial_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_voicecall *vc = cbd->user;
	ofono_voicecall_cb_t cb = cbd->cb;
	uint16_t error;
	uint8_t call_id;

	static const uint8_t RESULT_CALL_ID = 0x10;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		DBG("QMI Error %d", error);
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	if (!qmi_result_get_uint8(result, RESULT_CALL_ID,
			&call_id)) {
		ofono_error("No call id in dial result");
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	DBG("New call QMI id %d", call_id);
	ofono_call_list_dial_callback(vc, call_id);

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void dial(struct ofono_voicecall *vc,
			const struct ofono_phone_number *ph,
			enum ofono_clir_option clir, ofono_voicecall_cb_t cb,
			void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param = qmi_param_new();

	const char *calling_number = phone_number_to_string(ph);

	static const uint8_t PARAM_CALL_NUMBER = 0x01;
	static const uint8_t PARAM_CALL_TYPE = 0x10;
	static const uint8_t QMI_VOICE_CALL_TYPE_VOICE = 0x00;

	DBG("");

	cbd->user = vc;
	memcpy(&vd->dialed, ph, sizeof(*ph));

	if (!qmi_param_append(param, PARAM_CALL_NUMBER,
			strlen(calling_number), calling_number))
		goto error;

	qmi_param_append_uint8(param, PARAM_CALL_TYPE,
				QMI_VOICE_CALL_TYPE_VOICE);

	if (qmi_service_send(vd->voice, QMI_VOICE_DIAL_CALL, param, dial_cb,
				cbd, l_free) > 0)
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	l_free(cbd);
	l_free(param);
}

static void answer_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_voicecall_cb_t cb = cbd->cb;
	uint16_t error;
	uint8_t call_id;

	static const uint8_t RESULT_CALL_ID = 0x10;

	DBG("");

	if (qmi_result_set_error(result, &error)) {
		DBG("QMI Error %d", error);
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	if (qmi_result_get_uint8(result, RESULT_CALL_ID, &call_id))
		DBG("Received answer result with call id %d", call_id);

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void answer(struct ofono_voicecall *vc,
					ofono_voicecall_cb_t cb, void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param = qmi_param_new();
	struct ofono_call *call;
	static const uint8_t PARAM_CALL_ID = 0x01;

	DBG("");

	call = l_queue_find(vd->call_list, ofono_call_match_by_status,
				L_UINT_TO_PTR(CALL_STATUS_INCOMING));
	if (!call) {
		ofono_error("Can not find a call to pick up");
		goto error;
	}

	cbd->user = vc;

	if (!qmi_param_append_uint8(param, PARAM_CALL_ID,
			call->id))
		goto error;

	if (qmi_service_send(vd->voice, QMI_VOICE_ANSWER_CALL, param,
			answer_cb, cbd, l_free) > 0)
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	l_free(cbd);
	l_free(param);
}

static void end_call_cb(struct qmi_result *result, void *user_data)
{
	struct cb_data *cbd = user_data;
	ofono_voicecall_cb_t cb = cbd->cb;
	uint16_t error;
	uint8_t call_id;

	static const uint8_t RESULT_CALL_ID = 0x10;

	if (qmi_result_set_error(result, &error)) {
		DBG("QMI Error %d", error);
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	if (qmi_result_get_uint8(result, RESULT_CALL_ID, &call_id))
		DBG("Received end call result with call id %d", call_id);

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void release_specific(struct ofono_voicecall *vc, int id,
			ofono_voicecall_cb_t cb, void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct cb_data *cbd = cb_data_new(cb, data);
	struct qmi_param *param = qmi_param_new();

	static const uint8_t PARAM_CALL_ID = 0x01;

	DBG("");

	cbd->user = vc;

	if (!qmi_param_append_uint8(param, PARAM_CALL_ID, id))
		goto error;

	if (qmi_service_send(vd->voice, QMI_VOICE_END_CALL, param, end_call_cb,
				cbd, l_free) > 0)
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	l_free(cbd);
	l_free(param);
}

static void hangup_active(struct ofono_voicecall *vc, ofono_voicecall_cb_t cb,
				void *data)
{
	struct voicecall_data *vd = ofono_voicecall_get_data(vc);
	struct ofono_call *call;
	enum call_status active[] = {
		CALL_STATUS_ACTIVE,
		CALL_STATUS_DIALING,
		CALL_STATUS_ALERTING,
		CALL_STATUS_INCOMING,
	};

	DBG("");

	for (uint32_t i = 0; i < L_ARRAY_SIZE(active); i++) {
		call = l_queue_find(vd->call_list, ofono_call_match_by_status,
			L_INT_TO_PTR(active[i]));

		if (call)
			break;
	}

	if (call == NULL) {
		DBG("Can not find a call to hang up");
		CALLBACK_WITH_FAILURE(cb, data);
		return;
	}

	release_specific(vc, call->id, cb, data);
}

static void create_voice_cb(struct qmi_service *service, void *user_data)
{
	struct ofono_voicecall *vc = user_data;
	struct voicecall_data *data = ofono_voicecall_get_data(vc);

	DBG("");

	if (!service) {
		ofono_error("Failed to request Voice service");
		ofono_voicecall_remove(vc);
		return;
	}

	if (!qmi_service_get_version(service, &data->major, &data->minor)) {
		ofono_error("Failed to get Voice service version");
		ofono_voicecall_remove(vc);
		return;
	}

	data->voice = qmi_service_ref(service);

	qmi_service_register(data->voice, QMI_VOICE_ALL_CALL_STATUS_IND,
				all_call_status_ind, vc, NULL);

	ofono_voicecall_register(vc);
}

static int qmi_voicecall_probe(struct ofono_voicecall *vc,
					unsigned int vendor, void *user_data)
{
	struct qmi_device *device = user_data;
	struct voicecall_data *data;

	DBG("");

	data = l_new(struct voicecall_data, 1);
	data->call_list = l_queue_new();

	ofono_voicecall_set_data(vc, data);

	qmi_service_create(device, QMI_SERVICE_VOICE,
					create_voice_cb, vc, NULL);

	return 0;
}

static void qmi_voicecall_remove(struct ofono_voicecall *vc)
{
	struct voicecall_data *data = ofono_voicecall_get_data(vc);

	DBG("");

	ofono_voicecall_set_data(vc, NULL);

	qmi_service_unregister_all(data->voice);

	qmi_service_unref(data->voice);

	l_queue_destroy(data->call_list, l_free);
	l_free(data);
}

static const struct ofono_voicecall_driver driver = {
	.probe		= qmi_voicecall_probe,
	.remove		= qmi_voicecall_remove,
	.dial		= dial,
	.answer		= answer,
	.hangup_active  = hangup_active,
	.release_specific  = release_specific,
};

OFONO_ATOM_DRIVER_BUILTIN(voicecall, qmimodem, &driver)
